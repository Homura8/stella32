// Stopwatch: the arithmetic, laps, running in the background, and the screen
// by touch at every size (incremental rendering equals a full redraw).
#include <string.h>

#include "sim_session.h"
#include "stella/ui/layout.h"
#include "test.h"

using namespace stella;
using namespace stella::host;
using stella::apps::StopwatchApp;

namespace {

// The bottom row: Lap or Reset | Start, Stop or Resume.
Rect swButton(SimSession &s, int index) {
  const Theme &t = s.os.theme();
  const Rect area = s.os.contentRect().inset(t.m.margin, t.m.gapS);
  const Rect row(area.x, area.bottom() - t.m.control, area.w, t.m.control);
  Rect b[2];
  layout::row(row, {layout::flex(), layout::flex(2)}, t.m.gapM, b);
  return b[index];
}

void tapRect(SimSession &s, const Rect &r) { s.tap(r.centerX(), r.centerY()); }

// While it counts, a repaint 20 ms later must not straddle a tenth: wait for a
// calm spot in the current tenth first.
void settle(SimSession &s) {
  for (int i = 0; i < 40 && s.stopwatch.running(); ++i) {
    const uint32_t phase = s.stopwatch.elapsed(s.os.millis()) % 100;
    if (phase >= 10 && phase <= 70) break;
    s.run(5);
  }
}

bool sameAsFullRedraw(SimSession &s) {
  settle(s);
  const std::vector<Color> incremental = s.dev.display.framebuffer();
  s.os.invalidateAll();
  s.run(20);
  return incremental == s.dev.display.framebuffer();
}

}  // namespace

TEST(stopwatch_formats_times) {
  char out[16];
  StopwatchApp::format(0, true, out, sizeof(out));
  CHECK(strcmp(out, "00:00.0") == 0);
  StopwatchApp::format(83456, true, out, sizeof(out));
  CHECK(strcmp(out, "01:23.4") == 0);
  StopwatchApp::format(83456, false, out, sizeof(out));
  CHECK(strcmp(out, "01:23") == 0);
  StopwatchApp::format(59999, true, out, sizeof(out));
  CHECK(strcmp(out, "00:59.9") == 0);
  StopwatchApp::format(3600000, true, out, sizeof(out));  // an hour: whole seconds
  CHECK(strcmp(out, "1:00:00") == 0);
  StopwatchApp::format(StopwatchApp::kMaxMs, true, out, sizeof(out));
  CHECK(strcmp(out, "99:59:59") == 0);
}

TEST(stopwatch_counts_stops_resumes_and_keeps_running_in_the_background) {
  SimSession s(320, 240);
  s.os.launch("stopwatch");
  s.run(100);
  CHECK(!s.stopwatch.running() && s.stopwatch.elapsed(s.os.millis()) == 0);
  tapRect(s, swButton(s, 1));  // Start
  CHECK(s.stopwatch.running());
  const uint32_t t0 = s.os.millis();
  s.run(3000);
  s.os.launch("clock");  // out of sight, still counting
  s.run(4000);
  const uint32_t e = s.stopwatch.elapsed(s.os.millis());
  CHECK(e >= 7000 && e <= 7000 + 500);
  (void)t0;
  s.os.launch("stopwatch");
  s.run(100);
  tapRect(s, swButton(s, 1));  // Stop
  CHECK(!s.stopwatch.running());
  const uint32_t stopped = s.stopwatch.elapsed(s.os.millis());
  s.run(5000);
  CHECK_EQ(s.stopwatch.elapsed(s.os.millis()), stopped);  // held
  tapRect(s, swButton(s, 1));  // Resume
  s.run(2000);
  CHECK(s.stopwatch.elapsed(s.os.millis()) >= stopped + 2000);
}

TEST(stopwatch_laps_are_recorded_newest_first_and_reset_clears_them) {
  SimSession s(320, 240);
  s.os.launch("stopwatch");
  s.run(100);
  tapRect(s, swButton(s, 1));  // Start
  s.run(1000);
  tapRect(s, swButton(s, 0));  // Lap
  s.run(2000);
  tapRect(s, swButton(s, 0));  // Lap
  s.run(500);
  tapRect(s, swButton(s, 0));  // Lap
  CHECK_EQ(s.stopwatch.lapCount(), 3);
  CHECK(s.stopwatch.lapTotal(0) < s.stopwatch.lapTotal(1) && s.stopwatch.lapTotal(1) < s.stopwatch.lapTotal(2));
  CHECK(s.stopwatch.lapTime(1) > s.stopwatch.lapTime(2));  // the second lap was the longer
  CHECK_EQ(s.stopwatch.lapTime(0) + s.stopwatch.lapTime(1) + s.stopwatch.lapTime(2), s.stopwatch.lapTotal(2));
  tapRect(s, swButton(s, 1));  // Stop
  CHECK(strcmp("Reset", "Reset") == 0);
  tapRect(s, swButton(s, 0));  // Reset (the left button is Reset while stopped)
  CHECK(s.stopwatch.lapCount() == 0 && s.stopwatch.elapsed(s.os.millis()) == 0 && !s.stopwatch.running());
}

TEST(stopwatch_lap_limit_and_disabled_buttons) {
  SimSession s(320, 240);
  s.os.launch("stopwatch");
  s.run(100);
  tapRect(s, swButton(s, 0));  // Reset with nothing to reset: ignored
  CHECK(s.stopwatch.lapCount() == 0 && !s.stopwatch.running());
  tapRect(s, swButton(s, 1));  // Start
  for (int i = 0; i < StopwatchApp::kMaxLaps + 5; ++i) {
    s.run(300);
    tapRect(s, swButton(s, 0));
  }
  CHECK_EQ(s.stopwatch.lapCount(), StopwatchApp::kMaxLaps);
  CHECK(s.stopwatch.running());
}

TEST(stopwatch_screen_renders_at_every_size_and_incremental_equals_full) {
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    SimSession s(sz[0], sz[1]);
    s.os.launch("stopwatch");
    s.run(150);
    CHECK(swButton(s, 0).h >= s.os.theme().dp(32) && swButton(s, 1).h >= s.os.theme().dp(32));
    CHECK(swButton(s, 1).bottom() <= sz[1]);
    CHECK(sameAsFullRedraw(s));  // idle
    tapRect(s, swButton(s, 1));
    s.run(1234);
    CHECK(sameAsFullRedraw(s));  // running
    for (int i = 0; i < 4; ++i) {
      s.run(700);
      tapRect(s, swButton(s, 0));
    }
    s.run(333);
    CHECK(sameAsFullRedraw(s));  // laps with best and worst marked
    tapRect(s, swButton(s, 1));
    s.run(200);
    CHECK(sameAsFullRedraw(s));  // stopped
  }
}

TEST(stopwatch_idle_screen_does_not_repaint) {
  SimSession s(320, 240);
  s.os.launch("stopwatch");
  s.run(300);
  CHECK(!s.os.renderPending());
  const uint32_t frames = s.os.status().framesRendered;
  s.run(5000);
  CHECK_EQ(s.os.status().framesRendered, frames);
}
