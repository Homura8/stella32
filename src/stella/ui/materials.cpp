#include "materials.h"

namespace stella {
namespace {

constexpr uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// Channels in 8.8 fixed point (0..255 << 8).
struct Rgb {
  int32_t r, g, b;
};
Rgb toRgb(Color c) { return Rgb{red8(c) << 8, green8(c) << 8, blue8(c) << 8}; }

// Soft elliptical wash: weight = strength * f^2 where f = 1 - (dx/rx)^2 - (dy/ry)^2.
struct Wash {
  float cx, cy, invRx2, invRy2, strength;
  Rgb color;
};

Wash makeWash(const Rect &s, float fx, float fy, float frx, float fry, float strength, Color c) {
  const float rx = frx * s.w, ry = fry * s.h;
  return Wash{s.x + fx * s.w, s.y + fy * s.h, 1.f / (rx * rx), 1.f / (ry * ry), strength, toRgb(c)};
}

// Ordered dither from 8.8 fixed point to 5/6-bit channels using shifts only:
// floor((v + d/16 * step) / step), the same quantisation as rgb() (v >> 3,
// v >> 2) plus a 4x4 Bayer threshold, so gradients never band.
inline uint32_t dither5(int32_t v88, uint32_t d) {
  const uint32_t q = ((uint32_t)(v88 >> 7) + d) >> 4;
  return q > 31 ? 31 : q;
}
inline uint32_t dither6(int32_t v88, uint32_t d) {
  const uint32_t q = ((uint32_t)(v88 >> 6) + d) >> 4;
  return q > 63 ? 63 : q;
}

}  // namespace

void paintSky(Canvas &c, const Theme &t, const Rect &screen) {
  const Rect area = c.clip().intersect(screen);
  if (area.empty()) return;
  const Palette &p = t.color;
  const bool dither = t.look.dither;
  const Rgb s0 = toRgb(p.sky[0]), s1 = toRgb(p.sky[1]), s2 = toRgb(p.sky[2]);
  const float orb = p.orbAlpha / 255.f;
  // Positions follow the site's .pastel-bg layers (pink top-left, tint
  // top-right, gold low centre), stretched to the panel's aspect ratio.
  const Wash washes[3] = {
      makeWash(screen, 0.15f, 0.20f, 0.80f, 0.95f, 0.55f * orb, p.orbPink),
      makeWash(screen, 0.88f, 0.12f, 0.75f, 0.85f, 0.50f * orb, p.skyTint),
      makeWash(screen, 0.48f, 0.92f, 0.60f, 0.70f, 0.45f * orb, p.orbGold),
  };
  // CSS linear-gradient(120deg): direction (sin 120°, -cos 120°); position in
  // 16.16 fixed point along the gradient line, advancing linearly in x.
  const float dirX = 0.8660254f, dirY = 0.5f;
  const float length = screen.w * dirX + screen.h * dirY;
  const float cx = screen.x + screen.w / 2.f, cy = screen.y + screen.h / 2.f;
  const int32_t step = (int32_t)(dirX / length * 65536.f);

  // The sky is smooth, so it is evaluated exactly every kSpan pixels on a grid
  // anchored to the screen and interpolated in between (then dithered per
  // pixel). Samples depend only on (x, y), never on where a render chunk
  // starts, so partial redraws match full redraws exactly.
  constexpr int kSpan = 8;
  auto sample = [&](int x, const float *rowTerm, int32_t tgRow) {
    const int32_t tg = tgRow + (x - screen.x) * step;
    const int32_t g = tg < 0 ? 0 : tg > 65536 ? 65536 : tg;
    Rgb col;
    const Rgb &a = g < 32768 ? s0 : s1, &b = g < 32768 ? s1 : s2;
    const int32_t h = g < 32768 ? g : g - 32768;
    col.r = a.r + (((b.r - a.r) * h) >> 15);
    col.g = a.g + (((b.g - a.g) * h) >> 15);
    col.b = a.b + (((b.b - a.b) * h) >> 15);
    const float px = x + 0.5f;
    for (int k = 0; k < 3; ++k) {
      if (rowTerm[k] <= 0.f) continue;  // the whole row is outside this wash
      const float dx = px - washes[k].cx;
      const float fk = rowTerm[k] - dx * dx * washes[k].invRx2;
      if (fk <= 0.f) continue;
      const float wgt = fk * fk * washes[k].strength;
      const int32_t w = wgt >= 1.f ? 256 : (int32_t)(wgt * 256.f);
      col.r += ((washes[k].color.r - col.r) * w) >> 8;
      col.g += ((washes[k].color.g - col.g) * w) >> 8;
      col.b += ((washes[k].color.b - col.b) * w) >> 8;
    }
    return col;
  };
  for (int y = area.y; y < area.bottom(); ++y) {
    const float py = y + 0.5f;
    // Gradient position anchored at the screen's left edge, then linear in x.
    const int32_t tgRow = (int32_t)((((screen.x + 0.5f - cx) * dirX + (py - cy) * dirY) / length + 0.5f) * 65536.f);
    float rowTerm[3];
    for (int k = 0; k < 3; ++k) {
      const float dy = py - washes[k].cy;
      rowTerm[k] = 1.f - dy * dy * washes[k].invRy2;
    }
    const uint8_t *bayer = kBayer4[y & 3];
    Color *out = &c.at(area.x, y);
    int x = area.x;
    int x0 = screen.x + ((x - screen.x) / kSpan) * kSpan;
    Rgb right = sample(x0, rowTerm, tgRow);
    for (; x < area.right(); x0 += kSpan) {
      const Rgb left = right;  // each span's right sample is the next span's left
      right = sample(x0 + kSpan, rowTerm, tgRow);
      const int end = x0 + kSpan < area.right() ? x0 + kSpan : area.right();
      for (; x < end; ++x) {
        const int32_t f = x - x0;  // 0..kSpan-1
        const int32_t r = left.r + (((right.r - left.r) * f) >> 3);
        const int32_t g = left.g + (((right.g - left.g) * f) >> 3);
        const int32_t b = left.b + (((right.b - left.b) * f) >> 3);
        const uint32_t th = dither ? bayer[x & 3] : 8u;  // 8 = plain rounding
        *out++ = (Color)(dither5(r, th) << 11 | dither6(g, th) << 5 | dither5(b, th));
      }
    }
  }
}

int glassOutset(const Theme &t) { return t.dp(4); }

namespace {

// Paints `shadow` (rounded) only where it lies outside `card` (rounded). The
// rectangles are integer-aligned, so straight edges are fully covered or not
// at all: only the corner regions need per-pixel coverage.
void shadowOutside(Canvas &c, const Rect &card, int cardRadius, const Rect &shadow, int shadowRadius, Color color,
                   uint8_t alpha) {
  const float cr = fminf((float)cardRadius, fminf(card.w, card.h) / 2.f);
  const float sr = fminf((float)shadowRadius, fminf(shadow.w, shadow.h) / 2.f);
  const float cx0 = card.x, cy0 = card.y, cx1 = card.right(), cy1 = card.bottom();
  const float sx0 = shadow.x, sy0 = shadow.y, sx1 = shadow.right(), sy1 = shadow.bottom();
  auto coverage = [=](float px, float py) {
    const float s = roundRectCoverage(px, py, sx0, sy0, sx1, sy1, sr);
    return s <= 0.f ? 0.f : s * (1.f - roundRectCoverage(px, py, cx0, cy0, cx1, cy1, cr));
  };
  const Rect all = shadow.unite(card);
  const int band = (int)ceilf(fmaxf(cr, sr)) + 2;
  // Corner regions: exact coverage.
  const int left = all.x + band, right = all.right() - band, top = all.y + band, bottom = all.bottom() - band;
  c.paint(Rect(all.x, all.y, band, band), color, alpha, coverage);
  c.paint(Rect(right, all.y, band, band), color, alpha, coverage);
  c.paint(Rect(all.x, bottom, band, band), color, alpha, coverage);
  c.paint(Rect(right, bottom, band, band), color, alpha, coverage);
  // Straight strips where the shadow extends past the card's edges.
  if (shadow.y < card.y) c.fillRect(Rect(left, shadow.y, right - left, card.y - shadow.y), color, alpha);
  if (shadow.bottom() > card.bottom())
    c.fillRect(Rect(left, card.bottom(), right - left, shadow.bottom() - card.bottom()), color, alpha);
  if (shadow.x < card.x) c.fillRect(Rect(shadow.x, top, card.x - shadow.x, bottom - top), color, alpha);
  if (shadow.right() > card.right())
    c.fillRect(Rect(card.right(), top, shadow.right() - card.right(), bottom - top), color, alpha);
}

}  // namespace

void paintGlass(Canvas &c, const Theme &t, const Rect &r, int radius, Elevation e) {
  if (!c.visible(r.inset(-glassOutset(t)))) return;
  const Palette &p = t.color;
  if (e == Elevation::Raised) {
    // Two offset layers approximate the site's soft 0 12px 40px shadow. Like
    // CSS box-shadow, a shadow is only visible outside the card: the glass
    // shows the sky through it, not its own shadow. That also skips most of
    // the area, which is the expensive part.
    shadowOutside(c, r, radius, r.offset(0, t.dp(3)).inset(-t.dp(1), 0), radius + t.dp(1), p.shadow,
                  p.shadowAlpha / 2);
    shadowOutside(c, r, radius, r.offset(0, t.dp(1)), radius, p.shadow, p.shadowAlpha / 2);
  }
  c.fillRoundRect(r, radius, p.glass, p.glassAlpha);
  c.strokeRoundRect(r, radius, 1, p.rim, p.rimAlpha);
}

void paintHairline(Canvas &c, const Theme &t, int x, int y, int w) {
  c.hline(x, y, w, t.color.line, t.color.lineAlpha);
}

void paintBubble(Canvas &c, const Theme &t, int cx, int cy, int radius, uint8_t tint) {
  c.fillCircle(cx, cy, radius, t.color.tints[tint & 3]);
  c.strokeCircle(cx, cy, radius, 1, t.color.rim, t.color.rimAlpha / 2);
}

}  // namespace stella
