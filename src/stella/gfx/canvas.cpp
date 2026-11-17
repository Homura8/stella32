#include "canvas.h"

#include <math.h>
#include <string.h>

#include "../core/text.h"

namespace stella {
namespace {

inline float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

}  // namespace

void Canvas::begin(Color *buffer, Rect target) {
  buffer_ = buffer;
  target_ = target;
  clip_ = target;
}

Rect Canvas::pushClip(const Rect &r) {
  const Rect previous = clip_;
  clip_ = clip_.intersect(r);
  return previous;
}

void Canvas::fillRect(const Rect &r, Color c, uint8_t alpha) {
  const Rect area = clip_.intersect(r);
  if (area.empty() || alpha == 0) return;
  for (int y = area.y; y < area.bottom(); ++y) {
    Color *row = &at(area.x, y);
    if (alpha == 255) {
      for (int i = 0; i < area.w; ++i) row[i] = c;
    } else {
      for (int i = 0; i < area.w; ++i) row[i] = blend(row[i], c, alpha);
    }
  }
}

void Canvas::pixel(int x, int y, Color c, uint8_t alpha) {
  if (!clip_.contains(x, y)) return;
  Color &p = at(x, y);
  p = blend(p, c, alpha);
}

void Canvas::darken(const Rect &r) {
  const Rect area = clip_.intersect(r);
  for (int y = area.y; y < area.bottom(); ++y)
    for (int x = area.x; x < area.right(); ++x) at(x, y) = darken50(at(x, y));
}

void Canvas::fillRoundRect(const Rect &r, int radius, Color c, uint8_t alpha) {
  if (r.empty()) return;
  const float rad = fminf((float)radius, fminf(r.w, r.h) / 2.f);
  const float x0 = r.x, y0 = r.y, x1 = r.right(), y1 = r.bottom();
  // Only the four corner squares need anti-aliasing maths; everything else
  // is fully covered (edges are integer-aligned), so it is a plain fill.
  const int ri = (int)ceilf(rad);
  const int topH = ri < r.h ? ri : r.h;
  const int bottomY = r.bottom() - ri > r.y + topH ? r.bottom() - ri : r.y + topH;
  const int cornerW = 2 * ri < r.w ? ri : r.w / 2;
  fillRect(Rect(r.x, r.y + topH, r.w, bottomY - (r.y + topH)), c, alpha);
  fillRect(Rect(r.x + cornerW, r.y, r.w - 2 * cornerW, topH), c, alpha);
  fillRect(Rect(r.x + cornerW, bottomY, r.w - 2 * cornerW, r.bottom() - bottomY), c, alpha);
  auto cov = [=](float px, float py) { return roundRectCoverage(px, py, x0, y0, x1, y1, rad); };
  paint(Rect(r.x, r.y, cornerW, topH), c, alpha, cov);
  paint(Rect(r.right() - cornerW, r.y, cornerW, topH), c, alpha, cov);
  paint(Rect(r.x, bottomY, cornerW, r.bottom() - bottomY), c, alpha, cov);
  paint(Rect(r.right() - cornerW, bottomY, cornerW, r.bottom() - bottomY), c, alpha, cov);
}

void Canvas::strokeRoundRect(const Rect &r, int radius, int thickness, Color c, uint8_t alpha) {
  if (r.empty() || thickness <= 0) return;
  const float rad = fminf((float)radius, fminf(r.w, r.h) / 2.f);
  const float t = (float)thickness;
  const float x0 = r.x, y0 = r.y, x1 = r.right(), y1 = r.bottom();
  const float inner = fmaxf(rad - t, 0.f);
  auto cov = [=](float px, float py) {
    const float outer = roundRectCoverage(px, py, x0, y0, x1, y1, rad);
    return outer <= 0.f ? 0.f
                        : fmaxf(outer - roundRectCoverage(px, py, x0 + t, y0 + t, x1 - t, y1 - t, inner), 0.f);
  };
  // Corner squares get exact coverage; the straight runs between them are
  // solid lines `thickness` wide.
  const int band = (int)ceilf(fmaxf(rad, t)) + 1;
  const int bh = 2 * band < r.h ? band : r.h / 2, bw = 2 * band < r.w ? band : r.w / 2;
  paint(Rect(r.x, r.y, bw, bh), c, alpha, cov);
  paint(Rect(r.right() - bw, r.y, bw, bh), c, alpha, cov);
  paint(Rect(r.x, r.bottom() - bh, bw, bh), c, alpha, cov);
  paint(Rect(r.right() - bw, r.bottom() - bh, bw, bh), c, alpha, cov);
  fillRect(Rect(r.x + bw, r.y, r.w - 2 * bw, thickness), c, alpha);
  fillRect(Rect(r.x + bw, r.bottom() - thickness, r.w - 2 * bw, thickness), c, alpha);
  fillRect(Rect(r.x, r.y + bh, thickness, r.h - 2 * bh), c, alpha);
  fillRect(Rect(r.right() - thickness, r.y + bh, thickness, r.h - 2 * bh), c, alpha);
}

void Canvas::fillCircle(int cx, int cy, int radius, Color c, uint8_t alpha) {
  const float fx = cx + 0.5f, fy = cy + 0.5f, r = (float)radius;
  paint(Rect(cx - radius - 1, cy - radius - 1, 2 * radius + 3, 2 * radius + 3), c, alpha, [=](float px, float py) {
    const float dx = px - fx, dy = py - fy;
    return clamp01(r + 0.5f - sqrtf(dx * dx + dy * dy));
  });
}

void Canvas::strokeCircle(int cx, int cy, int radius, int thickness, Color c, uint8_t alpha) {
  strokeArc(cx, cy, radius, thickness, 0.f, 360.f, c, alpha);
}

void Canvas::strokeArc(int cx, int cy, int radius, int thickness, float startDeg, float sweepDeg, Color c,
                       uint8_t alpha) {
  if (sweepDeg <= 0.f || thickness <= 0) return;
  const float fx = cx + 0.5f, fy = cy + 0.5f;
  // The stroke is centred on `radius - thickness/2` so the outer edge is `radius`.
  const float mid = radius - thickness / 2.f, half = thickness / 2.f;
  const bool full = sweepDeg >= 360.f;
  const float kRad = 3.14159265f / 180.f;
  const float a0 = startDeg * kRad, a1 = (startDeg + sweepDeg) * kRad;
  // End-cap centres (round caps).
  const float e0x = fx + mid * sinf(a0), e0y = fy - mid * cosf(a0);
  const float e1x = fx + mid * sinf(a1), e1y = fy - mid * cosf(a1);
  paint(Rect(cx - radius - 1, cy - radius - 1, 2 * radius + 3, 2 * radius + 3), c, alpha, [=](float px, float py) {
    const float dx = px - fx, dy = py - fy;
    const float d = sqrtf(dx * dx + dy * dy);
    const float ring = clamp01(half + 0.5f - fabsf(d - mid));
    if (full) return ring;
    float angle = atan2f(dx, -dy);  // clockwise from 12 o'clock
    float rel = angle - a0;
    while (rel < 0.f) rel += 2.f * 3.14159265f;
    while (rel >= 2.f * 3.14159265f) rel -= 2.f * 3.14159265f;
    if (rel <= a1 - a0) return ring;
    const float c0 = clamp01(half + 0.5f - sqrtf((px - e0x) * (px - e0x) + (py - e0y) * (py - e0y)));
    const float c1 = clamp01(half + 0.5f - sqrtf((px - e1x) * (px - e1x) + (py - e1y) * (py - e1y)));
    return fmaxf(c0, c1);
  });
}

void Canvas::line(float x0, float y0, float x1, float y1, float thickness, Color c, uint8_t alpha) {
  const float half = thickness / 2.f;
  const float vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
  const int left = (int)floorf(fminf(x0, x1) - half - 1), top = (int)floorf(fminf(y0, y1) - half - 1);
  const int right = (int)ceilf(fmaxf(x0, x1) + half + 1), bottom = (int)ceilf(fmaxf(y0, y1) + half + 1);
  paint(Rect(left, top, right - left, bottom - top), c, alpha, [=](float px, float py) {
    float t = len2 > 0.f ? ((px - x0) * vx + (py - y0) * vy) / len2 : 0.f;
    t = clampf(t, 0.f, 1.f);
    const float dx = px - (x0 + t * vx), dy = py - (y0 + t * vy);
    return clamp01(half + 0.5f - sqrtf(dx * dx + dy * dy));
  });
}

int Canvas::textWidth(const Font &f, const char *text) {
  if (!text) return 0;
  int w = 0;
  while (*text) w += f.glyphs[f.find(nextCodepoint(text))].advance;
  return w;
}

void Canvas::drawGlyph(const Font &f, const Glyph &g, int penX, int baseline, Color c) {
  const Rect box(penX + g.left, baseline - g.top, g.width, g.height);
  const Rect area = clip_.intersect(box);
  if (area.empty()) return;
  const uint8_t *bits = f.bitmap + g.offset;
  for (int y = area.y; y < area.bottom(); ++y) {
    const int row = y - box.y;
    for (int x = area.x; x < area.right(); ++x) {
      const int index = row * g.width + (x - box.x);
      const uint8_t v = bits[index >> 1];
      const uint8_t a = (index & 1) ? (v & 0x0F) : (v >> 4);
      if (!a) continue;
      Color &p = at(x, y);
      p = blend(p, c, (uint8_t)(a * 17));
    }
  }
}

int Canvas::drawText(const Font &f, const char *text, int x, int baseline, Color c, Align align) {
  if (!text) return 0;
  const int width = textWidth(f, text);
  if (align == Align::Center) x -= width / 2;
  else if (align == Align::Right) x -= width;
  // Cull whole lines outside the clip.
  if (!clip_.intersects(Rect(x - 4, baseline - f.ascent, width + 8, f.ascent + f.descent))) return width;
  while (*text) {
    const Glyph &g = f.glyphs[f.find(nextCodepoint(text))];
    drawGlyph(f, g, x, baseline, c);
    x += g.advance;
  }
  return width;
}

void Canvas::drawTextIn(const Font &f, const char *text, const Rect &box, Color c, Align align) {
  if (!text || !visible(box)) return;
  char fitted[128];
  copyText(fitted, sizeof(fitted), text);
  if (textWidth(f, fitted) > box.w) {
    const int dots = textWidth(f, "...");
    size_t n = strlen(fitted);
    while (n > 0) {
      do { --n; } while (n > 0 && ((unsigned char)fitted[n] & 0xC0) == 0x80);
      fitted[n] = '\0';
      if (textWidth(f, fitted) + dots <= box.w) break;
    }
    // Trim a dangling space before the ellipsis.
    while (n > 0 && fitted[n - 1] == ' ') fitted[--n] = '\0';
    strncat(fitted, "...", sizeof(fitted) - strlen(fitted) - 1);
  }
  const int baseline = box.y + (box.h + f.ascent - f.descent) / 2;
  const int x = align == Align::Left ? box.x : align == Align::Center ? box.centerX() : box.right();
  const Rect previous = pushClip(box.inset(-2, -f.descent));
  drawText(f, fitted, x, baseline, c, align);
  restoreClip(previous);
}

int Canvas::drawWrapped(const Font &f, const char *text, const Rect &box, Color c, int maxLines, Align align) {
  if (!text) return 0;
  const int capacity = f.lineHeight > 0 ? box.h / f.lineHeight : 0;
  if (maxLines <= 0 || maxLines > capacity) maxLines = capacity;
  int lines = 0;
  const char *p = text;
  char line[128];
  while (*p && lines < maxLines) {
    while (*p == ' ') ++p;
    // Greedy: extend word by word while the line still fits.
    size_t take = 0, lastFit = 0;
    for (;;) {
      size_t end = take;
      while (p[end] == ' ') ++end;
      while (p[end] && p[end] != ' ' && p[end] != '\n') ++end;
      if (end == take || end >= sizeof(line)) break;
      memcpy(line, p, end);
      line[end] = '\0';
      if (textWidth(f, line) > box.w && lastFit > 0) break;
      lastFit = end;
      take = end;
      if (p[end] == '\n' || !p[end]) break;
    }
    if (lastFit == 0) {  // A single word wider than the box: let drawTextIn shorten it.
      while (p[lastFit] && p[lastFit] != ' ' && p[lastFit] != '\n' && lastFit < sizeof(line) - 1) ++lastFit;
    }
    memcpy(line, p, lastFit);
    line[lastFit] = '\0';
    p += lastFit;
    while (*p == ' ') ++p;
    if (*p == '\n') ++p;
    const bool more = *p != '\0';
    const Rect row(box.x, box.y + lines * f.lineHeight, box.w, f.lineHeight);
    if (lines == maxLines - 1 && more) {
      // Last permitted line: append the remainder so drawTextIn ellipsizes it.
      char rest[128];
      copyText(rest, sizeof(rest), line);
      strncat(rest, " ", sizeof(rest) - strlen(rest) - 1);
      strncat(rest, p, sizeof(rest) - strlen(rest) - 1);
      drawTextIn(f, rest, row, c, align);
    } else {
      drawTextIn(f, line, row, c, align);
    }
    ++lines;
  }
  return lines;
}

}  // namespace stella
