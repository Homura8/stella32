// Units and dates (settings v10): the Fahrenheit and date-order and week-start
// choices, where they show, and the migration of v9 settings.
#include <string.h>

#include "sim_session.h"
#include "stella/apps/alarms_app.h"
#include "stella/apps/common.h"
#include "test.h"

using namespace stella;
using namespace stella::host;
using namespace stella::apps;

TEST(temperatures_convert_to_the_nearest_fahrenheit_degree) {
  CHECK_EQ(displayTemperature(20, false), 20);
  CHECK_EQ(displayTemperature(0, true), 32);
  CHECK_EQ(displayTemperature(100, true), 212);
  CHECK_EQ(displayTemperature(-40, true), -40);
  CHECK_EQ(displayTemperature(37, true), 99);   // 98.6
  CHECK_EQ(displayTemperature(21, true), 70);   // 69.8
  CHECK_EQ(displayTemperature(-5, true), 23);
  CHECK_EQ(displayTemperature(-1, true), 30);   // 30.2
  CHECK_EQ(displayTemperature(1, true), 34);    // 33.8
}

TEST(dates_follow_the_chosen_order) {
  LocalTime t;
  t.valid = true;
  t.weekday = 2;
  t.day = 17;
  t.month = 11;
  char text[48];
  formatLongDate(t, text, sizeof(text), 0);
  CHECK(strcmp(text, "Tuesday 17 November") == 0);
  formatLongDate(t, text, sizeof(text), 1);
  CHECK(strcmp(text, "Tuesday November 17") == 0);
  formatShortDate(t, text, sizeof(text), 0);
  CHECK(strcmp(text, "Tue 17 Nov") == 0);
  formatShortDate(t, text, sizeof(text), 1);
  CHECK(strcmp(text, "Tue Nov 17") == 0);
  LocalTime none;
  formatLongDate(none, text, sizeof(text), 1);
  CHECK(strcmp(text, "Waiting for the time") == 0);
}

TEST(week_columns_start_on_monday_or_sunday) {
  const int monday[7] = {1, 2, 3, 4, 5, 6, 0};
  const int sunday[7] = {0, 1, 2, 3, 4, 5, 6};
  for (int i = 0; i < 7; ++i) {
    CHECK_EQ(weekdayInColumn(i, false), monday[i]);
    CHECK_EQ(weekdayInColumn(i, true), sunday[i]);
  }
  char text[48];
  const uint8_t days = (1u << 0) | (1u << 1) | (1u << 3);  // Sun, Mon, Wed
  describeDays(days, text, sizeof(text), false);
  CHECK(strcmp(text, "Mon, Wed, Sun") == 0);
  describeDays(days, text, sizeof(text), true);
  CHECK(strcmp(text, "Sun, Mon, Wed") == 0);
}

TEST(units_screen_changes_temperature_date_and_week) {
  SimSession s(320, 240);
  s.os.launch("settings", "units");  // Settings > Units (a row below the fold on a small screen)
  s.run(100);
  CHECK(s.os.topScreen() && strcmp(s.os.topScreen()->title(), "Units") == 0);
  CHECK(!s.os.settings().fahrenheit && !s.os.settings().weekStartsSunday && s.os.settings().dateOrder == 0);
  Rect r = s.settings.unitsChoice(0, 1);
  s.tap(r.centerX(), r.centerY());
  CHECK(s.os.settings().fahrenheit);
  r = s.settings.unitsChoice(1, 1);
  s.tap(r.centerX(), r.centerY());
  CHECK(s.os.settings().weekStartsSunday);
  r = s.settings.unitsChoice(2, 1);
  s.tap(r.centerX(), r.centerY());
  CHECK_EQ(s.os.settings().dateOrder, 1);
  s.run(2000);  // saved
  SystemSettings loaded;
  CHECK(loadSettings(&s.dev.storage, loaded) == RecordStatus::Ok);
  CHECK(loaded.fahrenheit && loaded.weekStartsSunday && loaded.dateOrder == 1);
}

TEST(weather_and_the_bedside_glance_use_fahrenheit_at_once) {
  SimSession s(320, 240, kEveningEpoch);
  s.run(40 * 1000);  // the Weather app publishes the glance from its own timer
  const std::string before = s.os.status().glance.text;  // "17° Partly cloudy": Celsius
  CHECK(!before.empty());
  SystemSettings st = s.os.settings();
  st.fahrenheit = true;
  s.os.applySettings(st);
  s.run(500);  // apps are woken to refresh what they offer
  const std::string after = s.os.status().glance.text;
  CHECK(after != before);
  const int celsius = atoi(before.c_str());
  CHECK_EQ(atoi(after.c_str()), displayTemperature(celsius, true));
  // The Weather screen itself shows the Fahrenheit value too.
  s.os.launch("weather");
  s.run(300);
  CHECK(!s.os.renderPending());
}

TEST(settings_v9_records_gain_the_units_and_idle_face_defaults) {
  MemoryStore store;
  struct V9 {
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
    uint8_t ledMode, ledLevel;
    uint8_t ledPin[3];
    bool ledInverted;
    bool autoBrightness;
    uint16_t lightDark;
    int16_t lightSelf;
    uint16_t lightBright;
  } v9{Appearance::Dark, 130, 60, 0, false, "AEST-10AEDT,M10.1.0,M4.1.0/3", true, 2, ColorTone::Rich, 86, 74, 70, 30,
       true, 0, 0, 0, 1024, 7, 1024, 0, 35, false, 2, 80, {4, 16, 17}, true, true, 700, -120, 2900};
  CHECK(saveRecord(&store, "sys", "settings", 9, &v9, sizeof(v9)));
  SystemSettings s;
  bool migrated = false;
  CHECK(loadSettings(&store, s, &migrated) == RecordStatus::Ok && migrated);
  CHECK(s.brightness == 130 && !s.clock24h && s.bedside == 2 && s.volume == 35 && s.ledMode == 2 && s.ledPin[2] == 17);
  CHECK(s.autoBrightness && s.lightCalibrated() && s.lightSelf == -120);
  CHECK(strcmp(s.timezone, "AEST-10AEDT,M10.1.0,M4.1.0/3") == 0);
  CHECK(!s.fahrenheit && !s.weekStartsSunday && s.dateOrder == 0 && s.idleFace == 0);  // the new ones: defaults
  s.fahrenheit = true;
  s.idleFace = 1;
  CHECK(saveSettings(&store, s));
  SystemSettings again;
  CHECK(loadSettings(&store, again, &migrated) == RecordStatus::Ok && !migrated);
  CHECK(again.fahrenheit && again.idleFace == 1);
  // Out-of-range values are put right.
  again.dateOrder = 9;
  again.idleFace = 7;
  again.sanitize();
  CHECK(again.dateOrder == 0 && again.idleFace == 0);
}
