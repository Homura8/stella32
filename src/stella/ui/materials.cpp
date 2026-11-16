#include "materials.h"

namespace stella {
namespace {

constexpr uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

struct Rgb {
  float r, g, b;
};

Rgb toRgb(Color c) { return Rgb{(float)red8(c), (float)green8(c), (float)blue8(c)}; }

inline Rgb mix(const Rgb &a, const Rgb &b, float t) {
  return Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// Soft elliptical wash: 1 at the centre fading smoothly to 0 at the ellipse edge.
struct Wash {
  float cx, cy, invRx2, invRy2, strength;
  Rgb color;
  float weight(float x, float y) const {
    const float dx = x - cx, dy = y - cy;
    float f = 1.f - (dx * dx * invRx2 + dy * dy * invRy2);
    if (f <= 0.f) return 0.f;
    return f * f * strength;
  }
};

Wash makeWash(const Rect &s, float fx, float fy, float frx, float fry, float strength, Color c) {
  const float rx = frx * s.w, ry = fry * s.h;
  return Wash{s.x + fx * s.w, s.y + fy * s.h, 1.f / (rx * rx), 1.f / (ry * ry), strength, toRgb(c)};
}

inline uint16_t dither(float v, int levels, uint8_t threshold) {
  // floor(v / 255 * levels + d/16): averages to correct rounding, no banding.
  int q = (int)(v * levels / 255.f + threshold / 16.f);
  return (uint16_t)(q < 0 ? 0 : q > levels ? levels : q);
}

}  // namespace

void paintSky(Canvas &c, const Theme &t, const Rect &screen) {
  const Rect area = c.clip().intersect(screen);
  if (area.empty()) return;
  const Palette &p = t.color;
  const Rgb stops[3] = {toRgb(p.sky[0]), toRgb(p.sky[1]), toRgb(p.sky[2])};
  const float orb = p.orbAlpha / 255.f;
  // Positions follow the site's .pastel-bg layers (pink top-left, tint
  // top-right, gold low centre), stretched to the panel's aspect ratio.
  const Wash washes[3] = {
      makeWash(screen, 0.15f, 0.20f, 0.80f, 0.95f, 0.55f * orb, p.orbPink),
      makeWash(screen, 0.88f, 0.12f, 0.75f, 0.85f, 0.50f * orb, p.skyTint),
      makeWash(screen, 0.48f, 0.92f, 0.60f, 0.70f, 0.45f * orb, p.orbGold),
  };
  // CSS linear-gradient(120deg): direction (sin 120°, -cos 120°).
  const float dirX = 0.8660254f, dirY = 0.5f;
  const float length = screen.w * dirX + screen.h * dirY;
  const float cx = screen.x + screen.w / 2.f, cy = screen.y + screen.h / 2.f;
  for (int y = area.y; y < area.bottom(); ++y) {
    const float py = y + 0.5f;
    float tg = ((area.x + 0.5f - cx) * dirX + (py - cy) * dirY) / length + 0.5f;
    const float step = dirX / length;
    for (int x = area.x; x < area.right(); ++x, tg += step) {
      const float px = x + 0.5f;
      const float g = tg < 0.f ? 0.f : tg > 1.f ? 1.f : tg;
      Rgb col = g < 0.5f ? mix(stops[0], stops[1], g * 2.f) : mix(stops[1], stops[2], g * 2.f - 1.f);
      for (const Wash &w : washes) {
        const float a = w.weight(px, py);
        if (a > 0.f) col = mix(col, w.color, a > 1.f ? 1.f : a);
      }
      const uint8_t d = kBayer4[y & 3][x & 3];
      c.at(x, y) = (Color)(dither(col.r, 31, d) << 11 | dither(col.g, 63, d) << 5 | dither(col.b, 31, d));
    }
  }
}

int glassOutset(const Theme &t) { return t.dp(4); }

void paintGlass(Canvas &c, const Theme &t, const Rect &r, int radius, Elevation e) {
  if (!c.visible(r.inset(-glassOutset(t)))) return;
  const Palette &p = t.color;
  if (e == Elevation::Raised) {
    // Two offset layers approximate the site's soft 0 12px 40px shadow.
    c.fillRoundRect(r.offset(0, t.dp(3)).inset(-t.dp(1), 0), radius + t.dp(1), p.shadow, p.shadowAlpha / 2);
    c.fillRoundRect(r.offset(0, t.dp(1)), radius, p.shadow, p.shadowAlpha / 2);
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
