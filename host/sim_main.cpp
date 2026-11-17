// Stella screenshot simulator: renders every built-in screen with the
// production renderer at several display sizes, in light and dark skies.
//
//   stella_sim <out-dir>      writes <out-dir>/<W>x<H>/<NN>-<name>.ppm
//
// tools/screenshots.py converts the PPMs to PNGs under docs/screenshots and
// checks them against the committed images in CI.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <string>

#include "sim_session.h"
#include "stella/app/text_entry.h"

using namespace stella;
using namespace stella::host;

namespace {

int failures = 0;

void tapRect(SimSession &s, const Rect &r) { s.tap(r.centerX(), r.centerY()); }

void shot(SimSession &s, const std::string &dir, const char *name) {
  const std::string path = dir + "/" + name + ".ppm";
  if (!s.dev.display.writePpm(path)) {
    fprintf(stderr, "cannot write %s\n", path.c_str());
    ++failures;
  }
}

void capture(int width, int height, const std::string &root) {
  char sizeName[32];
  snprintf(sizeName, sizeof(sizeName), "%dx%d", width, height);
  const std::string dir = root + "/" + sizeName;
  mkdir(dir.c_str(), 0755);

  for (int variant = 0; variant < 2; ++variant) {
    const bool evening = variant == 0;
    const char *suffix = evening ? "night" : "day";
    SimSession s(width, height, evening ? kEveningEpoch : kMorningEpoch);
    char name[64];
    auto save = [&](int index, const char *screen) {
      snprintf(name, sizeof(name), "%02d-%s-%s", index, screen, suffix);
      shot(s, dir, name);
    };
    save(1, "home");
    s.os.launch("clock");
    s.run(200);
    save(2, "clock");
    s.os.launch("weather");
    s.run(200);
    save(3, "weather");
    s.os.launch("settings");
    s.run(200);
    save(4, "settings");
    const char *pages[] = {"wifi", "display", "appearance", "time", "about"};
    for (int i = 0; i < 5; ++i) {
      const Point row = s.settingsRow(i);
      s.tap(row.x, row.y);
      s.run(600);  // Wi-Fi: let the scan finish
      save(5 + i, pages[i]);
      const Point back = s.backButton();
      s.tap(back.x, back.y);
    }
    s.os.launch("touch");
    s.run(100);
    const Rect c = s.os.contentRect();
    s.drag(c.x + c.w / 4, c.y + c.h / 3, c.x + c.w * 3 / 4, c.y + c.h / 2);
    s.os.toast("Settings saved");
    s.run(100);
    save(10, "touch-toast");

    // First run: no network saved yet; then the password keyboard.
    SimSession fresh(width, height, evening ? kEveningEpoch : kMorningEpoch, Appearance::Auto, false, false);
    auto saveFresh = [&](int index, const char *screen) {
      snprintf(name, sizeof(name), "%02d-%s-%s", index, screen, suffix);
      shot(fresh, dir, name);
    };
    saveFresh(11, "first-run");
    fresh.os.launch("settings", "wifi");
    fresh.run(600);
    const Theme &t = fresh.os.theme();
    const Rect fc = fresh.os.contentRect();
    const int listTop = fc.y + t.m.gapS + (t.m.row + t.dp(4)) + t.m.gapS + (t.m.control - t.dp(8)) + t.m.gapS + t.dp(4);
    fresh.tap(fc.centerX(), listTop + t.dp(16));
    for (const char *p = "moon"; *p; ++p) {
      const Keyboard &kb = static_cast<TextEntryScreen *>(fresh.os.topScreen())->keyboard();
      const Rect key = kb.keyFor(*p);
      fresh.tap(key.centerX(), key.centerY());
    }
    saveFresh(12, "password");

    // Alarms, the alarm editor, a running timer and the alert it raises.
    s.os.launch("alarms");
    s.run(100);
    apps::AlarmEntry weekday;
    weekday.hour = 6;
    weekday.minute = 45;
    weekday.days = apps::kWeekdays;
    s.alarms.setAlarm(-1, weekday, s.os);
    apps::AlarmEntry weekend;
    weekend.hour = 9;
    weekend.minute = 30;
    weekend.days = apps::kWeekend;
    weekend.enabled = 0;
    s.alarms.setAlarm(-1, weekend, s.os);
    s.run(2500);  // let the earlier toast fade
    save(13, "alarms");
    const Theme &st = s.os.theme();
    const Rect sc = s.os.contentRect();
    const int firstRow = sc.y + st.m.gapS + st.m.control + st.m.gapM + st.dp(4) + (st.m.row + st.dp(8)) / 2;
    s.tap(sc.x + sc.w / 3, firstRow);
    s.run(2500);
    save(14, "alarm-edit");
    s.os.goHome();
    s.os.launch("timer");
    s.run(100);
    const int bottom = sc.bottom() - st.m.gapS;
    s.tap(sc.right() - st.m.margin - sc.w / 6, bottom - st.m.control / 2);  // Start (5 min)
    s.run(83 * 1000);
    save(15, "timer");
    s.os.launch("clock");
    s.run(5 * 60 * 1000);
    save(16, "alert");
    tapRect(s, s.os.alertButtonRect(0));

    // Display calibration (both tabs) and touch calibration.
    s.os.launch("settings");
    s.run(100);
    const Point display = s.settingsRow(1);
    s.tap(display.x, display.y);
    const Rect area = sc.inset(st.m.margin, st.m.gapS);
    const int want = st.m.control + st.dp(4), fit = (area.h - 2 * st.dp(4)) / 5;
    const int rowH = want <= fit ? want : fit < st.dp(32) ? st.dp(32) : fit;
    s.tap(area.centerX(), area.y + st.dp(4) + 4 * rowH + rowH / 2);
    s.run(2500);
    save(17, "calibrate-colour");
    s.tap(area.right() - area.w / 4, area.y + st.m.control / 2);
    s.run(100);
    save(18, "calibrate-flicker");
    s.os.goHome();
    s.os.launch("touch");
    s.run(100);
    s.tap(area.right() - st.dp(52), area.bottom() - st.m.control / 2);
    s.run(100);
    save(19, "touch-calibrate");
  }
}

}  // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : "screenshots";
  mkdir(root.c_str(), 0755);
  capture(320, 240, root);
  capture(480, 320, root);
  if (failures) return 1;
  printf("screenshots written to %s\n", root.c_str());
  return 0;
}
