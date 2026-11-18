#include "changelog.h"

namespace stella {
namespace apps {

namespace {
// Newest first. Add an entry when STELLA_VERSION goes up; a test checks the
// top one matches the running version.
const ChangelogEntry kChangelog[] = {
    {"0.15.0", {"Values: live numbers", "Sent by a script on your Wi-Fi", "See Settings, Remote page", nullptr}},
    {"0.13.0",
     {"Units: \xc2\xb0" "C/\xc2\xb0" "F, week, date", "Idle face: clock or dashboard",
      "Sunrise/sunset on bedside", nullptr}},
    {"0.12.0", {"A web page on your Wi-Fi", "Wallpaper from your phone", "Settings, then Remote page", nullptr}},
    {"0.11.1", {"Weather works again", "(0.9.0 to 0.11.0 sent a", "request that was cut short)", nullptr}},
    {"0.11.0", {"The sky follows the real sun", "Dimming and bedside follow it", nullptr}},
    {"0.10.0",
     {"A changelog after each update", "Choose and order Home apps",
      "Long-press the time to edit", nullptr}},
    {"0.9.0", {"The next 12 hours of weather", "Rain outlook: 'Rain ~3 pm'", "Tap the rain chip for hours", nullptr}},
    {"0.8.4", {"A bigger time on Home", nullptr}},
    {"0.8.3", {"Sunrise and sunset on Weather", nullptr}},
    {"0.8.2", {"The OS is now called StellaOS", "Time moves to Home's header", "Even spacing on Home", nullptr}},
    {"0.8.1", {"A brighter LED for alarms", nullptr}},
    {"0.8.0",
     {"Status LED: alarms, updates", "Find my LED, in Sound & light", "Light sensor: dims in the dark",
      nullptr}},
    {"0.7.1", {"Kinder updates, clearer errors", nullptr}},
    {"0.6.1",
     {"Daily check for updates", "Volume setting", "Stopwatch", "Faster phone Wi-Fi setup", nullptr}},
};
}  // namespace

const ChangelogEntry *changelog(int &count) {
  count = (int)(sizeof(kChangelog) / sizeof(kChangelog[0]));
  return kChangelog;
}

}  // namespace apps
}  // namespace stella
