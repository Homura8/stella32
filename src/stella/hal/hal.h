#pragma once
// Hardware abstraction interfaces. A board port implements these once; the rest
// of Stella (and every app) only ever talks to these interfaces. Apps never
// receive HAL objects directly: they go through AppContext.
#include <stddef.h>
#include <stdint.h>

#include "../core/color.h"
#include "../core/geometry.h"
#include "network.h"

namespace stella {

// Pixel sink. writePixels() receives a contiguous, row-major block of
// area.w * area.h native-order RGB565 pixels and must finish reading it before
// returning (the renderer reuses the buffer immediately).
class Display {
 public:
  virtual ~Display() = default;
  virtual int16_t width() const = 0;
  virtual int16_t height() const = 0;
  virtual void writePixels(const Rect &area, const Color *pixels) = 0;
  virtual void setBacklight(uint8_t level) = 0;  // 0 = off, 255 = full

  // Optional asynchronous (DMA) transfer. writePixelsAsync() may return while
  // the transfer is still running, but must first wait for any earlier
  // transfer to finish (one in flight at a time) and may modify the buffer
  // (e.g. byte-swap in place). waitIdle() blocks until nothing is in flight.
  virtual bool supportsAsync() const { return false; }
  virtual void writePixelsAsync(const Rect &area, Color *pixels) { writePixels(area, pixels); }
  virtual void waitIdle() {}
};

// One touch-controller reading, already mapped to screen pixels by the board
// (see input/touch_calibration.h). Raw values are kept for calibration tools.
struct TouchSample {
  bool pressed = false;
  int16_t x = 0, y = 0;
  uint16_t pressure = 0;
  int16_t rawX = 0, rawY = 0;
};

class TouchInput {
 public:
  virtual ~TouchInput() = default;
  // Returns false only if the controller could not be read at all.
  virtual bool sample(TouchSample &out) = 0;
};

enum class ResetCause : uint8_t { Unknown, PowerOn, Software, Watchdog, Brownout, Panic, External };

struct MemoryStats {
  uint32_t freeHeap = 0, minimumFreeHeap = 0, largestFreeBlock = 0;
};

class Platform {
 public:
  virtual ~Platform() = default;
  virtual const char *boardName() const = 0;
  virtual uint32_t millis() const = 0;                     // monotonic, wraps at 2^32
  virtual bool wallClock(int64_t &epochSeconds) const = 0;  // false until time is known
  virtual MemoryStats memory() const = 0;
  virtual ResetCause resetCause() const = 0;
  virtual void restart() = 0;
  // A small block that survives a software restart (RTC memory on ESP32).
  // Contents are random after power-on; callers must validate them.
  virtual void *retainedMemory(size_t &size) = 0;
  // Lowest free stack ever seen on the calling thread, in bytes (0 if unknown).
  virtual uint32_t stackHeadroom() const { return 0; }
};

// Persistent key/value storage (NVS on ESP32). Namespaces and keys are limited
// to 15 characters by NVS; the record layer checks this.
class KeyValueStore {
 public:
  virtual ~KeyValueStore() = default;
  // Returns the stored size (which may exceed capacity: nothing is copied
  // then), or -1 if the key is missing or unreadable.
  virtual int read(const char *ns, const char *key, void *out, size_t capacity) = 0;
  virtual bool write(const char *ns, const char *key, const void *data, size_t size) = 0;
  virtual bool erase(const char *ns, const char *key) = 0;
};

// Everything a board port provides, gathered for Runtime::begin().
struct Board {
  Display *display = nullptr;
  TouchInput *touch = nullptr;     // optional
  Platform *platform = nullptr;
  KeyValueStore *storage = nullptr;  // optional; settings are then not persisted
  WifiDriver *wifi = nullptr;        // optional
  HttpClient *http = nullptr;        // optional (requires wifi)
};

}  // namespace stella
