#pragma once
// Arduino-ESP32 port: Platform, NVS storage, Wi-Fi and HTTPS drivers. Header-only so it
// is compiled into the sketch (Arduino builds library sources separately).
// Requires Arduino-ESP32 3.x.
#include <Arduino.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <mbedtls/pk.h>
#include <time.h>

#include "../../core/log.h"
#include "../../core/text.h"
#include "../../hal/hal.h"
#include "../../core/deadline.h"
#include "../../net/firmware_update.h"
#include "../../net/http_request.h"
#include "../../net/setup_portal.h"
#include "../../runtime/runtime.h"

namespace stella {
namespace esp32 {

// Survives esp_restart() (not power loss); the runtime validates it.
inline unsigned char *retainedBlock() {
  static RTC_NOINIT_ATTR unsigned char block[64];
  return block;
}

class Esp32Platform : public Platform {
 public:
  explicit Esp32Platform(const char *boardName) : name_(boardName) {}
  const char *boardName() const override { return name_; }
  uint32_t millis() const override { return (uint32_t)(esp_timer_get_time() / 1000); }
  bool wallClock(int64_t &epoch) const override {
    const time_t now = time(nullptr);
    if (now < 1700000000) return false;  // not yet set by SNTP
    epoch = (int64_t)now;
    return true;
  }
  MemoryStats memory() const override {
    return MemoryStats{(uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                       (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                       (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)};
  }
  ResetCause resetCause() const override {
    switch (esp_reset_reason()) {
      case ESP_RST_POWERON: return ResetCause::PowerOn;
      case ESP_RST_SW: return ResetCause::Software;
      case ESP_RST_PANIC: return ResetCause::Panic;
      case ESP_RST_INT_WDT:
      case ESP_RST_TASK_WDT:
      case ESP_RST_WDT: return ResetCause::Watchdog;
      case ESP_RST_BROWNOUT: return ResetCause::Brownout;
      case ESP_RST_EXT: return ResetCause::External;
      default: return ResetCause::Unknown;
    }
  }
  void restart() override {
    fflush(stdout);
    delay(50);
    esp_restart();
  }
  void *retainedMemory(size_t &size) override {
    size = 64;
    return retainedBlock();
  }
  uint32_t stackHeadroom() const override { return (uint32_t)uxTaskGetStackHighWaterMark(nullptr); }
  // After an over-the-air update the bootloader runs the new image "pending
  // verification" and returns to the old one at the next reset unless it is
  // confirmed (the Arduino core's own confirmation is deferred by the
  // sketch's verifyRollbackLater(); see examples/CYD/CYD.ino).
  bool updatePending() const override {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    return running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
  }
  void confirmUpdate() override { esp_ota_mark_app_valid_cancel_rollback(); }

 private:
  const char *name_;
};

// NVS via Arduino Preferences. One short-lived handle per call keeps it safe
// from any thread (NVS has its own lock).
class NvsStore : public KeyValueStore {
 public:
  int read(const char *ns, const char *key, void *out, size_t capacity) override {
    Preferences p;
    if (!p.begin(ns, true)) return -1;
    int result = -1;
    if (p.isKey(key)) {
      const size_t len = p.getBytesLength(key);
      result = (int)len;
      if (len <= capacity && p.getBytes(key, out, len) != len) result = -1;
    }
    p.end();
    return result;
  }
  bool write(const char *ns, const char *key, const void *data, size_t size) override {
    Preferences p;
    if (!p.begin(ns, false)) return false;
    const bool ok = p.putBytes(key, data, size) == size;
    p.end();
    return ok;
  }
  bool erase(const char *ns, const char *key) override {
    Preferences p;
    if (!p.begin(ns, false)) return false;
    const bool ok = p.remove(key);
    p.end();
    return ok;
  }
};

// Wi-Fi station driver. Connection policy (backoff, saved credentials) lives
// in the portable WifiManager; this only translates to the Arduino WiFi API.
class Esp32WifiDriver : public WifiDriver {
 public:
  bool begin() override {
    WiFi.persistent(false);  // credentials live in Stella's own record
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);  // WifiManager owns reconnection
    return true;
  }
  bool connect(const char *ssid, const char *password) override {
    attempting_ = true;
    WiFi.begin(ssid, password && password[0] ? password : nullptr);
    return true;
  }
  void disconnect() override {
    attempting_ = false;
    WiFi.disconnect(false);
  }
  WifiLink link() override {
    switch (WiFi.status()) {
      case WL_CONNECTED: return WifiLink::Connected;
      case WL_CONNECT_FAILED:
      case WL_NO_SSID_AVAIL: return attempting_ ? WifiLink::Failed : WifiLink::Idle;
      case WL_CONNECTION_LOST: return WifiLink::Idle;
      default: return attempting_ ? WifiLink::Connecting : WifiLink::Idle;
    }
  }
  int8_t rssi() override { return (int8_t)WiFi.RSSI(); }
  uint32_t random32() override { return esp_random(); }  // hardware RNG (strong while the radio is on)
  void address(char *out, size_t capacity) override { copyText(out, capacity, WiFi.localIP().toString().c_str()); }
  bool startScan() override { return WiFi.scanNetworks(/*async=*/true, /*hidden=*/false) >= -1; }
  int scanResults(WifiNetwork *out, int capacity) override {
    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return -1;
    if (n < 0) return -2;
    int count = 0;
    for (int i = 0; i < n && count < capacity; ++i) {
      copyText(out[count].ssid, sizeof(out[count].ssid), WiFi.SSID(i).c_str());
      out[count].rssi = (int8_t)WiFi.RSSI(i);
      out[count].secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      ++count;
    }
    WiFi.scanDelete();
    return count;
  }
  void startTimeSync() override {
    // configTzTime sets TZ too: pass the runtime's current zone unchanged.
    char tz[64];
    const char *current = getenv("TZ");
    copyText(tz, sizeof(tz), current ? current : "UTC0");
    configTzTime(tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  }

 private:
  bool attempting_ = false;
};

// The other app slot of an OTA partition layout (two app partitions). Never
// writes over the running image: with a single-app layout (e.g. Huge APP, the `cyd-huge` profile)
// there is no other slot and updates are reported unavailable. An optional
// public key (PEM) makes the manifest's signature mandatory; it is checked
// with mbedTLS (ECDSA P-256 over SHA-256).
class Esp32Firmware : public FirmwareSlot {
 public:
  explicit Esp32Firmware(const char *publicKeyPem = nullptr) : pem_(publicKeyPem) {}
  void setPublicKey(const char *pem) { pem_ = pem; }

  bool available() override {
    const esp_partition_t *p = target();
    return p && p->size >= 256 * 1024;
  }
  bool requiresSignature() override { return pem_ && pem_[0]; }
  size_t capacity() override {
    const esp_partition_t *p = target();
    return p ? p->size : 0;
  }
  bool begin(size_t size) override {
    abort();
    part_ = target();
    if (!part_ || size > part_->size) return false;
    open_ = esp_ota_begin(part_, size, &handle_) == ESP_OK;
    return open_;
  }
  bool write(const uint8_t *data, size_t size) override { return open_ && esp_ota_write(handle_, data, size) == ESP_OK; }
  bool finish() override {
    if (!open_) return false;
    open_ = false;
    // esp_ota_end() validates the image (header, checksum); only then is it
    // chosen for the next boot, and the bootloader treats it as pending.
    if (esp_ota_end(handle_) != ESP_OK) return false;
    return esp_ota_set_boot_partition(part_) == ESP_OK;
  }
  void abort() override {
    if (open_) esp_ota_abort(handle_);
    open_ = false;
  }
  bool verify(const uint8_t digest[32], const uint8_t r[32], const uint8_t s[32]) override {
    if (!pem_ || !pem_[0]) return false;
    uint8_t der[72];
    const size_t derLength = UpdateService::encodeDerSignature(r, s, der);
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    bool ok = mbedtls_pk_parse_public_key(&key, reinterpret_cast<const unsigned char *>(pem_), strlen(pem_) + 1) == 0 &&
              mbedtls_pk_can_do(&key, MBEDTLS_PK_ECDSA) && mbedtls_pk_get_bitlen(&key) == 256 &&
              mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, 32, der, derLength) == 0;
    mbedtls_pk_free(&key);
    return ok;
  }

 private:
  static const esp_partition_t *target() {
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    return next && next != esp_ota_get_running_partition() ? next : nullptr;
  }
  const char *pem_;
  const esp_partition_t *part_ = nullptr;
  esp_ota_handle_t handle_ = 0;
  bool open_ = false;
};

// Phone setup: a WPA2 access point, a captive DNS (every name resolves to the
// device, so a phone opens the page by itself) and a tiny web server. All
// meaning lives in the portable SetupPortal; this only moves bytes. Runs on
// the service thread (poll() is called about every 25 ms while setup is open).
//
// The server never waits. A phone that joins opens several connections at once
// (captive-portal probes, speculative sockets that send nothing), and the
// stock WebServer serves one client at a time and waits up to five seconds on
// each silent one, which made the page take about a minute. Here every
// connection is read without blocking, a few are open at once, a request is
// answered as soon as it is complete, and a socket that stays silent is
// closed after kIdleMs.
class Esp32SetupServer : public SetupServer {
 public:
  static constexpr int kSlots = 4;
  static constexpr uint32_t kIdleMs = 2500;

