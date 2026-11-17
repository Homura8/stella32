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

// Turns design colours (hex) into panel colours: the tone (saturation and,
// for light materials, depth) and then the contrast gamma, in 8-bit before
// quantising to RGB565.
struct Toner {
  ToneParams k;
  float contrast;  // gamma, 1.0 = none

  float curve(float v, float gamma) const { return gamma == 1.f ? v : 255.f * powf(v / 255.f, gamma); }

  // Design colour with contrast only (text and other inks).
  Color ink(uint32_t hex) const { return tone(hex, false, 1.f); }

  Color tone(uint32_t hex, bool deepen, float saturation = -1.f) const {
    float r = (float)((hex >> 16) & 0xff), g = (float)((hex >> 8) & 0xff), b = (float)(hex & 0xff);
    const float sat = saturation < 0.f ? k.saturation : saturation;
    if (sat != 1.f) {
      const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
      r = clamp255(luma + (r - luma) * sat);
      g = clamp255(luma + (g - luma) * sat);
      b = clamp255(luma + (b - luma) * sat);
    }
    const float gamma = (deepen ? k.depth : 1.f) * contrast;
    r = curve(r, gamma);
    g = curve(g, gamma);
    b = curve(b, gamma);
    return rgb((uint8_t)(r + 0.5f), (uint8_t)(g + 0.5f), (uint8_t)(b + 0.5f));
  }
};

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
    return Metrics{22, 14, 6, 11, 17, 14, 22, 16, 999, 44, 50, 56, 32, 22};
  }
  return Metrics{16, 10, 4, 8, 12, 10, 16, 12, 999, 32, 36, 40, 24, 16};
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

Color lookColor(const Look &look, uint32_t hex) {
  const uint8_t contrast = look.contrast > 100 ? 100 : look.contrast;
  return Toner{kTones[0], 1.f + contrast * 0.012f}.ink(hex);
}

Theme makeTheme(Density density, bool dark, SkyPhase phase, const Look &look) {
  const int p = (int)phase & 3;
  const ToneParams &k = kTones[(int)look.tone <= 2 ? (int)look.tone : 0];
  const uint8_t contrast = look.contrast > 100 ? 100 : look.contrast;
  const Toner x{k, 1.f + contrast * 0.012f};
  Theme t{};
  t.density = density;
  t.phase = phase;
  t.look = look;
  t.m = metricsFor(density);
  t.font = density == Density::Regular ? fonts::kRegular : fonts::kCompact;

  Palette &c = t.color;
  c.dark = dark;
  const uint32_t(&sky)[4][3] = dark ? kDarkSky : kLightSky;
  // Light skies lean toward the phase's own tint so dawn, day, dusk and night
  // stay recognisable on panels that flatten pastels.
  for (int i = 0; i < 3; ++i) {
    const uint32_t base = dark ? sky[p][i] : mixHex(sky[p][i], kSkyTint[p], k.phaseTint * (0.6f + 0.2f * i));
    c.sky[i] = x.tone(base, !dark);
  }
  c.skyTint = x.tone(kSkyTint[p], !dark);
  // Accents keep their contrast with text-coloured fills: saturation only.
  c.accent = x.tone(dark ? kDarkAccent[p] : kLightAccent[p], false, 1.f + (k.saturation - 1.f) * 0.5f);
  if (dark) {
    c.orbPink = x.tone(0x6a435e, false);
    c.orbLavender = x.tone(0x534374, false);
    c.orbGold = x.tone(0x58455f, false);
    c.orbAlpha = kDarkOrb[p];
    c.text = x.ink(0xeee8f5);
    c.textMuted = x.ink(0xbcb2c9);
    c.accentText = x.ink(0x211e30);
    c.glass = x.ink(0x2e283f);
    c.glassAlpha = 212;  // rgba(46,40,63,.83)
    c.rim = x.ink(0xeddeff);
    c.rimAlpha = 46;  // rgba(237,222,255,.18)
    c.line = x.ink(0xd7c5ef);
    c.lineAlpha = 41;  // rgba(215,197,239,.16)
    c.shadow = x.ink(0x080414);
    c.shadowAlpha = 70;
    c.surface = x.ink(0x302a3d);
    c.good = x.ink(0xb3d6cd);
    c.warn = x.ink(0xe7d8b5);
    c.danger = x.ink(0xf0a9b5);
    c.tints[0] = x.tone(0x6a435e, false);
    c.tints[1] = x.tone(0x534374, false);
    c.tints[2] = x.tone(0x3e4a6b, false);
    c.tints[3] = x.tone(0x684e56, false);
  } else {
    c.orbPink = x.tone(0xe9bccd, true);
    c.orbLavender = x.tone(0xc0b2e4, true);
    c.orbGold = x.tone(0xe7cde1, true);
    c.orbAlpha = kLightOrb[p];
    c.text = x.ink(0x3d354d);
    c.textMuted = x.ink(0x685e74);
    c.accentText = x.ink(0xfaf7fb);
    c.glass = x.ink(0xfffdff);
    c.glassAlpha = 179;  // rgba(255,253,255,.7)
    c.rim = x.ink(0xffffff);
    c.rimAlpha = 240;
    c.line = x.ink(0x5d4878);
    c.lineAlpha = gain(33, k.edge);  // rgba(93,72,120,.13)
    c.shadow = x.ink(0x4b346c);
    c.shadowAlpha = gain(30, k.edge);
    c.surface = x.ink(0xfaf7fb);
    c.good = x.ink(0x5f9e8c);
    c.warn = x.ink(0xa8823a);
    c.danger = x.ink(0xb35d74);
    c.tints[0] = x.tone(0xf3d6e1, true);
    c.tints[1] = x.tone(0xdcd3f1, true);
    c.tints[2] = x.tone(0xd3e0f1, true);
    c.tints[3] = x.tone(0xf3e0d4, true);
  }
  return t;
}

}  // namespace stella
