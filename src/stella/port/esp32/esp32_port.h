#pragma once
// Arduino-ESP32 port: Platform, NVS storage, Wi-Fi and HTTPS drivers. Header-only so it
// is compiled into the sketch (Arduino builds library sources separately).
// Requires Arduino-ESP32 3.x.
#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <time.h>

#include "../../core/log.h"
#include "../../core/text.h"
#include "../../hal/hal.h"
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
