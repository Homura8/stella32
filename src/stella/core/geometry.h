#pragma once
// Integer screen geometry. All coordinates are physical pixels with the origin
// at the top-left of the display after rotation.
#include <stdint.h>

namespace stella {

struct Point {
  int16_t x = 0, y = 0;
};

struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;

  constexpr Rect() = default;
  constexpr Rect(int x_, int y_, int w_, int h_)
      : x((int16_t)x_), y((int16_t)y_), w((int16_t)(w_ < 0 ? 0 : w_)), h((int16_t)(h_ < 0 ? 0 : h_)) {}

  constexpr int right() const { return x + w; }   // exclusive
  constexpr int bottom() const { return y + h; }  // exclusive
  constexpr bool empty() const { return w <= 0 || h <= 0; }
  constexpr int area() const { return empty() ? 0 : w * h; }
  constexpr bool contains(int px, int py) const { return px >= x && px < right() && py >= y && py < bottom(); }
  constexpr bool contains(Point p) const { return contains(p.x, p.y); }
  constexpr bool contains(const Rect &r) const {
    return !r.empty() && r.x >= x && r.y >= y && r.right() <= right() && r.bottom() <= bottom();
  }
  constexpr bool intersects(const Rect &r) const {
    return !empty() && !r.empty() && r.x < right() && x < r.right() && r.y < bottom() && y < r.bottom();
  }
  constexpr Rect intersect(const Rect &r) const {
    const int l = x > r.x ? x : r.x, t = y > r.y ? y : r.y;
    const int rr = right() < r.right() ? right() : r.right(), b = bottom() < r.bottom() ? bottom() : r.bottom();
    return (rr <= l || b <= t) ? Rect() : Rect(l, t, rr - l, b - t);
  }
  // Smallest rectangle containing both. An empty operand is ignored.
  constexpr Rect unite(const Rect &r) const {
    if (empty()) return r;
    if (r.empty()) return *this;
    const int l = x < r.x ? x : r.x, t = y < r.y ? y : r.y;
    const int rr = right() > r.right() ? right() : r.right(), b = bottom() > r.bottom() ? bottom() : r.bottom();
    return Rect(l, t, rr - l, b - t);
  }
  constexpr Rect inset(int dx, int dy) const { return Rect(x + dx, y + dy, w - 2 * dx, h - 2 * dy); }
  constexpr Rect inset(int d) const { return inset(d, d); }
  constexpr Rect offset(int dx, int dy) const { return Rect(x + dx, y + dy, w, h); }
  constexpr int centerX() const { return x + w / 2; }
  constexpr int centerY() const { return y + h / 2; }

  // Layout helpers: carve a strip off one edge.
  constexpr Rect top(int height) const { return Rect(x, y, w, height < h ? height : h); }
  constexpr Rect bottomStrip(int height) const { return Rect(x, bottom() - (height < h ? height : h), w, height < h ? height : h); }
  constexpr Rect left(int width) const { return Rect(x, y, width < w ? width : w, h); }
  constexpr Rect rightStrip(int width) const { return Rect(right() - (width < w ? width : w), y, width < w ? width : w, h); }

  constexpr bool operator==(const Rect &o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
  constexpr bool operator!=(const Rect &o) const { return !(*this == o); }
};

// Split `area` into `count` equal cells of a `columns`-wide grid with `gap`.
constexpr Rect gridCell(const Rect &area, int columns, int rows, int index, int gap) {
  const int cw = (area.w - gap * (columns - 1)) / columns;
  const int ch = (area.h - gap * (rows - 1)) / rows;
  const int col = index % columns, row = index / columns;
  return Rect(area.x + col * (cw + gap), area.y + row * (ch + gap), cw, ch);
}

}  // namespace stella
