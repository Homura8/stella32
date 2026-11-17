#pragma once
// Declarative layout: describe a screen as rows/columns of fixed and flexible
// lengths, resolved against whatever content rectangle the display provides.
//
//   Rect r[3];
//   layout::column(content, {px(t.m.statusBar), flex(), px(t.m.control)}, t.m.gapM, r);
//
// Screens compute all their rectangles in Screen::layout() from the theme and
// the content rect; draw() and event handling only read those rectangles. To
// change a layout, edit that one function. Never hard-code screen coordinates.
#include <stddef.h>
#include <stdint.h>

#include "../core/geometry.h"

namespace stella {
namespace layout {

struct Length {
  int16_t value;
  bool flex;  // value is a weight when true, pixels otherwise
};
constexpr Length px(int pixels) { return Length{(int16_t)(pixels < 0 ? 0 : pixels), false}; }
constexpr Length flex(int weight = 1) { return Length{(int16_t)(weight < 1 ? 1 : weight), true}; }

enum class Axis : uint8_t { Horizontal, Vertical };

// Divide `area` along `axis`: fixed lengths first, then the remaining space is
// shared between flex items by weight (the last flex item absorbs rounding).
// Fixed lengths that do not fit are shrunk from the end; nothing overflows.
template <size_t N>
void split(const Rect &area, Axis axis, const Length (&lengths)[N], int gap, Rect (&out)[N]) {
  const int total = axis == Axis::Horizontal ? area.w : area.h;
  int fixed = gap * (int)(N - 1), weights = 0;
  for (const Length &l : lengths) {
    if (l.flex) weights += l.value;
    else fixed += l.value;
  }
  int spare = total - fixed;
  if (spare < 0) spare = 0;
  int pos = axis == Axis::Horizontal ? area.x : area.y;
  const int end = pos + total;
  int lastFlex = -1;
  for (size_t i = 0; i < N; ++i)
    if (lengths[i].flex) lastFlex = (int)i;
  int flexUsed = 0;
  for (size_t i = 0; i < N; ++i) {
    int size;
    if (lengths[i].flex) {
      size = (int)i == lastFlex ? spare - flexUsed : spare * lengths[i].value / weights;
      flexUsed += size;
    } else {
      size = lengths[i].value;
    }
    if (pos + size > end) size = end - pos > 0 ? end - pos : 0;
    out[i] = axis == Axis::Horizontal ? Rect(pos, area.y, size, area.h) : Rect(area.x, pos, area.w, size);
    pos += size + gap;
    if (pos > end) pos = end;
  }
}

template <size_t N>
void row(const Rect &area, const Length (&lengths)[N], int gap, Rect (&out)[N]) {
  split(area, Axis::Horizontal, lengths, gap, out);
}
template <size_t N>
void column(const Rect &area, const Length (&lengths)[N], int gap, Rect (&out)[N]) {
  split(area, Axis::Vertical, lengths, gap, out);
}

// Cell `index` of a columns x rows grid, row-major.
constexpr Rect grid(const Rect &area, int columns, int rows, int index, int gap) {
  return gridCell(area, columns, rows, index, gap);
}

// Box of size w x h centred in `area` (clamped to it).
constexpr Rect center(const Rect &area, int w, int h) {
  return Rect(area.x + (area.w - (w < area.w ? w : area.w)) / 2, area.y + (area.h - (h < area.h ? h : area.h)) / 2,
              w < area.w ? w : area.w, h < area.h ? h : area.h);
}

// Square of side `size` at the left/right edge of `area`, vertically centred.
constexpr Rect leading(const Rect &area, int size) { return Rect(area.x, area.y + (area.h - size) / 2, size, size); }
constexpr Rect trailing(const Rect &area, int size) {
  return Rect(area.right() - size, area.y + (area.h - size) / 2, size, size);
}

}  // namespace layout
}  // namespace stella
