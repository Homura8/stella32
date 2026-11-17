// Alerts, background wake-ups, the alert chime, and the Alarms and Timer apps.
#include <string.h>

#include "sim_session.h"
#include "stella/ui/layout.h"
#include "test.h"

using namespace stella;
using namespace stella::host;
using stella::apps::AlarmBook;
using stella::apps::AlarmEntry;
using stella::apps::AlarmsApp;
using stella::apps::TimerApp;

namespace {

LocalTime at(int weekday, int hour, int minute) {
  LocalTime t;
  t.valid = true;
  t.weekday = weekday;
  t.hour = hour;
  t.minute = minute;
  return t;
}

AlarmEntry alarm(int hour, int minute, uint8_t days, bool enabled = true) {
  AlarmEntry a;
  a.hour = (uint8_t)hour;
  a.minute = (uint8_t)minute;
  a.days = days;
  a.enabled = enabled ? 1 : 0;
  return a;
}

const char *topApp(SimSession &s) { return s.os.foreground() ? s.os.foreground()->info().id : ""; }

void tapRect(SimSession &s, const Rect &r) { s.tap(r.centerX(), r.centerY()); }

// The timer screen's bottom row: Reset | +1 min | Start.
Rect timerButton(SimSession &s, int index) {
  const Theme &t = s.os.theme();
  const Rect area = s.os.contentRect().inset(t.m.margin, t.m.gapS);
  const Rect row(area.x, area.bottom() - t.m.control, area.w, t.m.control);
  Rect b[3];
  layout::row(row, {layout::flex(), layout::flex(), layout::flex()}, t.m.gapM, b);
  return b[index];
}

void startFiveMinuteTimer(SimSession &s) {
  s.os.launch("timer");
  s.run(100);
  tapRect(s, timerButton(s, 2));
}

bool sameAsFullRedraw(SimSession &s) {
  const std::vector<Color> incremental = s.dev.display.framebuffer();
  s.os.invalidateAll();
  s.run(20);
  return incremental == s.dev.display.framebuffer();
}

}  // namespace

// ------------------------------------------------------------------ logic

TEST(alarm_schedule_logic) {
  using namespace stella::apps;
  const LocalTime mon0644 = at(1, 6, 44);
  CHECK(alarmDue(alarm(6, 44, kWeekdays), mon0644));
  CHECK(!alarmDue(alarm(6, 44, kWeekend), mon0644));
  CHECK(!alarmDue(alarm(6, 44, kWeekdays, false), mon0644));
  CHECK(alarmDue(alarm(6, 44, 0), mon0644));  // once: any day
  CHECK_EQ(minutesUntil(alarm(6, 45, kWeekdays), mon0644), 1);
  CHECK_EQ(minutesUntil(alarm(6, 44, kWeekdays), mon0644), 1440);   // next is Tuesday
  CHECK_EQ(minutesUntil(alarm(6, 44, 1u << 1), mon0644), 7 * 1440);  // Mondays only
  const LocalTime fri2300 = at(5, 23, 0);
  CHECK_EQ(minutesUntil(alarm(7, 0, kWeekdays), fri2300), 2 * 1440 + 8 * 60);  // over the weekend
  CHECK_EQ(minutesUntil(alarm(7, 0, 0), fri2300), 8 * 60);
  CHECK_EQ(minutesUntil(alarm(7, 0, kEveryDay), LocalTime{}), -1);  // time unknown

  AlarmBook book;
  book.count = 3;
  book.alarms[0] = alarm(9, 0, kEveryDay);
  book.alarms[1] = alarm(7, 30, kWeekdays, false);
  book.alarms[2] = alarm(8, 0, 0);
  int minutes = 0;
  CHECK_EQ(nextAlarm(book, at(1, 6, 0), minutes), 2);
  CHECK_EQ(minutes, 120);

  char text[40];
  describeDays(0, text, sizeof(text));
  CHECK(strcmp(text, "Once") == 0);
  describeDays(kWeekdays, text, sizeof(text));
  CHECK(strcmp(text, "Weekdays") == 0);
  describeDays((1u << 1) | (1u << 3) | (1u << 0), text, sizeof(text));
  CHECK(strcmp(text, "Mon, Wed, Sun") == 0);
  describeWait(25, text, sizeof(text));
  CHECK(strcmp(text, "25 min") == 0);
  describeWait(9 * 60 + 5, text, sizeof(text));
  CHECK(strcmp(text, "9 h 5 min") == 0);
  describeWait(2 * 1440 + 3 * 60, text, sizeof(text));
  CHECK(strcmp(text, "2 d 3 h") == 0);

  AlarmBook bad;
  bad.count = 40;
  bad.alarms[0] = alarm(31, 77, 0xFF);
  bad.sanitize();
  CHECK(bad.count == AlarmBook::kMax && bad.alarms[0].hour == 7 && bad.alarms[0].minute == 0 &&
        bad.alarms[0].days == kEveryDay);
}

