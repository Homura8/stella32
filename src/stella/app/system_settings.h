#pragma once
// System settings: persisted as one versioned record ("sys"/"settings").
// Apps read and change them through AppContext::settings() and
// AppContext::applySettings(); the runtime persists changes after a short
// debounce (a slider drag must not write flash dozens of times).
#include <stdint.h>

#include "../ui/theme.h"
#include "../storage/record.h"

namespace stella {

struct SystemSettings {
  // v2 added nightDim, v3 colorTone, v4 the display and touch calibration,
  // v5 per-mode contrast, white level and warmth. Older records are migrated
  // by loadSettings().
  static constexpr uint16_t kVersion = 5;

  Appearance appearance = Appearance::Auto;
  uint8_t brightness = 200;      // 16..255, perceived (see backlightDuty)
  uint16_t dimAfterSec = 60;     // 0 = never
  uint16_t sleepAfterSec = 0;    // backlight off; 0 = never
  bool clock24h = true;
  char timezone[48] = "UTC0";    // POSIX TZ string
  bool nightDim = true;          // dim the backlight during the night phase
  ColorTone colorTone = ColorTone::Soft;  // palette strength for this panel
  // Display calibration (boards supply their own defaults; see CYD.ino).
  uint8_t contrastLight = 0;     // 0..100, see Look::contrast (light mode)
  uint8_t contrastDark = 0;      // ... and dark mode
  uint8_t whiteLevel = 100;      // 0..100, see Look::whites
  uint8_t warmth = 0;            // 0..100, see Look::warmth
  bool dither = true;            // smooth sky gradients
  int8_t panelVcom = 0;          // Display::PanelTrim::vcom
  uint8_t panelRefreshHz = 0;    // Display::PanelTrim::refreshHz (0 = default)
  uint8_t skyPin = 0;            // 0 = the sky follows the time; 1..4 = dawn..night
  // Touch correction applied to every touch point: x' = x * scaleX / 1024 + offsetX.
  int16_t touchScaleX = 1024, touchOffsetX = 0, touchScaleY = 1024, touchOffsetY = 0;

  Look look(bool dark) const {
    Look l;
    l.tone = colorTone;
    l.contrast = dark ? contrastDark : contrastLight;
    l.whites = whiteLevel;
    l.warmth = warmth;
    l.dither = dither;
    return l;
  }

  // Clamp every field into its valid range (also used after loading).
  void sanitize();
};

struct TimezonePreset {
  const char *label;
  const char *posix;
};
// Small built-in list for the Settings app.
const TimezonePreset *timezonePresets(int &count);

// Perceived brightness actually used for `level` when night dimming applies:
// a fraction of it, so the slider keeps working at night.
uint8_t nightBrightness(uint8_t level);
// Perceived level (0..255) -> backlight PWM duty (0..255). LEDs look far
// brighter than their duty cycle, so a power curve makes the slider even.
uint8_t backlightDuty(uint8_t level);

// Loads and sanitizes; migrates older versions (the result is then Ok and the
// caller should save it back). `out` holds the defaults on entry: migrated
// records keep them for fields they did not have.
RecordStatus loadSettings(KeyValueStore *store, SystemSettings &out, bool *migrated = nullptr);
bool saveSettings(KeyValueStore *store, const SystemSettings &settings);

}  // namespace stella
