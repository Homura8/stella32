#pragma once
// Dirty-rectangle renderer for displays without a full framebuffer.
//
// Classic ESP32 boards without PSRAM cannot hold a 320x240x16 framebuffer next
// to Wi-Fi/TLS, so Stella renders through one small chunk buffer (12.5 KiB on
// the CYD). Callers mark what changed with invalidate(); render() then replays
// the draw function once per chunk of each dirty rectangle and streams the
// chunk to the display. Nothing outside the dirty region is redrawn or sent.
//
// Invariant (tested): rendering any sequence of dirty regions produces exactly
// the same screen as redrawing everything.
#include <stddef.h>
#include <stdint.h>

#include "../core/geometry.h"
#include "../hal/hal.h"
#include "canvas.h"

namespace stella {

class DirtyRegion {
 public:
  static constexpr int kMaxRects = 8;

  void setBounds(const Rect &screen) { bounds_ = screen; }
  // Adds a rectangle, clipped to the screen. Overlapping or nearby rectangles
  // are merged; when the list is full the two cheapest to merge are combined.
  void add(const Rect &r);
  void addAll() { add(bounds_); }
  void clear() { count_ = 0; }
  bool empty() const { return count_ == 0; }
  int count() const { return count_; }
  const Rect &operator[](int i) const { return rects_[i]; }
  Rect bounds() const;

 private:
  void insertMerged(Rect r);
  Rect bounds_{0, 0, 320, 240};
  Rect rects_[kMaxRects];
  int count_ = 0;
};

class Renderer {
 public:
  using DrawFn = void (*)(Canvas &canvas, void *context);

  struct Stats {
    uint32_t chunks = 0;  // draw-function replays
    uint32_t pixels = 0;  // pixels sent to the display
  };

  Renderer(Color *buffer, size_t capacityPixels) : buffer_(buffer), capacity_(capacityPixels) {}

  // Renders and clears `region`. The chunk height adapts to each rectangle's
  // width, so narrow updates (a clock digit, a toggle) need a single pass.
  Stats render(Display &display, DirtyRegion &region, DrawFn draw, void *context);

 private:
  Color *buffer_;
  size_t capacity_;
};

}  // namespace stella