// ------------------------------------------------------------------ alerts and timer

TEST(timer_rings_in_the_background_with_a_chime) {
  SimSession s(320, 240);
  startFiveMinuteTimer(s);
  CHECK(s.timer.running());
  s.os.launch("weather");  // the timer keeps running behind another app
  s.run(4 * 60 * 1000);
  CHECK(!s.os.alertShowing());
  s.run(61 * 1000);
  CHECK(s.os.alertShowing() && strcmp(s.os.alertTitle(), "Time's up") == 0);
  CHECK(strcmp(topApp(s), "weather") == 0);  // the alert sits over whatever is in front
  CHECK(s.dev.sound.tones() >= 3);
  CHECK(sameAsFullRedraw(s));
  tapRect(s, s.os.alertButtonRect(0));  // Done
  CHECK(!s.os.alertShowing() && !s.timer.running() && s.dev.sound.current() == 0);
  CHECK(sameAsFullRedraw(s));
}

TEST(timer_again_restarts_and_unanswered_alerts_time_out) {
  SimSession s(320, 240);
  startFiveMinuteTimer(s);
  s.run(5 * 60 * 1000 + 500);
  CHECK(s.os.alertShowing());
  tapRect(s, s.os.alertButtonRect(1));  // Again
  CHECK(!s.os.alertShowing() && s.timer.running());
  s.run(5 * 60 * 1000 + 500);
  CHECK(s.os.alertShowing());
  s.run(TimerApp::kRingMs + 1000);  // nobody answers
  CHECK(!s.os.alertShowing() && s.dev.sound.current() == 0 && !s.timer.running());
}

TEST(timer_pause_resume_reset_and_plus_minute) {
  SimSession s(320, 240);
  startFiveMinuteTimer(s);
  s.run(60 * 1000);
  tapRect(s, timerButton(s, 2));  // Pause
  CHECK(!s.timer.running());
  const uint32_t left = s.timer.remaining(s.dev.platform.millis());
  s.run(10 * 60 * 1000);
  CHECK(!s.os.alertShowing() && s.timer.remaining(s.dev.platform.millis()) == left);
  tapRect(s, timerButton(s, 1));  // +1 min
  CHECK_EQ(s.timer.remaining(s.dev.platform.millis()), left + 60000u);
  tapRect(s, timerButton(s, 2));  // Resume
  CHECK(s.timer.running());
  // While visible the countdown repaints once a second, and only the digits.
  s.dev.display.resetCounters();
  s.run(3000);
  CHECK(s.dev.display.writes() >= 3);
  CHECK(s.dev.display.pixelsWritten() < 320u * 240u);
  tapRect(s, timerButton(s, 0));  // Reset
  CHECK(!s.timer.running() && s.timer.remaining(s.dev.platform.millis()) == s.timer.duration());
  CHECK(sameAsFullRedraw(s));
}

TEST(alert_wakes_the_display_and_keeps_it_awake) {
  SimSession s(320, 240);
  SystemSettings p = s.os.settings();
  p.dimAfterSec = 30;
  p.sleepAfterSec = 60;
  s.os.applySettings(p);
  startFiveMinuteTimer(s);
  s.os.goHome();
  s.run(2 * 60 * 1000);
  CHECK(s.os.power() == PowerState::Off && s.dev.display.backlight() == 0);
  s.run(3 * 60 * 1000 + 500);
  CHECK(s.os.alertShowing() && s.os.power() == PowerState::Active && s.dev.display.backlight() > 0);
  s.run(90 * 1000);  // longer than the sleep timeout: still awake while ringing
  CHECK(s.os.power() == PowerState::Active);
}

