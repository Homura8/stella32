#pragma once
// Drawing surface for one render chunk. The renderer points the canvas at a
// small pixel buffer that represents `target()` (part of the screen); all
// drawing uses screen coordinates and is clipped to target() ∩ clip().
//
// Draw code must be deterministic and must paint every pixel inside the clip
// (start with an opaque background): the same draw calls are replayed for
// every chunk, and only dirty areas are ever redrawn.
//
// Every fill takes an optional alpha (0..255). Translucent fills blend with the
// pixels already drawn, which is how "glass" surfaces sit on the sky gradient.
#include <math.h>
#include <stdint.h>

#include "../core/color.h"
#include "../core/geometry.h"
#include "font.h"

namespace stella {

enum class Align : uint8_t { Left, Center, Right };

// Coverage (0..1) of the pixel centred at (px, py) by a rounded rectangle in
// continuous coordinates [x0, x1) x [y0, y1) with corner radius r.
inline float roundRectCoverage(float px, float py, float x0, float y0, float x1, float y1, float r) {
  if (px < x0 - 1.f || px > x1 + 1.f || py < y0 - 1.f || py > y1 + 1.f) return 0.f;
  auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; };
  if (r <= 0.f) {
    const float covX = clampf(fminf(px + 0.5f - x0, x1 - (px - 0.5f)), 0.f, 1.f);
    const float covY = clampf(fminf(py + 0.5f - y0, y1 - (py - 0.5f)), 0.f, 1.f);
    return covX * covY;
  }
  const float cx = clampf(px, x0 + r, x1 - r), cy = clampf(py, y0 + r, y1 - r);
  const float dx = px - cx, dy = py - cy;
  return clampf(r + 0.5f - sqrtf(dx * dx + dy * dy), 0.f, 1.f);
}

class Canvas {
 public:
  Canvas() = default;
  Canvas(Color *buffer, Rect target) { begin(buffer, target); }

  void begin(Color *buffer, Rect target);
  const Rect &target() const { return target_; }
  const Rect &clip() const { return clip_; }
  // A view of the same pixels in which everything drawn lands (dx, dy) further
  // right and down. Screens draw in their own coordinates, so a screen can be
  // slid across the display (screen transitions) without knowing about it.
  // Same buffer, same clip as seen from outside.
  Canvas shifted(int dx, int dy) const {
    Canvas v;
    v.buffer_ = buffer_;
    v.target_ = target_.offset(-dx, -dy);
    v.clip_ = clip_.offset(-dx, -dy);
    return v;
  }
  // Narrow the clip; returns the previous clip for restoreClip().
  Rect pushClip(const Rect &r);
  void restoreClip(const Rect &previous) { clip_ = previous; }
  // True if anything inside `r` can be visible in this chunk (cheap culling).
  bool visible(const Rect &r) const { return clip_.intersects(r); }

  // Direct pixel access for generated backgrounds (x, y must be inside clip()).
  Color &at(int x, int y) { return buffer_[(y - target_.y) * target_.w + (x - target_.x)]; }

  void fill(Color c) { fillRect(clip_, c); }
  void fillRect(const Rect &r, Color c, uint8_t alpha = 255);
  void hline(int x, int y, int w, Color c, uint8_t alpha = 255) { fillRect(Rect(x, y, w, 1), c, alpha); }
  void vline(int x, int y, int h, Color c, uint8_t alpha = 255) { fillRect(Rect(x, y, 1, h), c, alpha); }
  void pixel(int x, int y, Color c, uint8_t alpha = 255);
  void darken(const Rect &r);  // 50% scrim

  // Anti-aliased primitives (coverage from the distance to the shape edge).
  void fillRoundRect(const Rect &r, int radius, Color c, uint8_t alpha = 255);
  void strokeRoundRect(const Rect &r, int radius, int thickness, Color c, uint8_t alpha = 255);
  void fillCircle(int cx, int cy, int radius, Color c, uint8_t alpha = 255);
  void strokeCircle(int cx, int cy, int radius, int thickness, Color c, uint8_t alpha = 255);
  // Arc from startDeg sweeping clockwise (0 = 12 o'clock), with round ends.
  void strokeArc(int cx, int cy, int radius, int thickness, float startDeg, float sweepDeg, Color c,
                 uint8_t alpha = 255);
  void line(float x0, float y0, float x1, float y1, float thickness, Color c, uint8_t alpha = 255);

  // Custom anti-aliased shape: `coverage(px, py)` returns 0..1 for the pixel
  // centre (px, py) and is evaluated for every visible pixel of `bounds`.
  template <class Coverage>
  void paint(const Rect &bounds, Color c, uint8_t alpha, Coverage coverage) {
    const Rect area = clip_.intersect(bounds);
    if (area.empty() || alpha == 0) return;
    for (int y = area.y; y < area.bottom(); ++y) {
      for (int x = area.x; x < area.right(); ++x) {
        const float cov = coverage(x + 0.5f, y + 0.5f);
        if (cov <= 0.f) continue;
        Color &p = at(x, y);
        p = blend(p, c, (uint8_t)((cov > 1.f ? 1.f : cov) * alpha + 0.5f));
      }
    }
  }

  // Text. `baseline` is the y of the font baseline. Returns the advance width.
  static int textWidth(const Font &f, const char *text);
  int drawText(const Font &f, const char *text, int x, int baseline, Color c, Align align = Align::Left);
  // One line vertically centred in `box`, shortened with "..." if it does not fit.
  void drawTextIn(const Font &f, const char *text, const Rect &box, Color c, Align align = Align::Left);
  // Word-wrapped paragraph from the top of `box`; returns the number of lines drawn.
  int drawWrapped(const Font &f, const char *text, const Rect &box, Color c, int maxLines = 0,
                  Align align = Align::Left);

 private:
  void drawGlyph(const Font &f, const Glyph &g, int penX, int baseline, Color c);

  Color *buffer_ = nullptr;
  Rect target_, clip_;
};

}  // namespace stella
