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
  static constexpr uint16_t kVersion = 1;

  Appearance appearance = Appearance::Auto;
  uint8_t brightness = 200;      // 16..255
  uint16_t dimAfterSec = 60;     // 0 = never
  uint16_t sleepAfterSec = 0;    // backlight off; 0 = never
  bool clock24h = true;
  char timezone[48] = "UTC0";    // POSIX TZ string

  // Clamp every field into its valid range (also used after loading).
  void sanitize();
};

struct TimezonePreset {
  const char *label;
  const char *posix;
};
// Small built-in list for the Settings app.
const TimezonePreset *timezonePresets(int &count);

RecordStatus loadSettings(KeyValueStore *store, SystemSettings &out);
bool saveSettings(KeyValueStore *store, const SystemSettings &settings);

}  // namespace stella
