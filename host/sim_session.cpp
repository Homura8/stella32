#include "sim_session.h"

#include <string.h>

namespace stella {
namespace host {

SimSession::SimSession(int width, int height, int64_t epoch, Appearance appearance) : dev(width, height) {
  if (epoch) dev.platform.setEpoch(epoch);
  config.renderBuffer = dev.buffer.data();
  config.renderBufferPixels = dev.buffer.size();
  config.defaults.appearance = appearance;
  strcpy(config.defaults.timezone, kMelbourneTz);
  config.defaults.dimAfterSec = 0;  // screenshots stay at full brightness
  os.begin(dev.board(), config);
  os.install(launcher);
  os.install(clock);
  os.install(settings);
  os.install(touchTest);
  os.start();
  run(100);
}

void SimSession::run(uint32_t ms) {
  uint32_t left = ms;
  for (;;) {
    uint32_t wait = os.step();
    if (left == 0) break;
    if (wait == 0) wait = 1;
    if (wait > left) wait = left;
    dev.platform.advance(wait);
    left -= wait;
  }
}

void SimSession::tap(int x, int y) {
  dev.touch.down(x, y);
  run(80);
  dev.touch.up();
  run(150);
}

void SimSession::longPress(int x, int y) {
  dev.touch.down(x, y);
  run(800);
  dev.touch.up();
  run(150);
}

void SimSession::drag(int x0, int y0, int x1, int y1) {
  dev.touch.down(x0, y0);
  run(40);
  for (int i = 1; i <= 8; ++i) {
    dev.touch.down(x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8);
    run(20);
  }
  dev.touch.up();
  run(150);
}

Point SimSession::settingsRow(int i) const {
  const Theme &t = os.theme();
  const Rect c = os.contentRect();
  return Point{(int16_t)c.centerX(), (int16_t)(c.y + t.m.gapS + i * t.m.row + t.m.row / 2)};
}

Point SimSession::backButton() const { return Point{(int16_t)(os.theme().m.margin + 6), (int16_t)(os.theme().m.statusBar / 2)}; }

}  // namespace host
}  // namespace stella
