// Ambient light: the model that takes the screen's own glow out of a cheap
// sensor's reading, and the runtime dimming the backlight in a dark room.
#include <string.h>

#include "sim_session.h"
#include "stella/runtime/ambient.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {

// What the simulator's sensor reads with the defaults, as a calibration.
void calibrate(SimSession &s, bool enable = true) {
  SystemSettings st = s.os.settings();
  st.lightDark = (uint16_t)s.dev.light.dark;
  st.lightSelf = (int16_t)(s.dev.light.inverted ? -s.dev.light.self : s.dev.light.self);
  st.lightBright = (uint16_t)(s.dev.light.dark + (s.dev.light.inverted ? -1 : 1) * s.dev.light.range);
  st.autoBrightness = enable;
  s.os.applySettings(st);
}

int backlight(SimSession &s) { return s.dev.display.backlight(); }

}  // namespace

TEST(ambient_model_removes_the_screens_own_light_and_finds_polarity) {
  Ambient a;
  LightCalibration c;
  c.dark = 700;
  c.self = 420;
  c.bright = 2900;
  a.configure(c);
  CHECK(a.instant(700, 0) == 0.f);               // dark room, screen off
  CHECK(a.instant(2900, 0) == 1.f);              // bright room, screen off
  CHECK(a.instant(1800, 0) > 0.49f && a.instant(1800, 0) < 0.51f);
  // The screen at duty 150 adds 420 * 150/255: still a dark room.
  const uint16_t glow = (uint16_t)(700 + 420 * 150 / 255);
  CHECK(a.instant(glow, 150) < 0.01f);
  CHECK(a.instant((uint16_t)(1800 + 420 * 150 / 255), 150) > 0.49f);
  CHECK(a.instant(4095, 0) == 1.f && a.instant(0, 0) == 0.f);  // clamped
  // A sensor that reads lower in light: the sign of (bright - dark) handles it.
  c.dark = 3000;
  c.self = -400;
  c.bright = 900;
  a.configure(c);
  CHECK(a.instant(3000, 0) == 0.f && a.instant(900, 0) == 1.f);
  CHECK(a.instant((uint16_t)(3000 - 400 * 255 / 255), 255) < 0.01f);
  // Without a calibration the room counts as bright: nothing is dimmed.
  a.configure(LightCalibration{});
  CHECK(a.instant(123, 0) == 1.f && a.scale() == 255);
}

TEST(ambient_scale_runs_from_the_minimum_in_the_dark_to_full_and_smooths) {
  Ambient a;
  LightCalibration c;
  c.dark = 700;
  c.self = 0;
  c.bright = 2900;
  a.configure(c);
  a.sample(700, 0);
  CHECK(a.scale() >= 28 && a.scale() <= 33);  // 12 % of 255
  a.configure(c);
  a.sample(2900, 0);
  CHECK(a.scale() == 255);
  // A hand over the sensor for a moment barely moves a smoothed level.
  for (int i = 0; i < 40; ++i) a.sample(2900, 0);
  a.sample(700, 0);
  CHECK(a.level() > 0.7f);
  for (int i = 0; i < 40; ++i) a.sample(700, 0);
  CHECK(a.level() < 0.02f);
  // The curve is gentle: a dim room is not nearly as dark as the dark.
  a.configure(c);
  a.sample(700 + 2200 / 10, 0);  // 10 % of the way
  CHECK(a.scale() > 80);
}

TEST(a_dark_room_dims_the_screen_and_light_brings_it_back_without_flicker) {
  SimSession s(320, 240, kMorningEpoch);
  calibrate(s);
  s.dev.light.ambient = 1.f;
  s.run(6000);
  const int full = backlight(s);
  CHECK(full > 100);
  s.dev.light.ambient = 0.f;
  s.run(20000);
  const int dark = backlight(s);
  CHECK(dark >= 1 && dark < full / 4);
  // Settled: it does not hunt (the screen's own light is taken out).
  int lo = 255, hi = 0;
  for (int i = 0; i < 100; ++i) {
    s.run(100);
    lo = backlight(s) < lo ? backlight(s) : lo;
    hi = backlight(s) > hi ? backlight(s) : hi;
  }
  CHECK(hi - lo <= 1);
  s.dev.light.ambient = 0.5f;
  s.run(20000);
  CHECK(backlight(s) > dark && backlight(s) < full);
  s.dev.light.ambient = 1.f;
  s.run(20000);
  CHECK(backlight(s) >= full - 2);
}

TEST(brightness_changes_in_small_steps_not_a_jump) {
  SimSession s(320, 240, kMorningEpoch);
  calibrate(s);
  s.dev.light.ambient = 1.f;
  s.run(6000);
  s.dev.light.ambient = 0.f;
  int previous = backlight(s), biggestStep = 0, changes = 0;
  for (int i = 0; i < 400; ++i) {
    s.run(50);
    const int now = backlight(s);
    if (now != previous) {
      ++changes;
      biggestStep = previous - now > biggestStep ? previous - now : biggestStep;
    }
    previous = now;
  }
  CHECK(changes >= 5);        // it glides
  CHECK(biggestStep <= 40);   // and never drops the screen in one go
}

