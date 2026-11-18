// Settings > Reset: everything saved is erased and the device restarts as new.
#include <string.h>

#include "sim_session.h"
#include "stella/storage/record.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {
void tapRect(SimSession &s, const Rect &r) { s.tap(r.centerX(), r.centerY()); }
const char *topTitle(SimSession &s) { return s.os.topScreen() && s.os.topScreen()->title() ? s.os.topScreen()->title() : ""; }
}  // namespace

TEST(reset_is_the_last_row_of_settings_and_asks_before_it_erases_anything) {
  SimSession s(320, 240);
  s.os.launch("settings");
  s.run(200);
  CHECK(strcmp(s.os.foreground()->info().id, "settings") == 0 && strcmp(topTitle(s), "Reset") != 0);
  // The list scrolls; its last row is Reset and opens the question.
  const Rect c = s.os.contentRect();
  for (int i = 0; i < 3; ++i) s.drag(c.centerX(), c.bottom() - 10, c.centerX(), c.y + 10);
  s.run(300);
  s.tap(c.centerX(), c.bottom() - s.os.theme().dp(20));
  CHECK(strcmp(topTitle(s), "Reset") == 0);
  CHECK(s.dev.storage.count() > 0);  // the sim device has saved something by now
  CHECK_EQ(s.dev.storage.eraseAlls(), 0);  // opening the question erases nothing
  CHECK_EQ(s.dev.platform.restarts(), 0);
}

TEST(keeping_everything_leaves_the_device_as_it_was) {
  SimSession s(320, 240);
  s.os.launch("settings", "reset");
  s.run(200);
  const size_t before = s.dev.storage.count();
  tapRect(s, s.settings.resetCancelRect());
  s.run(1500);
  CHECK(strcmp(topTitle(s), "Reset") != 0);  // back on the list
  CHECK_EQ(s.dev.storage.eraseAlls(), 0);
  CHECK_EQ(s.dev.platform.restarts(), 0);
  CHECK_EQ((int)s.dev.storage.count(), (int)before);
}

TEST(erasing_removes_everything_saved_including_the_wallpaper_and_restarts) {
  SimSession s(320, 240);
  uint8_t marker = 7;
  CHECK(saveRecord(&s.dev.storage, "alarms", "marker", 1, &marker, sizeof(marker)));
  s.os.launch("settings", "reset");
  s.run(200);
  tapRect(s, s.settings.resetEraseRect());
  s.run(300);
  CHECK(s.settings.resetErasing());  // it says so, and the buttons are inert
  CHECK_EQ(s.dev.platform.restarts(), 0);  // the screen has a moment to say it
  s.run(1500);
  CHECK_EQ(s.dev.storage.eraseAlls(), 1);
  CHECK_EQ((int)s.dev.storage.count(), 0);
  CHECK_EQ(s.dev.wallpaper.erases, 1);
  CHECK_EQ(s.dev.platform.restarts(), 1);
  uint8_t back = 0;
  CHECK(loadRecord(&s.dev.storage, "alarms", "marker", 1, &back, sizeof(back)) != RecordStatus::Ok);
}
