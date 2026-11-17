// Gestures, touch calibration, theme tokens and widgets.
#include "stella/input/gesture.h"
#include "stella/input/touch_calibration.h"
#include "stella/ui/theme.h"
#include "stella/ui/widgets.h"
#include "test.h"

using namespace stella;

namespace {

struct Recorder {
  GestureRecognizer g;
  Event events[16];
  int count = 0;
  void feed(bool pressed, int x, int y, uint32_t now) {
    TouchSample s;
    s.pressed = pressed;
    s.x = (int16_t)x;
    s.y = (int16_t)y;
    Event out[2];
    const int n = g.update(s, now, out);
    for (int i = 0; i < n && count < 16; ++i) events[count++] = out[i];
  }
  bool has(EventType t) const {
    for (int i = 0; i < count; ++i)
      if (events[i].type == t) return true;
    return false;
  }
};

struct Sink : Invalidator {
  int calls = 0;
  void invalidate(const Rect &) override { ++calls; }
};

Event pointer(EventType t, int x, int y, int sx = -1, int sy = -1) {
  Event e;
  e.type = t;
  e.x = (int16_t)x;
  e.y = (int16_t)y;
  e.startX = (int16_t)(sx < 0 ? x : sx);
  e.startY = (int16_t)(sy < 0 ? y : sy);
  return e;
}

}  // namespace

TEST(gesture_tap_survives_dropped_samples) {
  Recorder r;
  r.feed(true, 50, 50, 1000);
  r.feed(false, 0, 0, 1020);  // resistive panels drop samples mid-press
  r.feed(true, 51, 50, 1040);
  r.feed(false, 0, 0, 1060);
  CHECK(!r.has(EventType::Release));  // not yet: release must be stable
  r.feed(false, 0, 0, 1130);
  CHECK(r.has(EventType::Press) && r.has(EventType::Release) && r.has(EventType::Tap));
  CHECK_EQ(r.count, 3);
}

TEST(gesture_drag_suppresses_tap) {
  Recorder r;
  r.feed(true, 50, 50, 0);
  r.feed(true, 80, 52, 20);
  r.feed(true, 90, 53, 35);
  r.feed(false, 0, 0, 200);
  CHECK(r.has(EventType::Drag) && r.has(EventType::Release) && !r.has(EventType::Tap));
}

// Regression (CYD): the pressed highlight makes the next frame slow, so the
// finger may be seen only once before it lifts. That is still a tap.
TEST(gesture_tap_seen_in_a_single_sample_after_a_slow_frame) {
  Recorder r;
  r.feed(true, 50, 50, 1000);
  r.feed(false, 0, 0, 1140);  // first poll after a 140 ms frame: finger gone
  r.feed(false, 0, 0, 1210);
  CHECK(r.has(EventType::Press) && r.has(EventType::Tap));
}

// Regression (CYD): one stray reading (landing or lift-off jitter) beyond
// the slop must not turn a tap into a drag.
TEST(gesture_single_stray_sample_does_not_cancel_a_tap) {
  Recorder r;
  r.feed(true, 50, 50, 0);
  r.feed(true, 51, 50, 15);
  r.feed(true, 75, 41, 30);  // one jump of 25 px
  r.feed(true, 52, 51, 45);
  r.feed(false, 0, 0, 60);
  r.feed(false, 0, 0, 130);
  CHECK(!r.has(EventType::Drag) && r.has(EventType::Tap));
}

TEST(gesture_long_press_fires_once_without_tap) {
  Recorder r;
  r.feed(true, 50, 50, 0);
  for (uint32_t t = 20; t < 1000; t += 20) r.feed(true, 51, 50, t);
  r.feed(false, 0, 0, 1100);
  int longs = 0;
  for (int i = 0; i < r.count; ++i) longs += r.events[i].type == EventType::LongPress;
  CHECK_EQ(longs, 1);
  CHECK(!r.has(EventType::Tap));
}

TEST(gesture_consumed_touch_is_silent) {
  Recorder r;
  r.g.consumeCurrent();
  r.feed(true, 50, 50, 0);
  r.feed(false, 0, 0, 200);
  CHECK_EQ(r.count, 0);
  r.feed(true, 50, 50, 300);  // next gesture is normal again
  CHECK(r.has(EventType::Press));
}

TEST(gesture_ignores_noise_shorter_than_min_tap) {
  Recorder r;
  r.feed(true, 50, 50, 0);
  r.feed(false, 0, 0, 10);
  r.feed(false, 0, 0, 200);
  CHECK(!r.has(EventType::Tap));
}

TEST(touch_calibration_maps_and_clamps) {
  const TouchCalibration cal{240, 3850, 220, 3850, false, false, false, 200};
  int x = -1, y = -1;
  CHECK(!mapTouch(cal, 2000, 2000, 150, 320, 240, x, y));
  CHECK(mapTouch(cal, 240, 220, 500, 320, 240, x, y) && x == 0 && y == 0);
  CHECK(mapTouch(cal, 3850, 3850, 500, 320, 240, x, y) && x == 319 && y == 239);
  CHECK(mapTouch(cal, 9000, -50, 500, 320, 240, x, y) && x == 319 && y == 0);
  const TouchCalibration flipped{240, 3850, 220, 3850, true, true, true, 200};
  // Swap first (raw y becomes x), then mirror both axes.
  CHECK(mapTouch(flipped, 220, 3850, 500, 320, 240, x, y) && x == 0 && y == 239);
}