  bool begin(const char *apSsid, const char *apPassword, SetupHandler &handler) override {
    handler_ = &handler;
    WiFi.mode(WIFI_AP_STA);  // keep the station side alive for scanning and joining
    if (!WiFi.softAP(apSsid, apPassword, /*channel=*/1, /*ssid_hidden=*/0, /*max_connection=*/3)) {
      WiFi.mode(WIFI_STA);
      return false;
    }
    dns_.start(53, "*", WiFi.softAPIP());
    server_.begin();
    server_.setNoDelay(true);
    running_ = true;
    return true;
  }
  void poll() override {
    if (!running_) return;
    for (int i = 0; i < 4; ++i) dns_.processNextRequest();  // a phone asks many names at once
    // Take every connection that is waiting.
    for (;;) {
      NetworkClient client = server_.accept();
      if (!client) break;
      Slot *free = nullptr;
      for (Slot &slot : slots_)
        if (!slot.client) {
          free = &slot;
          break;
        }
      if (!free) {  // all busy: the longest-silent one makes room
        free = &slots_[0];
        for (Slot &slot : slots_)
          if (elapsedMs(millis(), slot.lastMs) > elapsedMs(millis(), free->lastMs)) free = &slot;
        close(*free);
      }
      free->client = client;
      free->parser.reset();
      free->lastMs = millis();
    }
    for (Slot &slot : slots_) {
      if (!slot.client) continue;
      uint8_t buffer[128];
      int budget = 8;  // a few reads per client per poll keeps one busy phone from starving the rest
      while (budget-- > 0 && slot.client.connected() && slot.client.available() > 0 && !slot.parser.done()) {
        const int n = slot.client.read(buffer, sizeof(buffer));
        if (n <= 0) break;
        slot.lastMs = millis();
        slot.parser.feed(buffer, (size_t)n);
      }
      if (slot.parser.done()) {
        respond(slot);
        close(slot);
      } else if (!slot.client.connected() && slot.client.available() <= 0) {
        close(slot);  // the phone hung up without asking for anything
      } else if (elapsedMs(millis(), slot.lastMs) >= kIdleMs) {
        close(slot);  // a speculative connection that never spoke
      }
    }
  }
  void end() override {
    if (!running_) return;
    for (Slot &slot : slots_) close(slot);
    server_.end();
    dns_.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    running_ = false;
    handler_ = nullptr;
  }
  void address(char *out, size_t capacity) override { copyText(out, capacity, WiFi.softAPIP().toString().c_str()); }

