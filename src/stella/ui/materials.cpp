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

namespace {

// A soft drop shadow, a smooth falloff instead of a few hard-edged layers.
// `blur` is the half-width of the transition (it reaches `blur` beyond the
// source rectangle's edge and is half strength on it), `dy` shifts the source
// down so the light seems to come from above. Like CSS box-shadow it is only
// visible outside the card: the glass shows the sky through it, not its own
// shadow. Straight edges are one alpha per row or column; only the corners
// need a per-pixel distance.
struct ShadowShape {
  int dy, blur;
  uint8_t peak;  // alpha where the shadow is strongest
};

ShadowShape shadowShape(const Theme &t, ShadowSize size) {
  const Palette &p = t.color;
  const int peak = p.shadowAlpha * 5 / 4;
  if (size == ShadowSize::Large) return {t.dp(3), t.dp(5), (uint8_t)(peak > 255 ? 255 : peak)};
  return {t.dp(2), t.dp(3), (uint8_t)(peak > 255 ? 255 : peak)};
}

// 1 well inside the blur, 0 beyond it, smooth between (d: distance outside the source edge).
float falloff(float d, float blur) {
  const float x = (d + blur) / (2.f * blur);
  if (x <= 0.f) return 1.f;
  if (x >= 1.f) return 0.f;
  return 1.f - x * x * (3.f - 2.f * x);
}

// Signed distance to a rounded rectangle (negative inside).
float roundRectDistance(float px, float py, float x0, float y0, float x1, float y1, float r) {
  const float qx = fabsf(px - (x0 + x1) * .5f) - ((x1 - x0) * .5f - r);
  const float qy = fabsf(py - (y0 + y1) * .5f) - ((y1 - y0) * .5f - r);
  const float ox = fmaxf(qx, 0.f), oy = fmaxf(qy, 0.f);
  return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.f) - r;
}

uint8_t scaled(uint8_t peak, float g) { return (uint8_t)(peak * g + 0.5f); }

void softShadow(Canvas &c, const Rect &card, int cardRadius, const ShadowShape &s, Color color) {
  if (s.peak == 0 || s.blur <= 0) return;
  const float radius = fminf((float)cardRadius, fminf(card.w, card.h) / 2.f);
  const Rect src = card.offset(0, s.dy);
  const float blur = (float)s.blur;
  const float cx0 = card.x, cy0 = card.y, cx1 = card.right(), cy1 = card.bottom();
  const float sx0 = src.x, sy0 = src.y, sx1 = src.right(), sy1 = src.bottom();
  const Rect all = card.unite(src.inset(-s.blur));
  const int band = (int)ceilf(radius) + s.blur + s.dy + 2;
  auto coverage = [=](float px, float py) {
    const float g = falloff(roundRectDistance(px, py, sx0, sy0, sx1, sy1, radius), blur);
    return g <= 0.f ? 0.f : g * (1.f - roundRectCoverage(px, py, cx0, cy0, cx1, cy1, radius));
  };
  if (all.w <= 2 * band || all.h <= 2 * band) {  // small: one pass over everything
    c.paint(all, color, s.peak, coverage);
    return;
  }
  const int left = all.x + band, right = all.right() - band, top = all.y + band, bottom = all.bottom() - band;
  // Corners: exact distance.
  c.paint(Rect(all.x, all.y, band, band), color, s.peak, coverage);
  c.paint(Rect(right, all.y, band, band), color, s.peak, coverage);
  c.paint(Rect(all.x, bottom, band, band), color, s.peak, coverage);
  c.paint(Rect(right, bottom, band, band), color, s.peak, coverage);
  // Straight edges: the same alpha along each row or column.
  for (int y = all.y; y < card.y; ++y) {
    const uint8_t a = scaled(s.peak, falloff(sy0 - (y + 0.5f), blur));
    if (a) c.hline(left, y, right - left, color, a);
  }
  for (int y = card.bottom(); y < all.bottom(); ++y) {
    const uint8_t a = scaled(s.peak, falloff((y + 0.5f) - sy1, blur));
    if (a) c.hline(left, y, right - left, color, a);
  }
  for (int x = all.x; x < card.x; ++x) {
    const uint8_t a = scaled(s.peak, falloff(sx0 - (x + 0.5f), blur));
    if (a) c.vline(x, top, bottom - top, color, a);
  }
  for (int x = card.right(); x < all.right(); ++x) {
    const uint8_t a = scaled(s.peak, falloff((x + 0.5f) - sx1, blur));
    if (a) c.vline(x, top, bottom - top, color, a);
  }
}

}  // namespace

// The largest reach of any shadow: below the card, the offset plus the blur.
int glassOutset(const Theme &t) { return t.dp(3) + t.dp(5) + 1; }

void paintShadow(Canvas &c, const Theme &t, const Rect &r, int radius, ShadowSize size) {
  if (!c.visible(r.inset(-glassOutset(t)))) return;
  softShadow(c, r, radius, shadowShape(t, size), t.color.shadow);
}

void paintGlass(Canvas &c, const Theme &t, const Rect &r, int radius, Elevation e) {
  if (!c.visible(r.inset(-glassOutset(t)))) return;
  const Palette &p = t.color;
  if (e == Elevation::Raised) {
    // Controls (buttons, rows) get the tighter shadow, cards and sheets the larger one.
    softShadow(c, r, radius, shadowShape(t, r.h <= t.dp(56) ? ShadowSize::Small : ShadowSize::Large), p.shadow);
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
