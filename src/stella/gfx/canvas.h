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
#include <stdint.h>

#include "../core/color.h"
#include "../core/geometry.h"
#include "font.h"

namespace stella {

enum class Align : uint8_t { Left, Center, Right };

class Canvas {
 public:
  Canvas() = default;
  Canvas(Color *buffer, Rect target) { begin(buffer, target); }

  void begin(Color *buffer, Rect target);
  const Rect &target() const { return target_; }
  const Rect &clip() const { return clip_; }
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