 private:
  struct Slot {
    NetworkClient client;
    HttpRequestParser parser;  // wiped on reset: the form holds the Wi-Fi password
    uint32_t lastMs = 0;
  };

  void close(Slot &slot) {
    slot.parser.reset();
    if (slot.client) slot.client.stop();
    slot.client = NetworkClient();
  }

  void respond(Slot &slot) {
    const HttpRequestParser &req = slot.parser;
    int status = 200;
    const char *type = "text/html; charset=utf-8";
    size_t length = 0;
    if (req.state() == HttpRequestParser::State::Failed) {
      status = req.errorStatus();
      type = "text/plain";
      length = (size_t)snprintf(page_, sizeof(page_), "Bad request");
    } else if (handler_) {
      SetupRequest request;
      request.post = req.post();
      request.path = req.path();
      request.body = req.body();
      request.bodyLen = req.bodyLen();
      SetupResponse response;
      response.body = page_;
      response.capacity = sizeof(page_);
      handler_->handleSetup(request, response);
      length = response.length < sizeof(page_) ? response.length : sizeof(page_) - 1;
      status = response.status;
      type = response.contentType;
    }
    page_[length] = '\0';
    char head[160];
    const int h = snprintf(head, sizeof(head),
                           "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                           "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                           status, status == 200 ? "OK" : "Error", type, (unsigned)length);
    slot.client.write((const uint8_t *)head, (size_t)(h > 0 && h < (int)sizeof(head) ? h : sizeof(head) - 1));
    // A GET page is a few KB; write it in pieces so a slow phone is not waited for in one go.
    const uint8_t *data = (const uint8_t *)page_;
    size_t left = length;
    while (left > 0 && slot.client.connected()) {
      const size_t n = slot.client.write(data, left < 1024 ? left : 1024);
      if (n == 0) break;
      data += n;
      left -= n;
    }
    slot.client.flush();
  }

