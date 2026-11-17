#include "system_settings.h"

#include <math.h>
#include <string.h>

namespace stella {
namespace {

constexpr const char *kNamespace = "sys";
constexpr const char *kKey = "settings";

const TimezonePreset kPresets[] = {
    {"UTC", "UTC0"},
    {"London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Berlin / Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"New York", "EST5EDT,M3.2.0,M11.1.0"},
    {"Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"Tokyo", "JST-9"},
    {"Singapore", "<+08>-8"},
    {"Melbourne / Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Brisbane", "AEST-10"},
    {"Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};

}  // namespace

void SystemSettings::sanitize() {
  if ((uint8_t)appearance > (uint8_t)Appearance::Dark) appearance = Appearance::Auto;
  if (brightness < 16) brightness = 16;
  if (dimAfterSec > 3600) dimAfterSec = 3600;
  if (sleepAfterSec > 7200) sleepAfterSec = 7200;
  timezone[sizeof(timezone) - 1] = '\0';
  if (!timezone[0]) strcpy(timezone, "UTC0");
  if ((uint8_t)colorTone > (uint8_t)ColorTone::Vivid) colorTone = ColorTone::Soft;
  if (contrastLight > 100) contrastLight = 100;
  if (contrastDark > 100) contrastDark = 100;
  if (whiteLevel > 100) whiteLevel = 100;
  if (warmth > 100) warmth = 100;
  if (panelVcom < -30) panelVcom = -30;
  if (panelVcom > 30) panelVcom = 30;
  if (panelRefreshHz != 0 && (panelRefreshHz < 60 || panelRefreshHz > 120)) panelRefreshHz = 0;
  if (skyPin > 4) skyPin = 0;
  if (bedside > 2) bedside = 1;
  if (volume > 100) volume = 100;
  if (ledMode > 2) ledMode = 1;
  if (ledLevel < 5) ledLevel = 5;
  if (ledLevel > 100) ledLevel = 100;
  for (uint8_t &pin : ledPin)
    if (pin > 39 && pin != 255) pin = 255;  // not a GPIO
  if (lightDark > 4095) lightDark = 4095;
  if (lightBright > 4095) lightBright = 4095;
  if (!lightCalibrated()) autoBrightness = false;
  // A correction outside these bounds is a failed calibration: undo it.
  if (touchScaleX < 819 || touchScaleX > 1280 || touchScaleY < 819 || touchScaleY > 1280 || touchOffsetX < -60 ||
      touchOffsetX > 60 || touchOffsetY < -60 || touchOffsetY > 60) {
    touchScaleX = touchScaleY = 1024;
    touchOffsetX = touchOffsetY = 0;
  }
}

const TimezonePreset *timezonePresets(int &count) {
  count = (int)(sizeof(kPresets) / sizeof(kPresets[0]));
  return kPresets;
}

// Frozen layout of version 1 (Stella 0.1.0). Never edit: it describes data
// already stored on devices.
struct SettingsV1 {
  Appearance appearance;
  uint8_t brightness;
  uint16_t dimAfterSec;
  uint16_t sleepAfterSec;
  bool clock24h;
  char timezone[48];
};

// Frozen layout of version 2 (Stella 0.2.0 - 0.3.0). Never edit.
struct SettingsV2 {
  Appearance appearance;
  uint8_t brightness;
  uint16_t dimAfterSec;
  uint16_t sleepAfterSec;
  bool clock24h;
  char timezone[48];
  bool nightDim;
};

// Frozen layout of version 3 (the 0.3.1 branch). Never edit.
struct SettingsV3 {
  Appearance appearance;
  uint8_t brightness;
  uint16_t dimAfterSec;
  uint16_t sleepAfterSec;
  bool clock24h;
  char timezone[48];
  bool nightDim;
  ColorTone colorTone;
};

// Frozen layout of version 4 (the 0.3.1 branch, second build). Never edit.
struct SettingsV4 {
  Appearance appearance;
  uint8_t brightness;
  uint16_t dimAfterSec;
  uint16_t sleepAfterSec;
  bool clock24h;
  char timezone[48];
  bool nightDim;
  ColorTone colorTone;
  uint8_t contrast;
  bool dither;
  int8_t panelVcom;
  uint8_t panelRefreshHz;
  uint8_t skyPin;
  int16_t touchScaleX, touchOffsetX, touchScaleY, touchOffsetY;
};

// Frozen layout of version 5 (0.3.2). Never edit.
struct SettingsV5 {
  Appearance appearance;
  uint8_t brightness;
  uint16_t dimAfterSec;
  uint16_t sleepAfterSec;
  bool clock24h;
  char timezone[48];
  bool nightDim;
  ColorTone colorTone;
  uint8_t contrastLight, contrastDark, whiteLevel, warmth;
  bool dither;
  int8_t panelVcom;
  uint8_t panelRefreshHz;
  uint8_t skyPin;
  int16_t touchScaleX, touchOffsetX, touchScaleY, touchOffsetY;
};

// Frozen layout of version 8 (0.7.0). Never edit.
struct SettingsV8 {
  Appearance appearance;
  uint8_t brightness;
  uint16_t dimAfterSec;
  uint16_t sleepAfterSec;
  bool clock24h;
  char timezone[48];
  bool nightDim;
  uint8_t bedside;
  ColorTone colorTone;
  uint8_t contrastLight, contrastDark, whiteLevel, warmth;
  bool dither;
  int8_t panelVcom;
  uint8_t panelRefreshHz;
  uint8_t skyPin;
  int16_t touchScaleX, touchOffsetX, touchScaleY, touchOffsetY;
  uint8_t volume;
  bool autoUpdate;
  bool animations;
};

// Frozen layout of version 7 (0.6.1). Never edit.
struct SettingsV7 {
  Appearance appearance;
  uint8_t brightness;
  uint16_t dimAfterSec;
  uint16_t sleepAfterSec;
  bool clock24h;
  char timezone[48];
  bool nightDim;
  uint8_t bedside;
  ColorTone colorTone;
  uint8_t contrastLight, contrastDark, whiteLevel, warmth;
  bool dither;
  int8_t panelVcom;
  uint8_t panelRefreshHz;
  uint8_t skyPin;
  int16_t touchScaleX, touchOffsetX, touchScaleY, touchOffsetY;
  uint8_t volume;
  bool autoUpdate;
};

// Frozen layout of version 6 (0.4.0 - 0.6.0). Never edit.
struct SettingsV6 {
  Appearance appearance;
  uint8_t brightness;
  uint16_t dimAfterSec;
  uint16_t sleepAfterSec;
  bool clock24h;
  char timezone[48];
  bool nightDim;
  uint8_t bedside;
  ColorTone colorTone;
  uint8_t contrastLight, contrastDark, whiteLevel, warmth;
  bool dither;
  int8_t panelVcom;
  uint8_t panelRefreshHz;
  uint8_t skyPin;
  int16_t touchScaleX, touchOffsetX, touchScaleY, touchOffsetY;
};

uint8_t nightBrightness(uint8_t level) {
  const int v = level * 45 / 100;
  return (uint8_t)(v < 16 ? (level < 16 ? level : 16) : v);
}

uint8_t backlightDuty(uint8_t level) {
  if (level == 0) return 0;
  // Gamma 2.2 with a floor of 2: the dimmest step stays visible.
  const float duty = 2.f + 253.f * powf(level / 255.f, 2.2f);
  return (uint8_t)(duty > 255.f ? 255.f : duty + 0.5f);
}

RecordStatus loadSettings(KeyValueStore *store, SystemSettings &out, bool *migrated) {
  if (migrated) *migrated = false;
  SystemSettings loaded = out;  // defaults for anything an old record lacks
  uint16_t found = 0;
  RecordStatus s = loadRecord(store, kNamespace, kKey, SystemSettings::kVersion, &loaded, sizeof(loaded), &found);
  if (s == RecordStatus::OldVersion && found == 1) {
    SettingsV1 v1;
    s = loadRecord(store, kNamespace, kKey, 1, &v1, sizeof(v1));
    if (s == RecordStatus::Ok) {
      loaded = out;
      loaded.appearance = v1.appearance;
      loaded.brightness = v1.brightness;
      loaded.dimAfterSec = v1.dimAfterSec;
      loaded.sleepAfterSec = v1.sleepAfterSec;
      loaded.clock24h = v1.clock24h;
      memcpy(loaded.timezone, v1.timezone, sizeof(loaded.timezone));
      if (migrated) *migrated = true;
    }
  } else if (s == RecordStatus::OldVersion && found == 2) {
    SettingsV2 v2;
    s = loadRecord(store, kNamespace, kKey, 2, &v2, sizeof(v2));
    if (s == RecordStatus::Ok) {
      loaded = out;
      loaded.appearance = v2.appearance;
      loaded.brightness = v2.brightness;
      loaded.dimAfterSec = v2.dimAfterSec;
      loaded.sleepAfterSec = v2.sleepAfterSec;
      loaded.clock24h = v2.clock24h;
      memcpy(loaded.timezone, v2.timezone, sizeof(loaded.timezone));
      loaded.nightDim = v2.nightDim;
      if (migrated) *migrated = true;
    }
  } else if (s == RecordStatus::OldVersion && found == 3) {
    SettingsV3 v3;
    s = loadRecord(store, kNamespace, kKey, 3, &v3, sizeof(v3));
    if (s == RecordStatus::Ok) {
      loaded = out;
      loaded.appearance = v3.appearance;
      loaded.brightness = v3.brightness;
      loaded.dimAfterSec = v3.dimAfterSec;
      loaded.sleepAfterSec = v3.sleepAfterSec;
      loaded.clock24h = v3.clock24h;
      memcpy(loaded.timezone, v3.timezone, sizeof(loaded.timezone));
      loaded.nightDim = v3.nightDim;
      loaded.colorTone = v3.colorTone;
      if (migrated) *migrated = true;
    }
  } else if (s == RecordStatus::OldVersion && found == 4) {
    SettingsV4 v4;
    s = loadRecord(store, kNamespace, kKey, 4, &v4, sizeof(v4));
    if (s == RecordStatus::Ok) {
      loaded = out;
      loaded.appearance = v4.appearance;
      loaded.brightness = v4.brightness;
      loaded.dimAfterSec = v4.dimAfterSec;
      loaded.sleepAfterSec = v4.sleepAfterSec;
      loaded.clock24h = v4.clock24h;
      memcpy(loaded.timezone, v4.timezone, sizeof(loaded.timezone));
      loaded.nightDim = v4.nightDim;
      loaded.colorTone = v4.colorTone;
      // v4's single contrast also darkened light inks in dark mode; v5's
      // per-mode contrast means something different, so it starts from the
      // board's defaults rather than the old value.
      loaded.dither = v4.dither;
      loaded.panelVcom = v4.panelVcom;
      loaded.panelRefreshHz = v4.panelRefreshHz;
      loaded.skyPin = v4.skyPin;
      loaded.touchScaleX = v4.touchScaleX;
      loaded.touchOffsetX = v4.touchOffsetX;
      loaded.touchScaleY = v4.touchScaleY;
      loaded.touchOffsetY = v4.touchOffsetY;
      if (migrated) *migrated = true;
    }
  } else if (s == RecordStatus::OldVersion && found == 5) {
    SettingsV5 v5;
    s = loadRecord(store, kNamespace, kKey, 5, &v5, sizeof(v5));
    if (s == RecordStatus::Ok) {
      loaded = out;
      loaded.appearance = v5.appearance;
      loaded.brightness = v5.brightness;
      loaded.dimAfterSec = v5.dimAfterSec;
      loaded.sleepAfterSec = v5.sleepAfterSec;
      loaded.clock24h = v5.clock24h;
      memcpy(loaded.timezone, v5.timezone, sizeof(loaded.timezone));
      loaded.nightDim = v5.nightDim;
      loaded.colorTone = v5.colorTone;
      loaded.contrastLight = v5.contrastLight;
      loaded.contrastDark = v5.contrastDark;
      loaded.whiteLevel = v5.whiteLevel;
      loaded.warmth = v5.warmth;
      loaded.dither = v5.dither;
      loaded.panelVcom = v5.panelVcom;
      loaded.panelRefreshHz = v5.panelRefreshHz;
      loaded.skyPin = v5.skyPin;
      loaded.touchScaleX = v5.touchScaleX;
      loaded.touchOffsetX = v5.touchOffsetX;
      loaded.touchScaleY = v5.touchScaleY;
      loaded.touchOffsetY = v5.touchOffsetY;
      if (migrated) *migrated = true;  // bedside keeps the board's default
    }
  } else if (s == RecordStatus::OldVersion && found == 6) {
    SettingsV6 v6;
    s = loadRecord(store, kNamespace, kKey, 6, &v6, sizeof(v6));
    if (s == RecordStatus::Ok) {
      loaded = out;
      loaded.appearance = v6.appearance;
      loaded.brightness = v6.brightness;
      loaded.dimAfterSec = v6.dimAfterSec;
      loaded.sleepAfterSec = v6.sleepAfterSec;
      loaded.clock24h = v6.clock24h;
      memcpy(loaded.timezone, v6.timezone, sizeof(loaded.timezone));
      loaded.nightDim = v6.nightDim;
      loaded.bedside = v6.bedside;
      loaded.colorTone = v6.colorTone;
      loaded.contrastLight = v6.contrastLight;
      loaded.contrastDark = v6.contrastDark;
      loaded.whiteLevel = v6.whiteLevel;
      loaded.warmth = v6.warmth;
      loaded.dither = v6.dither;
      loaded.panelVcom = v6.panelVcom;
      loaded.panelRefreshHz = v6.panelRefreshHz;
      loaded.skyPin = v6.skyPin;
      loaded.touchScaleX = v6.touchScaleX;
      loaded.touchOffsetX = v6.touchOffsetX;
      loaded.touchScaleY = v6.touchScaleY;
      loaded.touchOffsetY = v6.touchOffsetY;
      if (migrated) *migrated = true;  // volume and autoUpdate keep their defaults
    }
  } else if (s == RecordStatus::OldVersion && found == 7) {
    SettingsV7 v7;
    s = loadRecord(store, kNamespace, kKey, 7, &v7, sizeof(v7));
    if (s == RecordStatus::Ok) {
      loaded = out;
      loaded.appearance = v7.appearance;
      loaded.brightness = v7.brightness;
      loaded.dimAfterSec = v7.dimAfterSec;
      loaded.sleepAfterSec = v7.sleepAfterSec;
      loaded.clock24h = v7.clock24h;
      memcpy(loaded.timezone, v7.timezone, sizeof(loaded.timezone));
      loaded.nightDim = v7.nightDim;
      loaded.bedside = v7.bedside;
      loaded.colorTone = v7.colorTone;
      loaded.contrastLight = v7.contrastLight;
      loaded.contrastDark = v7.contrastDark;
      loaded.whiteLevel = v7.whiteLevel;
      loaded.warmth = v7.warmth;
      loaded.dither = v7.dither;
      loaded.panelVcom = v7.panelVcom;
      loaded.panelRefreshHz = v7.panelRefreshHz;
      loaded.skyPin = v7.skyPin;
      loaded.touchScaleX = v7.touchScaleX;
      loaded.touchOffsetX = v7.touchOffsetX;
      loaded.touchScaleY = v7.touchScaleY;
      loaded.touchOffsetY = v7.touchOffsetY;
      loaded.volume = v7.volume;
      loaded.autoUpdate = v7.autoUpdate;
      if (migrated) *migrated = true;  // nothing else to carry over
    }
  } else if (s == RecordStatus::OldVersion && found == 8) {
    SettingsV8 v8;
    s = loadRecord(store, kNamespace, kKey, 8, &v8, sizeof(v8));
    if (s == RecordStatus::Ok) {
      loaded = out;  // the LED and light fields keep the board's defaults
      loaded.appearance = v8.appearance;
      loaded.brightness = v8.brightness;
      loaded.dimAfterSec = v8.dimAfterSec;
      loaded.sleepAfterSec = v8.sleepAfterSec;
      loaded.clock24h = v8.clock24h;
      memcpy(loaded.timezone, v8.timezone, sizeof(loaded.timezone));
      loaded.nightDim = v8.nightDim;
      loaded.bedside = v8.bedside;
      loaded.colorTone = v8.colorTone;
      loaded.contrastLight = v8.contrastLight;
      loaded.contrastDark = v8.contrastDark;
      loaded.whiteLevel = v8.whiteLevel;
      loaded.warmth = v8.warmth;
      loaded.dither = v8.dither;
      loaded.panelVcom = v8.panelVcom;
      loaded.panelRefreshHz = v8.panelRefreshHz;
      loaded.skyPin = v8.skyPin;
      loaded.touchScaleX = v8.touchScaleX;
      loaded.touchOffsetX = v8.touchOffsetX;
      loaded.touchScaleY = v8.touchScaleY;
      loaded.touchOffsetY = v8.touchOffsetY;
      loaded.volume = v8.volume;
      loaded.autoUpdate = v8.autoUpdate;
      if (migrated) *migrated = true;
    }
  }
  if (s == RecordStatus::Ok) {
    out = loaded;
    out.sanitize();
  }
  return s;
}

bool saveSettings(KeyValueStore *store, const SystemSettings &settings) {
  return saveRecord(store, kNamespace, kKey, SystemSettings::kVersion, &settings, sizeof(settings));
}

}  // namespace stella
