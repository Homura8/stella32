#pragma once
// Host (Linux) board: deterministic stand-ins for every HAL interface, used by
// the unit tests and the screenshot simulator. Time only moves when told to.
#include <atomic>
#include <map>
#include <string>
#include <vector>

#include "stella/hal/hal.h"

namespace stella {
namespace host {

class SimDisplay : public Display {
 public:
  SimDisplay(int width, int height) : w_(width), h_(height), pixels_((size_t)width * height, 0) {}
  int16_t width() const override { return (int16_t)w_; }
  int16_t height() const override { return (int16_t)h_; }
  void writePixels(const Rect &area, const Color *pixels) override;
  void setBacklight(uint8_t level) override { backlight_ = level; }

  Color pixel(int x, int y) const { return pixels_[(size_t)y * w_ + x]; }
  const std::vector<Color> &framebuffer() const { return pixels_; }
  uint8_t backlight() const { return backlight_; }
  uint64_t pixelsWritten() const { return written_; }
  uint32_t writes() const { return writes_; }
  void resetCounters() { written_ = 0; writes_ = 0; }
  bool writePpm(const std::string &path) const;

 private:
  int w_, h_;
  std::vector<Color> pixels_;
  uint8_t backlight_ = 0;
  uint64_t written_ = 0;
  uint32_t writes_ = 0;
};

class ScriptedTouch : public TouchInput {
 public:
  bool sample(TouchSample &out) override {
    out = current_;
    return true;
  }
  void down(int x, int y) {
    current_.pressed = true;
    current_.x = (int16_t)x;
    current_.y = (int16_t)y;
    current_.pressure = 900;
  }
  void up() { current_.pressed = false; }

 private:
  TouchSample current_;
};

class SimPlatform : public Platform {
 public:
  const char *boardName() const override { return "Host simulator"; }
  uint32_t millis() const override { return ms_; }
  bool wallClock(int64_t &epoch) const override {
    if (!timeKnown_) return false;
    epoch = epochBase_ + (int64_t)((ms_ - epochAtMs_) / 1000u);
    return true;
  }
  MemoryStats memory() const override { return MemoryStats{142000, 118000, 98000}; }
  ResetCause resetCause() const override { return ResetCause::PowerOn; }
  void restart() override { restarts_.fetch_add(1); }
  void *retainedMemory(size_t &size) override {
    size = sizeof(retained_);
    return retained_;
  }

  void advance(uint32_t ms) { ms_.fetch_add(ms); }
  void setMillis(uint32_t ms) { ms_ = ms; }
  void setEpoch(int64_t epoch) {
    timeKnown_ = true;
    epochBase_ = epoch;
    epochAtMs_ = ms_;
  }
  void clearTime() { timeKnown_ = false; }
  int restarts() const { return restarts_.load(); }

 private:
  std::atomic<uint32_t> ms_{1000};  // read by the service thread
  bool timeKnown_ = false;
  int64_t epochBase_ = 0;
  uint32_t epochAtMs_ = 0;
  std::atomic<int> restarts_{0};
  alignas(8) unsigned char retained_[64] = {};
};

class MemoryStore : public KeyValueStore {
 public:
  int read(const char *ns, const char *key, void *out, size_t capacity) override;
  bool write(const char *ns, const char *key, const void *data, size_t size) override;
  bool erase(const char *ns, const char *key) override;
  size_t writes() const { return writes_; }
  std::vector<uint8_t> *raw(const char *ns, const char *key);  // for corruption tests

 private:
  std::map<std::string, std::vector<uint8_t>> values_;
  size_t writes_ = 0;
};

// A complete simulated device.
struct SimDevice {
  SimDevice(int width, int height) : display(width, height), buffer((size_t)width * 20) {}
  SimDisplay display;
  ScriptedTouch touch;
  SimPlatform platform;
  MemoryStore storage;
  std::vector<Color> buffer;
  Board board() { return Board{&display, &touch, &platform, &storage}; }
};

}  // namespace host
}  // namespace stella