  NetworkServer server_{80};
  DNSServer dns_;
  SetupHandler *handler_ = nullptr;
  bool running_ = false;
  Slot slots_[kSlots];
  char page_[SetupPortal::kPageBytes];
};

// HTTPS GET with certificate verification against the core's built-in CA
// bundle. One TLS session at a time (the caller holds the network lease).
class Esp32HttpClient : public HttpClient {
 public:
  int get(const char *url, uint32_t timeoutMs, size_t maxBodyBytes, HttpSink &sink) override {
    if (WiFi.status() != WL_CONNECTED) return kHttpErrNoNetwork;
    WiFiClientSecure tls;
    tls.useBuiltinCACertBundle();
    tls.setHandshakeTimeout(10);
    HTTPClient http;
    http.setConnectTimeout(timeoutMs < 8000 ? timeoutMs : 8000);
    http.setTimeout((uint16_t)(timeoutMs < 65000 ? timeoutMs : 65000));
    http.setReuse(false);
    http.setUserAgent("Stella/" STELLA_VERSION);
    http.setAcceptEncoding("identity");
    if (!http.begin(tls, url)) return kHttpErrConnect;
    const int code = http.GET();
    if (code <= 0) {
      http.end();
      return code == HTTPC_ERROR_READ_TIMEOUT ? kHttpErrTimeout : kHttpErrConnect;
    }
    if (code < 200 || code >= 300) {
      http.end();
      return code;
    }
    const int length = http.getSize();
    if (length > 0 && (size_t)length > maxBodyBytes) {
      http.end();
      return kHttpErrTooLarge;
    }
    SinkStream stream(sink, maxBodyBytes);
    const int written = http.writeToStream(&stream);  // decodes chunked transfer
    http.end();
    if (stream.tooLarge) return kHttpErrTooLarge;
    if (stream.aborted) return kHttpErrAborted;
    if (written < 0) return written == HTTPC_ERROR_READ_TIMEOUT ? kHttpErrTimeout : kHttpErrConnect;
    return code;
  }

 private:
  // Adapts HTTPClient::writeToStream() to an HttpSink with a byte limit.
  class SinkStream : public Stream {
   public:
    SinkStream(HttpSink &sink, size_t limit) : sink_(sink), limit_(limit) {}
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t *data, size_t size) override {
      if (aborted || tooLarge) return 0;
      if (total_ + size > limit_) {
        tooLarge = true;
        return 0;
      }
      total_ += size;
      if (!sink_.onData(data, size)) {
        aborted = true;
        return 0;
      }
      return size;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    bool aborted = false, tooLarge = false;

   private:
    HttpSink &sink_;
    size_t limit_;
    size_t total_ = 0;
  };
};

}  // namespace esp32
}  // namespace stella