TEST(theme_phases_density_and_modes) {
  CHECK(phaseForHour(6) == SkyPhase::Dawn && phaseForHour(12) == SkyPhase::Day);
  CHECK(phaseForHour(18) == SkyPhase::Dusk && phaseForHour(23) == SkyPhase::Night && phaseForHour(2) == SkyPhase::Night);
  CHECK(!resolveDark(Appearance::Auto, SkyPhase::Day) && resolveDark(Appearance::Auto, SkyPhase::Night));
  CHECK(resolveDark(Appearance::Dark, SkyPhase::Day) && !resolveDark(Appearance::Light, SkyPhase::Night));
  CHECK(densityFor(320, 240) == Density::Compact && densityFor(480, 320) == Density::Regular);
  CHECK(densityFor(240, 320) == Density::Compact);  // portrait CYD
  const Theme compact = makeTheme(Density::Compact, true, SkyPhase::Night);
  const Theme regular = makeTheme(Density::Regular, true, SkyPhase::Night);
  CHECK(regular.m.row > compact.m.row && regular.font.body->lineHeight > compact.font.body->lineHeight);
  CHECK_EQ(compact.dp(10), 10);
  CHECK(regular.dp(10) > 10);
  // Every palette keeps readable text on its sky (rough luminance contrast).
  for (int dark = 0; dark < 2; ++dark)
    for (int p = 0; p < 4; ++p) {
      const Theme t = makeTheme(Density::Compact, dark, (SkyPhase)p);
      const int sky = red8(t.color.sky[1]) + green8(t.color.sky[1]) + blue8(t.color.sky[1]);
      const int text = red8(t.color.text) + green8(t.color.text) + blue8(t.color.text);
      CHECK((sky > text ? sky - text : text - sky) > 300);
    }
}

TEST(button_activates_only_on_tap_inside) {
  Sink sink;
  Button b;
  b.bounds = Rect(10, 10, 80, 30);
  CHECK(!b.handle(pointer(EventType::Press, 20, 20), sink) && b.pressed);
  CHECK(!b.handle(pointer(EventType::Drag, 200, 20, 20, 20), sink) && !b.pressed);  // dragged away
  CHECK(!b.handle(pointer(EventType::Release, 200, 20, 20, 20), sink));
  CHECK(b.handle(pointer(EventType::Tap, 20, 20), sink));
  b.enabled = false;
  CHECK(!b.handle(pointer(EventType::Tap, 20, 20), sink));
  CHECK(sink.calls >= 2);
}

TEST(slider_drag_updates_and_clamps) {
  Sink sink;
  const Theme t = makeTheme(Density::Compact, false, SkyPhase::Day);
  Slider s;
  s.bounds = Rect(0, 0, 218, 30);
  s.minimum = 0;
  s.maximum = 200;
  CHECK(s.handle(pointer(EventType::Press, 109, 15), sink, t));
  CHECK(s.value > 90 && s.value < 110);
  CHECK(s.handle(pointer(EventType::Drag, 500, 90, 109, 15), sink, t));  // still captured off the track
  CHECK_EQ(s.value, 200);
  s.handle(pointer(EventType::Release, 500, 90), sink, t);
  CHECK(!s.dragging);
  CHECK(!s.handle(pointer(EventType::Drag, 0, 15, 109, 15), sink, t));  // no capture, no change
}

TEST(segmented_and_toggle) {
  Sink sink;
  const char *labels[] = {"A", "B", "C"};
  Segmented seg;
  seg.bounds = Rect(0, 0, 150, 30);
  seg.labels = labels;
  seg.count = 3;
  CHECK(seg.handle(pointer(EventType::Tap, 140, 15), sink) && seg.selected == 2);
  CHECK(!seg.handle(pointer(EventType::Tap, 140, 15), sink));  // already selected
  Toggle tg;
  tg.bounds = Rect(0, 0, 200, 40);
  CHECK(tg.handle(pointer(EventType::Tap, 10, 10), sink) && tg.on);
}

TEST(list_view_scrolls_and_picks) {
  Sink sink;
  ListView list;
  list.bounds = Rect(0, 100, 200, 100);
  list.rowHeight = 25;
  list.count = 10;
  CHECK_EQ(list.maxOffset(), 150);
  CHECK_EQ(list.handle(pointer(EventType::Tap, 50, 130), sink), 1);
  list.handle(pointer(EventType::Press, 50, 190), sink);
  list.handle(pointer(EventType::Drag, 50, 90, 50, 190), sink);  // scroll up by 100
  CHECK_EQ(list.offset, 100);
  list.handle(pointer(EventType::Drag, 50, -400, 50, 190), sink);
  CHECK_EQ(list.offset, 150);  // clamped
  list.handle(pointer(EventType::Release, 50, -400, 50, 190), sink);
  CHECK_EQ(list.handle(pointer(EventType::Tap, 50, 105), sink), 6);  // (5 + 150) / 25
  list.scrollTo(0);
  CHECK_EQ(list.offset, 0);
}
