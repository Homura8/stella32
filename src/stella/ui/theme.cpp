#include "theme.h"

#include <math.h>

namespace stella {
namespace {

// Celestia Glow atmosphere palettes (script.js: atmospherePalettes), per phase.
constexpr uint32_t kLightSky[4][3] = {
    {0xfaf0e5, 0xf4e8ed, 0xebe7f4},  // dawn
    {0xf7f2ee, 0xf0eff5, 0xe8edf5},  // day
    {0xf6e8df, 0xf0e1eb, 0xe5dff3},  // dusk
    {0xeef0f7, 0xeeebf5, 0xe5eaf6},  // night
};
constexpr uint32_t kDarkSky[4][3] = {
    {0x2c2140, 0x3a2740, 0x241c3a},
    {0x2c1f3f, 0x332750, 0x1e2a45},
    {0x3a2436, 0x45283f, 0x241f3f},
    {0x1c1330, 0x201a38, 0x141225},
};
constexpr uint32_t kSkyTint[4] = {0xe5baca, 0xc5cfdf, 0xc9acd9, 0xa9aed9};
constexpr uint32_t kLightAccent[4] = {0x85586b, 0x5d6980, 0x775386, 0x625e84};
constexpr uint32_t kDarkAccent[4] = {0xe1bccb, 0xc1cbe0, 0xd6b7e5, 0xbdbde7};
// Wash strength per phase (orbOpacity), scaled for a small emissive panel.
constexpr uint8_t kLightOrb[4] = {128, 128, 140, 115};
constexpr uint8_t kDarkOrb[4] = {115, 115, 128, 128};

// Strength of each ColorTone (Soft is the identity).
struct ToneParams {
  float phaseTint;   // light skies blend toward the phase's sky tint
  float saturation;  // chroma gain around luma
  float depth;       // light materials: gamma > 1 deepens near-white tones
  float edge;        // line and shadow opacity gain (light)
};
constexpr ToneParams kTones[3] = {
    {0.00f, 1.00f, 1.00f, 1.0f},  // Soft
    {0.30f, 1.45f, 1.35f, 1.7f},  // Rich
    {0.48f, 1.90f, 1.70f, 2.4f},  // Vivid
};

inline float clamp255(float v) { return v < 0.f ? 0.f : v > 255.f ? 255.f : v; }

// Saturation gain, then (for light materials) a gamma that pulls pastels away
// from white, where small panels lose them.
Color tone(uint32_t hex, const ToneParams &k, bool deepen, float saturation = -1.f) {
  float r = (float)((hex >> 16) & 0xff), g = (float)((hex >> 8) & 0xff), b = (float)(hex & 0xff);
  const float sat = saturation < 0.f ? k.saturation : saturation;
  const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
  r = clamp255(luma + (r - luma) * sat);
  g = clamp255(luma + (g - luma) * sat);
  b = clamp255(luma + (b - luma) * sat);
  if (deepen && k.depth != 1.f) {
    r = 255.f * powf(r / 255.f, k.depth);
    g = 255.f * powf(g / 255.f, k.depth);
    b = 255.f * powf(b / 255.f, k.depth);
  }
  return rgb((uint8_t)(r + 0.5f), (uint8_t)(g + 0.5f), (uint8_t)(b + 0.5f));
}

uint32_t mixHex(uint32_t a, uint32_t b, float t) {
  uint32_t out = 0;
  for (int shift = 0; shift <= 16; shift += 8) {
    const float ca = (float)((a >> shift) & 0xff), cb = (float)((b >> shift) & 0xff);
    out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << shift;
  }
  return out;
}

uint8_t gain(uint8_t alpha, float k) {
  const float v = alpha * k;
  return (uint8_t)(v > 255.f ? 255.f : v);
}

Metrics metricsFor(Density d) {
  if (d == Density::Regular) {
    return Metrics{22, 14, 6, 11, 17, 14, 22, 16, 999, 34, 50, 56, 32, 22};
  }
  return Metrics{16, 10, 4, 8, 12, 10, 16, 12, 999, 26, 36, 40, 24, 16};
}

}  // namespace

Density densityFor(int width, int height) {
  const int shortSide = width < height ? width : height;
  return shortSide >= 300 ? Density::Regular : Density::Compact;
}

SkyPhase phaseForHour(int hour) {
  if (hour >= 5 && hour < 8) return SkyPhase::Dawn;
  if (hour >= 8 && hour < 17) return SkyPhase::Day;
  if (hour >= 17 && hour < 20) return SkyPhase::Dusk;
  return SkyPhase::Night;
}

bool resolveDark(Appearance appearance, SkyPhase phase) {
  if (appearance == Appearance::Light) return false;
  if (appearance == Appearance::Dark) return true;
  return phase == SkyPhase::Dusk || phase == SkyPhase::Night;
}

const char *phaseName(SkyPhase phase) {
  static const char *const kNames[] = {"dawn", "day", "dusk", "night"};
  return kNames[(int)phase & 3];
}

const char *toneName(ColorTone tone) {
  static const char *const kNames[] = {"Soft", "Rich", "Vivid"};
  return kNames[(int)tone <= 2 ? (int)tone : 1];
}

Theme makeTheme(Density density, bool dark, SkyPhase phase, ColorTone colorTone) {
  const int p = (int)phase & 3;
  const ToneParams &k = kTones[(int)colorTone <= 2 ? (int)colorTone : 1];
  Theme t{};
  t.density = density;
  t.phase = phase;
  t.tone = colorTone;
  t.m = metricsFor(density);
  t.font = density == Density::Regular ? fonts::kRegular : fonts::kCompact;

  Palette &c = t.color;
  c.dark = dark;
  const uint32_t(&sky)[4][3] = dark ? kDarkSky : kLightSky;
  // Light skies lean toward the phase's own tint so dawn, day, dusk and night
  // stay recognisable on panels that flatten pastels.
  for (int i = 0; i < 3; ++i) {
    const uint32_t base = dark ? sky[p][i] : mixHex(sky[p][i], kSkyTint[p], k.phaseTint * (0.6f + 0.2f * i));
    c.sky[i] = tone(base, k, !dark);
  }
  c.skyTint = tone(kSkyTint[p], k, !dark);
  // Accents keep their contrast with text-coloured fills: saturation only.
  c.accent = tone(dark ? kDarkAccent[p] : kLightAccent[p], k, false, 1.f + (k.saturation - 1.f) * 0.5f);
  if (dark) {
    c.orbPink = tone(0x6a435e, k, false);
    c.orbLavender = tone(0x534374, k, false);
    c.orbGold = tone(0x58455f, k, false);
    c.orbAlpha = kDarkOrb[p];
    c.text = rgbHex(0xeee8f5);
    c.textMuted = rgbHex(0xbcb2c9);
    c.accentText = rgbHex(0x211e30);
    c.glass = rgbHex(0x2e283f);
    c.glassAlpha = 212;  // rgba(46,40,63,.83)
    c.rim = rgbHex(0xeddeff);
    c.rimAlpha = 46;  // rgba(237,222,255,.18)
    c.line = rgbHex(0xd7c5ef);
    c.lineAlpha = 41;  // rgba(215,197,239,.16)
    c.shadow = rgbHex(0x080414);
    c.shadowAlpha = 70;
    c.surface = rgbHex(0x302a3d);
    c.good = rgbHex(0xb3d6cd);
    c.warn = rgbHex(0xe7d8b5);
    c.danger = rgbHex(0xf0a9b5);
    c.tints[0] = tone(0x6a435e, k, false);
    c.tints[1] = tone(0x534374, k, false);
    c.tints[2] = tone(0x3e4a6b, k, false);
    c.tints[3] = tone(0x684e56, k, false);
  } else {
    c.orbPink = tone(0xe9bccd, k, true);
    c.orbLavender = tone(0xc0b2e4, k, true);
    c.orbGold = tone(0xe7cde1, k, true);
    c.orbAlpha = kLightOrb[p];
    c.text = rgbHex(0x3d354d);
    c.textMuted = rgbHex(0x685e74);
    c.accentText = rgbHex(0xfaf7fb);
    c.glass = rgbHex(0xfffdff);
    c.glassAlpha = 179;  // rgba(255,253,255,.7)
    c.rim = rgbHex(0xffffff);
    c.rimAlpha = 240;
    c.line = rgbHex(0x5d4878);
    c.lineAlpha = gain(33, k.edge);  // rgba(93,72,120,.13)
    c.shadow = rgbHex(0x4b346c);
    c.shadowAlpha = gain(30, k.edge);
    c.surface = rgbHex(0xfaf7fb);
    c.good = rgbHex(0x5f9e8c);
    c.warn = rgbHex(0xa8823a);
    c.danger = rgbHex(0xb35d74);
    c.tints[0] = tone(0xf3d6e1, k, true);
    c.tints[1] = tone(0xdcd3f1, k, true);
    c.tints[2] = tone(0xd3e0f1, k, true);
    c.tints[3] = tone(0xf3e0d4, k, true);
  }
  return t;
}

}  // namespace stella