TEST(a_sensor_that_reads_lower_in_light_works_too) {
  SimSession s(320, 240, kMorningEpoch);
  s.dev.light.inverted = true;
  s.dev.light.dark = 3300;
  calibrate(s);
  s.dev.light.ambient = 1.f;
  s.run(6000);
  const int full = backlight(s);
  s.dev.light.ambient = 0.f;
  s.run(20000);
  CHECK(backlight(s) < full / 4);
}

TEST(auto_brightness_needs_a_calibration_and_the_setting) {
  SimSession s(320, 240, kMorningEpoch);
  s.dev.light.ambient = 0.f;
  s.run(6000);
  const int normal = backlight(s);  // not calibrated, not enabled: the screen is the person's
  CHECK(normal > 100);
  SystemSettings st = s.os.settings();
  st.autoBrightness = true;  // no calibration behind it
  s.os.applySettings(st);
  CHECK(!s.os.settings().autoBrightness);  // sanitised away
  calibrate(s, false);       // calibrated but switched off
  s.run(10000);
  CHECK_EQ(backlight(s), normal);
  CHECK(s.os.status().ambient < 0);
  calibrate(s, true);
  s.run(10000);
  CHECK(backlight(s) < normal / 2);
  CHECK(s.os.status().ambient >= 0 && s.os.status().ambient <= 5);  // a dark room
}

TEST(a_hold_fixes_the_backlight_for_a_calibration_and_lets_go) {
  SimSession s(320, 240, kMorningEpoch);
  calibrate(s);
  s.dev.light.ambient = 1.f;
  s.run(6000);
  const int normal = backlight(s);
  s.os.holdBacklight(0);
  CHECK_EQ(backlight(s), 0);
  s.run(2000);
  CHECK_EQ(backlight(s), 0);  // auto brightness does not touch it
  s.os.holdBacklight(77);
  CHECK_EQ(backlight(s), 77);
  s.os.holdBacklight(-1);
  CHECK(backlight(s) >= normal - 2);
  s.os.holdBacklight(0);
  s.run(31000);  // forgotten: it lets go by itself
  CHECK(backlight(s) >= normal - 2);
}

TEST(settings_v8_records_gain_the_led_and_light_defaults) {
  MemoryStore store;
  struct V8 {
    Appearance appearance;
    uint8_t brightness;
    uint16_t dimAfterSec, sleepAfterSec;
    bool clock24h;
    char timezone[48];
    bool nightDim;
    uint8_t bedside;
    ColorTone colorTone;
    uint8_t contrastLight, contrastDark, whiteLevel, warmth;
    bool dither;
    int8_t panelVcom;
    uint8_t panelRefreshHz;
    uint8_t skyPin;
    int16_t touchScaleX, touchOffsetX, touchScaleY, touchOffsetY;
    uint8_t volume;
    bool autoUpdate;
    bool animations;
  } v8{Appearance::Dark, 130, 60, 0, true, "AEST-10", true, 2, ColorTone::Rich, 86, 74, 70, 30, true, 0, 0, 0,
       1024, 7, 1024, 0, 35, false, false};
  CHECK(saveRecord(&store, "sys", "settings", 8, &v8, sizeof(v8)));
  SystemSettings s;
  s.ledPin[0] = 4;
  s.ledPin[1] = 16;
  s.ledPin[2] = 17;  // the board's wiring, from its sketch
  bool migrated = false;
  CHECK(loadSettings(&store, s, &migrated) == RecordStatus::Ok);
  CHECK(migrated && s.volume == 35 && s.brightness == 130);
  CHECK(s.ledPin[0] == 4 && s.ledPin[2] == 17 && s.ledMode == 1 && !s.autoBrightness && !s.lightCalibrated());
  s.ledMode = 2;
  s.lightDark = 700;
  s.lightBright = 2900;
  s.lightSelf = -120;
  s.autoBrightness = true;
  CHECK(saveSettings(&store, s));
  SystemSettings again;
  CHECK(loadSettings(&store, again, &migrated) == RecordStatus::Ok && !migrated);
  CHECK(again.ledMode == 2 && again.lightSelf == -120 && again.autoBrightness && again.lightCalibrated());
}

TEST(settings_v7_records_still_migrate_through_the_chain) {
  MemoryStore store;
  struct V7 {
    Appearance appearance;
    uint8_t brightness;
    uint16_t dimAfterSec, sleepAfterSec;
    bool clock24h;
    char timezone[48];
    bool nightDim;
    uint8_t bedside;
    ColorTone colorTone;
    uint8_t contrastLight, contrastDark, whiteLevel, warmth;
    bool dither;
    int8_t panelVcom;
    uint8_t panelRefreshHz;
    uint8_t skyPin;
    int16_t touchScaleX, touchOffsetX, touchScaleY, touchOffsetY;
    uint8_t volume;
    bool autoUpdate;
  } v7{Appearance::Light, 140, 60, 0, true, "UTC0", true, 1, ColorTone::Soft, 0, 0, 100, 0, true, 0, 0, 0,
       1024, 0, 1024, 0, 35, false};
  CHECK(saveRecord(&store, "sys", "settings", 7, &v7, sizeof(v7)));
  SystemSettings s;
  bool migrated = false;
  CHECK(loadSettings(&store, s, &migrated) == RecordStatus::Ok);
  CHECK(migrated && s.volume == 35 && !s.autoUpdate && s.brightness == 140 && s.ledMode == 1 && !s.autoBrightness);
}
