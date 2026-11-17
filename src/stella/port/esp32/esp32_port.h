#pragma once
// Arduino-ESP32 port: Platform, NVS storage and a Wi-Fi job. Header-only so it
// is compiled into the sketch (Arduino builds library sources separately).
// Requires Arduino-ESP32 3.x.
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <time.h>

#include "../../core/log.h"
#include "../../core/text.h"
#include "../../hal/hal.h"
#include "../../runtime/runtime.h"
#include "../../service/job_scheduler.h"

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

// Keeps a station connection alive, starts SNTP, and publishes NetworkStatus.
// Runs on the service thread as an ordinary Job. Credentials are never logged.
class WifiJob : public Job {
 public:
  WifiJob(Runtime &os, const char *ssid, const char *password) : os_(os), ssid_(ssid), password_(password) {}
  bool configured() const { return ssid_ && ssid_[0]; }
  const char *name() const override { return "wifi"; }
  JobPolicy policy() const override {
    JobPolicy p;
    p.intervalMs = 5000;   // status refresh while healthy
    p.retryMs = 10000;     // reconnect attempts back off to...
    p.maxBackoffMs = 120000;
    return p;
  }
  JobResult run(uint32_t now) override {
    if (!started_) {
      WiFi.persistent(false);
      WiFi.mode(WIFI_STA);
      WiFi.setAutoReconnect(true);
      WiFi.begin(ssid_, password_);
      started_ = true;
      startedMs_ = now;
      publish(false);
      return JobResult::Ok;
    }
    const bool up = WiFi.status() == WL_CONNECTED;
    if (up && !sntp_) {
      configTzTime(os_.settings().timezone, "pool.ntp.org", "time.google.com");
      sntp_ = true;
      LOGI("wifi", "connected; time sync started");
    }
    publish(up);
    os_.jobs().setNetworkAvailable(up);
    if (up) return JobResult::Ok;
    // Give the first association 20 s before treating it as a failure.
    if ((uint32_t)(now - startedMs_) < 20000) return JobResult::Ok;
    WiFi.reconnect();
    return JobResult::Failed;
  }

 private:
  void publish(bool up) {
    NetworkStatus s;
    s.present = true;
    s.connected = up;
    if (up) {
      s.rssi = (int8_t)WiFi.RSSI();
      copyText(s.ssid, sizeof(s.ssid), WiFi.SSID().c_str());
      copyText(s.ip, sizeof(s.ip), WiFi.localIP().toString().c_str());
    }
    const NetworkStatus old = os_.networkTopic().get();
    if (old.connected != s.connected || old.present != s.present || strcmp(old.ip, s.ip) != 0 ||
        old.rssi / 6 != s.rssi / 6) {
      os_.networkTopic().publish(s);
    }
  }

  Runtime &os_;
  const char *ssid_;
  const char *password_;
  bool started_ = false, sntp_ = false;
  uint32_t startedMs_ = 0;
};

}  // namespace esp32
}  // namespace stella
