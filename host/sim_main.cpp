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

using namespace stella;
using namespace stella::host;

namespace {

int failures = 0;

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
    s.os.launch("settings");
    s.run(200);
    save(3, "settings");
    const char *pages[] = {"display", "appearance", "time", "about"};
    for (int i = 0; i < 4; ++i) {
      const Point row = s.settingsRow(i);
      s.tap(row.x, row.y);
      save(4 + i, pages[i]);
      const Point back = s.backButton();
      s.tap(back.x, back.y);
    }
    s.os.launch("touch");
    s.run(100);
    const Rect c = s.os.contentRect();
    s.drag(c.x + c.w / 4, c.y + c.h / 3, c.x + c.w * 3 / 4, c.y + c.h / 2);
    s.os.toast("Settings saved");
    s.run(100);
    save(8, "touch-toast");
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
