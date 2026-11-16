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

RecordStatus loadSettings(KeyValueStore *store, SystemSettings &out) {
  SystemSettings loaded;
  const RecordStatus s = loadRecord(store, kNamespace, kKey, SystemSettings::kVersion, &loaded, sizeof(loaded));
  if (s == RecordStatus::Ok) {
    out = loaded;
    out.sanitize();
  }
  // Older versions: no migration exists yet (v1 is the first layout).
  return s;
}

bool saveSettings(KeyValueStore *store, const SystemSettings &settings) {
  return saveRecord(store, kNamespace, kKey, SystemSettings::kVersion, &settings, sizeof(settings));
}

}  // namespace stella
