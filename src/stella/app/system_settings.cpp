#include "system_settings.h"

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

uint8_t nightBrightness(uint8_t level) { return level < 40 ? level : 40; }

RecordStatus loadSettings(KeyValueStore *store, SystemSettings &out, bool *migrated) {
  if (migrated) *migrated = false;
  SystemSettings loaded;
  uint16_t found = 0;
  RecordStatus s = loadRecord(store, kNamespace, kKey, SystemSettings::kVersion, &loaded, sizeof(loaded), &found);
  if (s == RecordStatus::OldVersion && found == 1) {
    SettingsV1 v1;
    s = loadRecord(store, kNamespace, kKey, 1, &v1, sizeof(v1));
    if (s == RecordStatus::Ok) {
      loaded = SystemSettings{};
      loaded.appearance = v1.appearance;
      loaded.brightness = v1.brightness;
      loaded.dimAfterSec = v1.dimAfterSec;
      loaded.sleepAfterSec = v1.sleepAfterSec;
      loaded.clock24h = v1.clock24h;
      memcpy(loaded.timezone, v1.timezone, sizeof(loaded.timezone));
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