TEST(alerts_are_modal_and_cancel_the_touch_in_progress) {
  SimSession s(320, 240);
  s.os.launch("settings");
  s.run(100);
  const Point row = s.settingsRow(1);
  // A finger is already down on a row when the alert appears.
  s.dev.touch.down(row.x, row.y);
  s.run(80);
  Alert a;
  a.title = "Hello";
  a.message = "From the foreground app";
  s.os.showAlert(a);
  s.dev.touch.up();
  s.run(150);
  CHECK(s.os.alertShowing() && s.os.topScreen() == &s.settings.root());
  // Taps outside the alert's buttons do nothing at all.
  s.tap(row.x, row.y);
  const Point back = s.backButton();
  s.tap(back.x, back.y);
  CHECK(s.os.alertShowing() && s.os.topScreen() == &s.settings.root() && strcmp(topApp(s), "settings") == 0);
  CHECK(sameAsFullRedraw(s));
  CHECK(s.os.alertButtonRect(1).empty());  // single-button alert
  tapRect(s, s.os.alertButtonRect(0));
  CHECK(!s.os.alertShowing());
  s.tap(row.x, row.y);
  CHECK(s.os.topScreen() != &s.settings.root());
}

TEST(alerts_queue_replace_by_tag_and_can_be_withdrawn) {
  SimSession s(320, 240);
  s.os.launch("clock");
  s.run(100);
  Alert a;
  a.title = "First";
  a.tag = 1;
  CHECK(s.os.showAlert(a));
  a.title = "Second";
  a.tag = 2;
  CHECK(s.os.showAlert(a));
  CHECK(strcmp(s.os.alertTitle(), "First") == 0);
  a.title = "First, updated";
  a.tag = 1;
  CHECK(s.os.showAlert(a));
  CHECK(strcmp(s.os.alertTitle(), "First, updated") == 0);
  s.os.dismissAlert(1);
  CHECK(strcmp(s.os.alertTitle(), "Second") == 0);
  a.tag = 3;
  CHECK(s.os.showAlert(a) && s.os.showAlert((a.tag = 4, a)) && s.os.showAlert((a.tag = 5, a)));
  CHECK(!s.os.showAlert((a.tag = 6, a)));  // the queue is bounded
  for (int i = 0; i < 4; ++i) tapRect(s, s.os.alertButtonRect(0));
  CHECK(!s.os.alertShowing());
}

// ------------------------------------------------------------------ alarms

TEST(alarm_rings_snoozes_and_stops) {
  SimSession s(320, 240);  // Monday 22:30
  s.os.launch("alarms");
  s.run(100);
  CHECK(s.alarms.setAlarm(-1, alarm(22, 31, 1u << 1), s.os));      // Mondays
  CHECK(s.alarms.setAlarm(-1, alarm(22, 31, apps::kWeekend), s.os));  // not today
  s.os.goHome();
  s.run(30 * 1000);
  CHECK(!s.os.alertShowing());
  s.run(40 * 1000);
  CHECK(s.os.alertShowing() && strcmp(s.os.alertTitle(), "22:31") == 0);
  CHECK(s.dev.sound.tones() > 0);
  tapRect(s, s.os.alertButtonRect(1));  // Snooze
  CHECK(!s.os.alertShowing() && s.alarms.snoozed());
  s.run(8 * 60 * 1000);
  CHECK(!s.os.alertShowing());
  s.run(70 * 1000);
  CHECK(s.os.alertShowing());
  tapRect(s, s.os.alertButtonRect(0));  // Stop
  CHECK(!s.os.alertShowing() && !s.alarms.snoozed());
  // A repeating alarm stays on; it does not ring again within the week.
  CHECK(s.alarms.book().alarms[0].enabled == 1);
  s.run(10 * 60 * 1000);
  CHECK(!s.os.alertShowing());
}

TEST(one_off_alarm_switches_itself_off_and_is_saved) {
  SimSession s(320, 240);
  s.os.launch("alarms");
  s.run(100);
  CHECK(s.alarms.setAlarm(-1, alarm(22, 31, 0), s.os));
  s.run(70 * 1000);
  CHECK(s.os.alertShowing());
  CHECK(s.alarms.book().alarms[0].enabled == 0);
  AlarmBook stored;
  CHECK(loadRecord(&s.dev.storage, "alarms", "book", AlarmBook::kVersion, &stored, sizeof(stored)) ==
        RecordStatus::Ok);
  CHECK(stored.count == 1 && stored.alarms[0].enabled == 0);
}

TEST(alarms_wait_for_the_time) {
  SimSession s(320, 240, /*epoch=*/0, Appearance::Auto, false, /*online=*/false);
  s.os.launch("alarms");
  s.run(100);
  CHECK(s.alarms.setAlarm(-1, alarm(0, 0, apps::kEveryDay), s.os));
  s.run(3 * 60 * 1000);
  CHECK(!s.os.alertShowing());
}

