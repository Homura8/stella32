#pragma once
// Celestia Glow design system for Stella: "frosted observatory" — soft pastel
// skies, translucent glass surfaces, quiet text colours and rounded forms.
//
// This header is the single source of visual truth. Screens and widgets must
// use these tokens (colours, spacing, radii, fonts, sizes) rather than
// literals, so a restyle or a new screen size is a change here, not in apps.
//
// Tokens follow the Celestia Glow site (style.css / script.js): light and dark
// palettes, each with dawn/day/dusk/night sky gradients and accents.
#include <stdint.h>

#include "../core/color.h"
#include "../core/geometry.h"
#include "../fonts/fonts.h"

namespace stella {

enum class Appearance : uint8_t { Auto = 0, Light = 1, Dark = 2 };
enum class SkyPhase : uint8_t { Dawn = 0, Day = 1, Dusk = 2, Night = 3 };
// Density class picks fonts and metrics. Compact: ~320x240. Regular: ~480x320+.
enum class Density : uint8_t { Compact = 0, Regular = 1 };

struct Palette {
  bool dark;
  Color sky[3];          // diagonal gradient stops (--bg-1..3)
  Color skyTint;         // top-right wash (--sky-tint)
  Color orbPink, orbLavender, orbGold;
  uint8_t orbAlpha;      // strength of the washes (--orb-opacity)
  Color text, textMuted;
  Color accent;          // phase accent (--accent)
  Color accentText;      // text drawn on an accent fill
  Color glass;           // card material (--glass-bg) ...
  uint8_t glassAlpha;    // ... and its opacity
  Color rim;             // glass highlight edge (--glass-rim)
  uint8_t rimAlpha;
  Color line;            // hairlines and separators (--line)
  uint8_t lineAlpha;
  Color shadow;          // soft drop shadow (--glass-shadow)
  uint8_t shadowAlpha;
  Color surface;         // opaque soft chip (--white-soft)
  Color good, warn, danger;
  Color tints[4];        // icon bubbles: pink, lavender, sky, peach
};

struct Metrics {
  uint8_t scale16;       // 16 = 1.0x (compact baseline)
  int16_t margin;        // screen edge to content
  int16_t gapS, gapM, gapL;
  int16_t padding;       // inside cards
  int16_t radiusCard, radiusControl, radiusPill;
  int16_t statusBar;     // status bar height
  int16_t control;       // button / slider / toggle height
  int16_t row;           // list row height
  int16_t icon;          // standard icon box
  int16_t touchSlop;     // movement before a press becomes a drag
};

struct Theme {
  Palette color;
  Metrics m;
  fonts::FontSet font;
  Density density;
  SkyPhase phase;

  // Scale a compact-baseline length ("dp") to this density.
  constexpr int dp(int v) const { return (v * m.scale16 + 8) / 16; }
};

Density densityFor(int width, int height);
SkyPhase phaseForHour(int hour);  // 5-8 dawn, 8-17 day, 17-20 dusk, else night
bool resolveDark(Appearance appearance, SkyPhase phase);  // Auto: dark at dusk and night
Theme makeTheme(Density density, bool dark, SkyPhase phase);

const char *phaseName(SkyPhase phase);

}  // namespace stella
