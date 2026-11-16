#include "theme.h"

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

Metrics metricsFor(Density d) {
  if (d == Density::Regular) {
    return Metrics{22, 14, 6, 11, 17, 14, 22, 16, 999, 34, 50, 56, 32, 14};
  }
  return Metrics{16, 10, 4, 8, 12, 10, 16, 12, 999, 26, 36, 40, 24, 10};
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

Theme makeTheme(Density density, bool dark, SkyPhase phase) {
  const int p = (int)phase & 3;
  Theme t{};
  t.density = density;
  t.phase = phase;
  t.m = metricsFor(density);
  t.font = density == Density::Regular ? fonts::kRegular : fonts::kCompact;

  Palette &c = t.color;
  c.dark = dark;
  const uint32_t(&sky)[4][3] = dark ? kDarkSky : kLightSky;
  for (int i = 0; i < 3; ++i) c.sky[i] = rgbHex(sky[p][i]);
  c.skyTint = rgbHex(kSkyTint[p]);
  c.accent = rgbHex(dark ? kDarkAccent[p] : kLightAccent[p]);
  if (dark) {
    c.orbPink = rgbHex(0x6a435e);
    c.orbLavender = rgbHex(0x534374);
    c.orbGold = rgbHex(0x58455f);
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
    c.tints[0] = rgbHex(0x6a435e);
    c.tints[1] = rgbHex(0x534374);
    c.tints[2] = rgbHex(0x3e4a6b);
    c.tints[3] = rgbHex(0x684e56);
  } else {
    c.orbPink = rgbHex(0xe9bccd);
    c.orbLavender = rgbHex(0xc0b2e4);
    c.orbGold = rgbHex(0xe7cde1);
    c.orbAlpha = kLightOrb[p];
    c.text = rgbHex(0x3d354d);
    c.textMuted = rgbHex(0x685e74);
    c.accentText = rgbHex(0xfaf7fb);
    c.glass = rgbHex(0xfffdff);
    c.glassAlpha = 179;  // rgba(255,253,255,.7)
    c.rim = rgbHex(0xffffff);
    c.rimAlpha = 240;
    c.line = rgbHex(0x5d4878);
    c.lineAlpha = 33;  // rgba(93,72,120,.13)
    c.shadow = rgbHex(0x4b346c);
    c.shadowAlpha = 30;
    c.surface = rgbHex(0xfaf7fb);
    c.good = rgbHex(0x5f9e8c);
    c.warn = rgbHex(0xa8823a);
    c.danger = rgbHex(0xb35d74);
    c.tints[0] = rgbHex(0xf3d6e1);
    c.tints[1] = rgbHex(0xdcd3f1);
    c.tints[2] = rgbHex(0xd3e0f1);
    c.tints[3] = rgbHex(0xf3e0d4);
  }
  return t;
}

}  // namespace stella
