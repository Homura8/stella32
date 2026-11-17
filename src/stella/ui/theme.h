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
// How strongly the palette is rendered. Soft is the Celestia Glow palette as
// designed (for good IPS screens); Rich and Vivid deepen the sky toward each
// phase's own colour, raise saturation and strengthen edges and shadows, for
// small TN panels (like the CYD's) that wash pastels out to white. Text
// colours never change.
enum class ColorTone : uint8_t { Soft = 0, Rich = 1, Vivid = 2 };

// How the palette is rendered on a particular panel (calibrated on the device
// in Settings > Display > Calibrate). The defaults render the design exactly.
struct Look {
  ColorTone tone = ColorTone::Soft;
  // 0..100: a gamma (1.0 at 0, 2.2 at 100) that deepens backgrounds and, in
  // light mode, dark text. Panels that lift their dark tones (light-lilac
  // text, grey "dark" mode) need some; good IPS screens need none. In dark
  // mode light inks (text, accents, toggles) keep their brightness.
  uint8_t contrast = 0;
  // 0..100: brightness of the lightest tones. Below 100 the tones above a
  // knee are compressed, so pure white does not glare next to near-whites.
  uint8_t whites = 100;
  uint8_t warmth = 0;  // 0..100: trims blue (and a little green) on cold panels
  bool dither = true;  // ordered dithering of the sky gradient
};

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
  // The bedside clock: a black field (the backlight is low, so black stays
  // calm), a lilac clock, dimmer lines and faint stars.
  Color bedBg, bedClock, bedText, bedMuted, bedStar;
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
  Look look;

  // Scale a compact-baseline length ("dp") to this density.
  constexpr int dp(int v) const { return (v * m.scale16 + 8) / 16; }
};

Density densityFor(int width, int height);
SkyPhase phaseForHour(int hour);  // 5-8 dawn, 8-17 day, 17-20 dusk, else night
bool resolveDark(Appearance appearance, SkyPhase phase);  // Auto: dark at dusk and night
Theme makeTheme(Density density, bool dark, SkyPhase phase, const Look &look = Look{});

const char *phaseName(SkyPhase phase);
const char *toneName(ColorTone tone);
// A design colour (e.g. a calibration grey) as this look renders it.
Color lookColor(const Look &look, uint32_t hex);

}  // namespace stella