TEST(alarm_added_and_toggled_by_touch) {
  SimSession s(320, 240);
  s.os.launch("alarms");
  s.run(100);
  const Theme &t = s.os.theme();
  const Rect c = s.os.contentRect();
  const Rect area = c.inset(t.m.margin, t.m.gapS);
  s.tap(area.right() - t.dp(42), area.y + t.m.control / 2);  // Add
  CHECK(s.os.topScreen() != &s.alarms.root());

  // The editor: time on top, then summary, day chips and the Save button.
  Rect r[4];
  layout::column(area, {layout::flex(), layout::px(t.font.caption->lineHeight), layout::px(t.dp(34)),
                        layout::px(t.m.control)},
                 t.m.gapM, r);
  const int digitW = Canvas::textWidth(*t.font.display, "00") + t.dp(12);
  const int colonW = Canvas::textWidth(*t.font.display, ":") + t.dp(4);
  const int hourX = r[0].centerX() - colonW / 2 - digitW / 2;
  s.tap(hourX, r[0].y + r[0].h / 5);  // hour up: 07 -> 08
  s.tap(hourX, r[0].y + r[0].h / 5);  // 09
  s.tap(hourX, r[0].bottom() - r[0].h / 5);  // down: 08
  const Rect monday = layout::grid(r[2], 7, 1, 0, t.m.gapS);
  tapRect(s, monday);
  tapRect(s, r[3]);  // Save
  CHECK(s.os.topScreen() == &s.alarms.root());
  CHECK(s.alarms.book().count == 1);
  const AlarmEntry &a = s.alarms.book().alarms[0];
  CHECK(a.hour == 8 && a.minute == 0 && a.days == (1u << 1) && a.enabled == 1);

  // The switch on the row turns it off (and it is saved).
  const int rowY = area.y + t.m.control + t.m.gapM + t.dp(4) + (t.m.row + t.dp(8)) / 2;
  s.tap(area.right() - t.m.padding - t.dp(20), rowY);
  CHECK(s.alarms.book().alarms[0].enabled == 0);
  AlarmBook stored;
  CHECK(loadRecord(&s.dev.storage, "alarms", "book", AlarmBook::kVersion, &stored, sizeof(stored)) ==
        RecordStatus::Ok);
  CHECK(stored.count == 1 && stored.alarms[0].enabled == 0);
  CHECK(sameAsFullRedraw(s));

  // Tapping the row opens it; Delete removes it.
  s.tap(area.x + area.w / 3, rowY);
  CHECK(s.os.topScreen() != &s.alarms.root());
  Rect b[2];
  layout::row(r[3], {layout::flex(), layout::flex()}, t.m.gapM, b);
  tapRect(s, b[0]);
  CHECK(s.os.topScreen() == &s.alarms.root() && s.alarms.book().count == 0);
}

TEST(alarm_editor_fits_narrow_panels_in_12_hour_mode) {
  SimSession s(240, 320);
  SystemSettings p = s.os.settings();
  p.clock24h = false;
  s.os.applySettings(p);
  s.os.launch("alarms");
  s.run(100);
  CHECK(s.alarms.setAlarm(-1, alarm(18, 5, apps::kWeekdays), s.os));
  const Theme &t = s.os.theme();
  const Rect area = s.os.contentRect().inset(t.m.margin, t.m.gapS);
  s.tap(area.x + area.w / 3, area.y + t.m.control + t.m.gapM + t.dp(4) + (t.m.row + t.dp(8)) / 2);
  CHECK(s.os.topScreen() != &s.alarms.root());
  // Tap where the AM/PM pill must be: right of the minutes, inside the panel.
  Rect r[4];
  layout::column(area, {layout::flex(), layout::px(t.font.caption->lineHeight), layout::px(t.dp(34)),
                        layout::px(t.m.control)},
                 t.m.gapM, r);
  const int digitW = Canvas::textWidth(*t.font.display, "00");
  const int colonW = Canvas::textWidth(*t.font.display, ":") + t.dp(4);
  const int total = 2 * digitW + colonW + t.m.gapS + t.dp(40);
  CHECK(total <= r[0].w);
  const int pillX = r[0].centerX() + total / 2 - t.dp(20);
  s.tap(pillX, r[0].centerY());  // PM -> AM
  Rect b[2];
  layout::row(r[3], {layout::flex(), layout::flex()}, t.m.gapM, b);
  tapRect(s, b[1]);  // Save
  CHECK(s.os.topScreen() == &s.alarms.root() && s.alarms.book().alarms[0].hour == 6);
}
