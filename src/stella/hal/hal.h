#pragma once
// Hardware abstraction interfaces. A board port implements these once; the rest
// of Stella (and every app) only ever talks to these interfaces. Apps never
// receive HAL objects directly: they go through AppContext.
#include <stddef.h>
#include <stdint.h>

#include "../core/color.h"
#include "../core/geometry.h"
#include "hid.h"
#include "network.h"
#include "update.h"

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
  // Optional LCD adjustments for on-device calibration (Settings > Display >
  // Calibrate); the defaults leave the panel as its driver set it up.
  //  * vcom: common-voltage offset in panel steps. A mis-set VCOM makes LCDs
  //    flicker, often more on one side; adjust while watching a checkerboard.
  //  * refreshHz: frame rate (0 = driver default); the nearest supported
  //    rate is used.
  struct PanelTrim {
    int8_t vcom = 0;
    uint8_t refreshHz = 0;
  };
  virtual bool supportsPanelTrim() const { return false; }
  virtual void setPanelTrim(const PanelTrim &trim) { (void)trim; }

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
  // After a firmware update the new image runs "pending": unless it is
  // confirmed, the next reset returns to the previous image (ESP32: the
  // bootloader's rollback). The runtime confirms once the system has run
  // healthily for a while.
  virtual bool updatePending() const { return false; }
  virtual void confirmUpdate() {}
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

// Optional tone output (a piezo or a small speaker). Non-blocking: tone(hz)
// starts or changes a tone and returns at once; tone(0) is silence. The
// runtime sequences alert chimes on the UI thread.
class Sound {
 public:
  virtual ~Sound() = default;
  virtual void tone(uint16_t hz) = 0;
  // Loudness for every later tone, 0..100 percent (0 = silent). Boards map it
  // onto what their output can do; the default ignores it.
  virtual void setVolume(uint8_t percent) { (void)percent; }
};

// An RGB status LED. Boards sharing a name are wired differently (which pin is
// which colour, active-low or active-high), so the wiring is data: the board
// offers pins that are safe to probe, Settings can find which is which by
// asking the person what lit up, and the result is a setting.
constexpr uint8_t kNoPin = 0xFF;
struct LedWiring {
  uint8_t pin[3] = {kNoPin, kNoPin, kNoPin};  // red, green, blue
  bool inverted = true;                        // the LED lights when its pin is low
  bool usable() const { return pin[0] != kNoPin || pin[1] != kNoPin || pin[2] != kNoPin; }
};

class Led {
 public:
  virtual ~Led() = default;
  // Takes over the pins (releasing earlier ones). False if a pin is unusable.
  virtual bool configure(const LedWiring &wiring) = 0;
  // Each channel 0..255 (linear), as configured. Unwired channels are ignored.
  virtual void set(uint8_t r, uint8_t g, uint8_t b) = 0;
  // Pins that may safely be driven while looking for the LED (never the
  // display, touch, speaker or storage pins). Returns how many.
  virtual int candidatePins(uint8_t *out, int capacity) const = 0;
  // Drives `pin` low (activeLow) or high and releases every other candidate to
  // high impedance, where no LED can light; pin = kNoPin releases them all.
  // Only for the "Find my LED" test: low lights an active-low LED, high an
  // active-high one, and nothing else lights, so what the person sees is
  // that one pin.
  virtual void probe(uint8_t pin, bool activeLow) = 0;
};

// An ambient light sensor (an LDR on an ADC pin). Its raw value has no unit
// and either polarity: the calibration (Settings) works that out.
class LightSensor {
 public:
  virtual ~LightSensor() = default;
  virtual uint16_t read() = 0;  // 0..4095, a few samples averaged
};

// Flash set aside for one picture (the wallpaper): a few tens of KB that need
// not be mapped, only written once in order and read back in pieces.
class WallpaperStore {
 public:
  virtual ~WallpaperStore() = default;
  virtual size_t capacity() = 0;  // bytes it can hold; 0 = this board cannot
  // Sequential from offset 0: the store erases as the offset advances.
  virtual bool write(size_t offset, const uint8_t *data, size_t size) = 0;
  virtual bool read(size_t offset, uint8_t *out, size_t size) = 0;
  virtual bool erase() = 0;  // forget the picture (at least its first sector)
};

// Everything a board port provides, gathered for Runtime::begin().
struct Board {
  Display *display = nullptr;
  TouchInput *touch = nullptr;     // optional
  Platform *platform = nullptr;
  KeyValueStore *storage = nullptr;  // optional; settings are then not persisted
  WifiDriver *wifi = nullptr;        // optional
  HttpClient *http = nullptr;        // optional (requires wifi)
  Sound *sound = nullptr;            // optional; alerts are silent without it
  SetupServer *setup = nullptr;      // optional; phone-based Wi-Fi setup
  FirmwareSlot *firmware = nullptr;  // optional; over-the-air updates
  Led *led = nullptr;                // optional; an RGB status LED
  LightSensor *light = nullptr;      // optional; ambient light
  RemoteServer *remote = nullptr;    // optional; the web page on the home network
  WallpaperStore *wallpaper = nullptr;  // optional; a picture behind the glass
  HidDevice *hid = nullptr;          // optional; Bluetooth keyboard and media keys (the Deck app)
};

}  // namespace stella
