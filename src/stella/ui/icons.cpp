#include "icons.h"

#include <math.h>

namespace stella {
namespace icons {
namespace {

struct Frame {
  float cx, cy, s;  // centre and size of the square drawing area
  float stroke;
  float X(float u) const { return cx + u * s; }  // u, v in [-0.5, 0.5]
  float Y(float v) const { return cy + v * s; }
};

Frame frame(const Rect &box) {
  const float s = (float)(box.w < box.h ? box.w : box.h);
  float stroke = s * 0.085f;
  if (stroke < 1.4f) stroke = 1.4f;
  return Frame{box.x + box.w / 2.f, box.y + box.h / 2.f, s, stroke};
}

inline float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

void ring(Canvas &c, const Frame &f, float radius, Color color) {
  const float r = radius * f.s, half = f.stroke / 2.f;
  c.paint(Rect((int)(f.cx - r - 2), (int)(f.cy - r - 2), (int)(2 * r + 5), (int)(2 * r + 5)), color, 255,
          [=](float px, float py) {
            const float d = sqrtf((px - f.cx) * (px - f.cx) + (py - f.cy) * (py - f.cy));
            return clamp01(half + 0.5f - fabsf(d - r));
          });
}

void disc(Canvas &c, float x, float y, float r, Color color) {
  c.paint(Rect((int)(x - r - 2), (int)(y - r - 2), (int)(2 * r + 5), (int)(2 * r + 5)), color, 255,
          [=](float px, float py) { return clamp01(r + 0.5f - sqrtf((px - x) * (px - x) + (py - y) * (py - y))); });
}

void seg(Canvas &c, const Frame &f, float u0, float v0, float u1, float v1, Color color, float weight = 1.f) {
  c.line(f.X(u0), f.Y(v0), f.X(u1), f.Y(v1), f.stroke * weight, color);
}

}  // namespace

void sparkle(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  const float r = 0.48f * f.s;
  // Superellipse |x|^p + |y|^p = r^p with p < 1 gives the soft four-point star.
  const float p = 0.55f, rp = powf(r, p);
  c.paint(box.inset(-1), color, 255, [=](float px, float py) {
    const float dx = fabsf(px - f.cx), dy = fabsf(py - f.cy);
    const float s = powf(powf(dx, p) + powf(dy, p), 1.f / p);
    (void)rp;
    return clamp01(r + 0.5f - s);
  });
}

void clock(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  ring(c, f, 0.42f, color);
  seg(c, f, 0.f, 0.f, 0.f, -0.24f, color);
  seg(c, f, 0.f, 0.f, 0.17f, 0.08f, color);
}

void gear(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  ring(c, f, 0.27f, color);
  for (int i = 0; i < 8; ++i) {
    const float a = i * 3.14159265f / 4.f;
    seg(c, f, 0.29f * sinf(a), -0.29f * cosf(a), 0.44f * sinf(a), -0.44f * cosf(a), color, 1.6f);
  }
  disc(c, f.cx, f.cy, 0.07f * f.s, color);
}

void touch(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  ring(c, f, 0.30f, color);
  seg(c, f, -0.48f, 0.f, -0.18f, 0.f, color);
  seg(c, f, 0.18f, 0.f, 0.48f, 0.f, color);
  seg(c, f, 0.f, -0.48f, 0.f, -0.18f, color);
  seg(c, f, 0.f, 0.18f, 0.f, 0.48f, color);
  disc(c, f.cx, f.cy, 0.06f * f.s, color);
}

void back(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  seg(c, f, 0.12f, -0.30f, -0.14f, 0.f, color, 1.2f);
  seg(c, f, -0.14f, 0.f, 0.12f, 0.30f, color, 1.2f);
}

void chevronRight(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  seg(c, f, -0.10f, -0.26f, 0.14f, 0.f, color);
  seg(c, f, 0.14f, 0.f, -0.10f, 0.26f, color);
}

void home(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  for (int i = 0; i < 4; ++i) {
    const float u = (i % 2) ? 0.17f : -0.17f, v = (i / 2) ? 0.17f : -0.17f;
    const int half = (int)(0.13f * f.s + 0.5f);
    c.fillRoundRect(Rect((int)(f.X(u)) - half, (int)(f.Y(v)) - half, 2 * half, 2 * half), half / 2 + 1, color);
  }
}

void info(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  ring(c, f, 0.42f, color);
  seg(c, f, 0.f, -0.02f, 0.f, 0.22f, color);
  disc(c, f.cx, f.Y(-0.18f), f.stroke * 0.65f, color);
}

void moon(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  const float r = 0.40f * f.s, ox = f.cx + 0.22f * f.s, oy = f.cy - 0.16f * f.s, r2 = 0.34f * f.s;
  c.paint(box.inset(-1), color, 255, [=](float px, float py) {
    const float inA = clamp01(r + 0.5f - sqrtf((px - f.cx) * (px - f.cx) + (py - f.cy) * (py - f.cy)));
    const float inB = clamp01(r2 + 0.5f - sqrtf((px - ox) * (px - ox) + (py - oy) * (py - oy)));
    return clamp01(inA - inB);
  });
}

void sun(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  disc(c, f.cx, f.cy, 0.18f * f.s, color);
  for (int i = 0; i < 8; ++i) {
    const float a = i * 3.14159265f / 4.f;
    seg(c, f, 0.30f * sinf(a), -0.30f * cosf(a), 0.44f * sinf(a), -0.44f * cosf(a), color);
  }
}

void wifi(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  const int cx = (int)f.cx, cy = (int)(f.cy + 0.28f * f.s);
  const int t = (int)(f.stroke + 0.5f);
  for (int i = 1; i <= 2; ++i) {
    const int r = (int)(f.s * (0.20f + 0.20f * i));
    c.strokeArc(cx, cy, r, t, -45.f, 90.f, color);
  }
  disc(c, cx + 0.5f, cy + 0.5f - f.stroke * 0.4f, f.stroke * 0.9f, color);
}

void appearance(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  ring(c, f, 0.40f, color);
  const float r = 0.40f * f.s;
  c.paint(box, color, 255, [=](float px, float py) {
    if (px < f.cx) return 0.f;
    return clamp01(r + 0.5f - sqrtf((px - f.cx) * (px - f.cx) + (py - f.cy) * (py - f.cy))) * clamp01(px - f.cx + 0.5f);
  });
}

void check(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  seg(c, f, -0.26f, 0.02f, -0.07f, 0.20f, color, 1.2f);
  seg(c, f, -0.07f, 0.20f, 0.28f, -0.20f, color, 1.2f);
}

void lock(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  const int w = (int)(0.56f * f.s), h = (int)(0.40f * f.s);
  c.fillRoundRect(Rect((int)(f.cx - w / 2.f), (int)f.Y(0.02f), w, h), (int)(0.08f * f.s) + 1, color);
  const int r = (int)(0.17f * f.s);
  c.strokeArc((int)f.cx, (int)f.Y(0.02f), r + (int)(f.stroke / 2), (int)(f.stroke + 0.5f), -90.f, 180.f, color);
}

void bell(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  // Dome and flared body as one shape: the half-width grows down the bell.
  const float top = f.Y(-0.30f), shoulder = f.Y(-0.10f), lip = f.Y(0.20f);
  const float r = 0.22f * f.s;
  c.paint(box.inset(-1), color, 255, [=](float px, float py) {
    const float dx = fabsf(px - f.cx);
    float cov;
    if (py < shoulder) {
      const float d = sqrtf(dx * dx + (py - shoulder) * (py - shoulder) * 1.8f);
      cov = clamp01(r + 0.5f - d);
    } else {
      const float half = r + (py - shoulder) * 0.38f;
      cov = clamp01(half + 0.5f - dx) * clamp01(lip + 0.5f - py);
    }
    return py < top - 1.f ? 0.f : cov;
  });
  seg(c, f, -0.31f, 0.21f, 0.31f, 0.21f, color, 1.1f);
  disc(c, f.cx, f.Y(0.31f), 0.075f * f.s, color);
  disc(c, f.cx, f.Y(-0.33f), 0.05f * f.s, color);
}

void stopwatch(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  const Frame body{f.cx, f.Y(0.06f), f.s, f.stroke};
  ring(c, body, 0.34f, color);
  seg(c, f, 0.f, -0.29f, 0.f, -0.40f, color);
  seg(c, f, -0.10f, -0.42f, 0.10f, -0.42f, color);
  seg(c, f, 0.25f, -0.22f, 0.31f, -0.28f, color);
  seg(c, f, 0.f, 0.06f, 0.f, -0.14f, color);
  seg(c, f, 0.f, 0.06f, 0.12f, 0.12f, color);
}

void hourglass(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  seg(c, f, -0.24f, -0.36f, 0.24f, -0.36f, color);  // top and bottom caps
  seg(c, f, -0.24f, 0.36f, 0.24f, 0.36f, color);
  seg(c, f, -0.18f, -0.36f, 0.f, 0.f, color);  // the glass: two funnels meeting at the neck
  seg(c, f, 0.18f, -0.36f, 0.f, 0.f, color);
  seg(c, f, 0.f, 0.f, -0.18f, 0.36f, color);
  seg(c, f, 0.f, 0.f, 0.18f, 0.36f, color);
  disc(c, f.cx, f.Y(0.24f), 0.055f * f.s, color);  // sand
}

void plus(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  seg(c, f, -0.26f, 0.f, 0.26f, 0.f, color);
  seg(c, f, 0.f, -0.26f, 0.f, 0.26f, color);
}

void chevronUp(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  seg(c, f, -0.26f, 0.10f, 0.f, -0.14f, color);
  seg(c, f, 0.f, -0.14f, 0.26f, 0.10f, color);
}

void chevronDown(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  seg(c, f, -0.26f, -0.10f, 0.f, 0.14f, color);
  seg(c, f, 0.f, 0.14f, 0.26f, -0.10f, color);
}

namespace {

// Soft cloud: three overlapping discs on a rounded base (union by max).
float cloudCoverage(const Frame &f, float px, float py, float ox, float oy, float scale) {
  const struct {
    float u, v, r;
  } puffs[] = {{-0.18f, 0.06f, 0.17f}, {0.02f, -0.06f, 0.22f}, {0.20f, 0.06f, 0.16f}};
  float cov = 0.f;
  for (const auto &p : puffs) {
    const float x = f.cx + (ox + p.u * scale) * f.s, y = f.cy + (oy + p.v * scale) * f.s, r = p.r * scale * f.s;
    cov = fmaxf(cov, clamp01(r + 0.5f - sqrtf((px - x) * (px - x) + (py - y) * (py - y))));
  }
  // Flat bottom between the outer puffs.
  const float left = f.cx + (ox - 0.18f * scale) * f.s, right = f.cx + (ox + 0.20f * scale) * f.s;
  const float top = f.cy + (oy + 0.04f * scale) * f.s, bottom = f.cy + (oy + 0.22f * scale) * f.s;
  if (px >= left && px <= right) cov = fmaxf(cov, clamp01(fminf(py - top, bottom - py) + 0.5f));
  return cov;
}

void cloudAt(Canvas &c, const Rect &box, Color color, float ox, float oy, float scale) {
  const Frame f = frame(box);
  c.paint(box.inset(-1), color, 255, [=](float px, float py) { return cloudCoverage(f, px, py, ox, oy, scale); });
}

}  // namespace

void cloud(Canvas &c, const Rect &box, Color color) { cloudAt(c, box, color, 0.f, 0.f, 1.15f); }

void partlyCloudy(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  disc(c, f.X(-0.12f), f.Y(-0.14f), 0.17f * f.s, color);
  for (int i = 0; i < 6; ++i) {
    const float a = -1.9f + i * 0.62f;
    seg(c, f, -0.12f + 0.27f * sinf(a), -0.14f - 0.27f * cosf(a), -0.12f + 0.36f * sinf(a), -0.14f - 0.36f * cosf(a),
        color, 0.8f);
  }
  cloudAt(c, box, color, 0.08f, 0.10f, 0.95f);
}

void rain(Canvas &c, const Rect &box, Color color) {
  cloudAt(c, box, color, 0.f, -0.12f, 1.0f);
  const Frame f = frame(box);
  for (int i = 0; i < 3; ++i) seg(c, f, -0.14f + i * 0.14f, 0.20f, -0.19f + i * 0.14f, 0.38f, color);
}

void snow(Canvas &c, const Rect &box, Color color) {
  cloudAt(c, box, color, 0.f, -0.12f, 1.0f);
  const Frame f = frame(box);
  for (int i = 0; i < 3; ++i) disc(c, f.X(-0.15f + i * 0.15f), f.Y(i == 1 ? 0.36f : 0.27f), f.stroke * 0.9f, color);
}

void storm(Canvas &c, const Rect &box, Color color) {
  cloudAt(c, box, color, 0.f, -0.14f, 1.0f);
  const Frame f = frame(box);
  seg(c, f, 0.04f, 0.14f, -0.06f, 0.30f, color, 1.1f);
  seg(c, f, -0.06f, 0.30f, 0.06f, 0.30f, color, 1.1f);
  seg(c, f, 0.06f, 0.30f, -0.04f, 0.46f, color, 1.1f);
}

void fog(Canvas &c, const Rect &box, Color color) {
  const Frame f = frame(box);
  seg(c, f, -0.34f, -0.16f, 0.30f, -0.16f, color);
  seg(c, f, -0.26f, 0.f, 0.36f, 0.f, color);
  seg(c, f, -0.34f, 0.16f, 0.22f, 0.16f, color);
}

void wifiBars(Canvas &c, const Rect &box, Color color, int bars) {
  const Frame f = frame(box);
  const int cx = (int)f.cx, cy = (int)(f.cy + 0.30f * f.s);
  const int t = (int)(f.stroke + 0.5f);
  for (int i = 1; i <= 3; ++i) {
    const int r = (int)(f.s * (0.14f + 0.16f * i));
    c.strokeArc(cx, cy, r, t, -45.f, 90.f, color, bars > i ? 255 : 70);
  }
  disc(c, cx + 0.5f, cy + 0.5f - f.stroke * 0.4f, f.stroke * 0.9f, color);
}

}  // namespace icons
}  // namespace stella
