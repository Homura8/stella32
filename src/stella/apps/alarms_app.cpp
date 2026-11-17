#include "alarms_app.h"

#include <stdio.h>
#include <string.h>

#include "../ui/layout.h"
#include "../ui/materials.h"
#include "common.h"

namespace stella {
namespace apps {

using namespace layout;

namespace {

// Day chips in Monday-first order (weekday numbers, 0 = Sunday).
constexpr uint8_t kChipDay[7] = {1, 2, 3, 4, 5, 6, 0};
const char *const kChipLabel[7] = {"M", "T", "W", "T", "F", "S", "S"};
const char *const kShortDay[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

int wrap(int value, int modulo) { return ((value % modulo) + modulo) % modulo; }

}  // namespace

// ------------------------------------------------------------------ logic

void AlarmBook::sanitize() {
  if (count > kMax) count = kMax;
  for (int i = 0; i < count; ++i) {
    AlarmEntry &a = alarms[i];
    if (a.hour > 23) a.hour = 7;
    if (a.minute > 59) a.minute = 0;
    a.days &= kEveryDay;
    a.enabled = a.enabled ? 1 : 0;
  }
}

bool alarmDue(const AlarmEntry &alarm, const LocalTime &now) {
  if (!alarm.enabled || !now.valid) return false;
  if (alarm.hour != now.hour || alarm.minute != now.minute) return false;
  return alarm.days == 0 || (alarm.days & (1u << (now.weekday % 7)));
}

int minutesUntil(const AlarmEntry &alarm, const LocalTime &now) {
  if (!alarm.enabled || !now.valid) return -1;
  const int nowMinute = now.hour * 60 + now.minute;
  for (int d = 0; d <= 7; ++d) {
    const int weekday = (now.weekday + d) % 7;
    if (alarm.days && !(alarm.days & (1u << weekday))) continue;
    const int delta = d * 1440 + alarm.hour * 60 + alarm.minute - nowMinute;
    if (delta > 0) return delta;
  }
  return -1;
}

int nextAlarm(const AlarmBook &book, const LocalTime &now, int &minutes) {
  int best = -1;
  minutes = -1;
  for (int i = 0; i < book.count; ++i) {
    const int m = minutesUntil(book.alarms[i], now);
    if (m > 0 && (best < 0 || m < minutes)) {
      best = i;
      minutes = m;
    }
  }
  return best;
}

void describeDays(uint8_t days, char *out, size_t capacity) {
  days &= kEveryDay;
  if (days == 0) snprintf(out, capacity, "Once");
  else if (days == kEveryDay) snprintf(out, capacity, "Every day");
  else if (days == kWeekdays) snprintf(out, capacity, "Weekdays");
  else if (days == kWeekend) snprintf(out, capacity, "Weekends");
  else {
    size_t used = 0;
    out[0] = 0;
    for (uint8_t day : kChipDay) {
      if (!(days & (1u << day))) continue;
      const int n = snprintf(out + used, capacity - used, "%s%s", used ? ", " : "", kShortDay[day]);
      if (n < 0 || (size_t)n >= capacity - used) break;
      used += (size_t)n;
    }
  }
}

// ------------------------------------------------------------------ app

AlarmsApp::AlarmsApp() : list_(*this), edit_(*this) {}

const AppInfo &AlarmsApp::info() const {
  static const AppInfo kInfo{"alarms", "Alarms", icons::bell, 3, 0};
  return kInfo;
}

void AlarmsApp::onInstall(ServiceHost &host) {
  const RecordStatus status = host.loadRecordFor(info().id, "book", AlarmBook::kVersion, &book_, sizeof(book_));
  if (status != RecordStatus::Ok) book_ = AlarmBook{};
  book_.sanitize();
}

bool AlarmsApp::setAlarm(int index, const AlarmEntry &alarm, AppContext &ctx) {
  if (index >= book_.count) return false;
  if (index < 0) {
    if (book_.count >= AlarmBook::kMax) return false;
    index = book_.count++;
  }
  book_.alarms[index] = alarm;
  book_.sanitize();
  save(ctx);
  if (list_.isVisible()) list_.refresh(ctx);
  return true;
}

void AlarmsApp::save(AppContext &ctx) {
  if (!ctx.saveAppRecord("book", AlarmBook::kVersion, &book_, sizeof(book_))) ctx.toast("Couldn't save alarms");
  publishNext(ctx);
}

void AlarmsApp::publishNext(AppContext &ctx) {
  const LocalTime now = ctx.localTime();
  int64_t next = 0;
  int minutes = 0;
  if (now.valid && nextAlarm(book_, now, minutes) >= 0) next = (now.epoch / 60 + minutes) * 60;
  if (snoozeUntil_ && (next == 0 || snoozeUntil_ < next)) next = snoozeUntil_;
  ctx.setNextAlarm(next);
}

void AlarmsApp::formatAlarm(const AppContext &ctx, int hour, int minute, char *time, size_t timeCap, char *suffix,
                            size_t suffixCap) const {
  LocalTime t;
  t.valid = true;
  t.hour = hour;
  t.minute = minute;
  ctx.formatTime(t, time, timeCap);
  if (suffix) snprintf(suffix, suffixCap, "%s", ctx.settings().clock24h ? "" : hour < 12 ? "AM" : "PM");
}

uint32_t AlarmsApp::onWake(AppContext &ctx) {
  const LocalTime now = ctx.localTime();
  if (!now.valid) return 30000;  // waiting for the time
  const int64_t minute = now.epoch / 60;
  if (minute != lastMinute_) {
    lastMinute_ = minute;
    int due = -1;
    bool changed = false;
    for (int i = 0; i < book_.count; ++i) {
      AlarmEntry &a = book_.alarms[i];
      if (!alarmDue(a, now)) continue;
      if (due < 0) due = i;
      if (a.days == 0) {  // a one-off alarm switches itself off
        a.enabled = 0;
        changed = true;
      }
    }
    if (changed) save(ctx);
    if (due >= 0) {
      snoozeUntil_ = 0;
      ring(now, book_.alarms[due].hour, book_.alarms[due].minute, false, ctx);
    }
    if (list_.isVisible()) list_.refresh(ctx);  // "next alarm" moves on every minute
    publishNext(ctx);
  }
  if (snoozeUntil_ && now.epoch >= snoozeUntil_) {
    snoozeUntil_ = 0;
    ring(now, ringingHour_, ringingMinute_, true, ctx);
    publishNext(ctx);
  }
  // Just after the next minute boundary, or when the snooze ends if sooner.
  uint32_t next = (uint32_t)(60 - now.second) * 1000u + 100u;
  if (snoozeUntil_) {
    const int64_t left = snoozeUntil_ - now.epoch;
    const uint32_t snoozeMs = left > 0 ? (uint32_t)left * 1000u + 100u : 100u;
    if (snoozeMs < next) next = snoozeMs;
  }
  return next;
}

void AlarmsApp::ring(const LocalTime &now, int hour, int minute, bool snoozed, AppContext &ctx) {
  ringingHour_ = (uint8_t)hour;
  ringingMinute_ = (uint8_t)minute;
  char time[12], suffix[4], title[20], message[64];
  formatAlarm(ctx, hour, minute, time, sizeof(time), suffix, sizeof(suffix));
  snprintf(title, sizeof(title), "%s%s%s", time, suffix[0] ? " " : "", suffix);
  if (snoozed) snprintf(message, sizeof(message), "Snoozed alarm \xc2\xb7 %s", weekdayName(now.weekday));
  else snprintf(message, sizeof(message), "%s \xc2\xb7 %s", greetingFor(now), weekdayName(now.weekday));
  Alert alert;
  alert.title = title;
  alert.message = message;
  alert.icon = icons::bell;
  alert.primary = "Stop";
  alert.secondary = "Snooze";
  alert.tag = kAlarmTag;
  alert.sound = true;
  alert.timeoutMs = kRingMs;
  ctx.showAlert(alert);
}

void AlarmsApp::onAlert(uint16_t tag, int action, AppContext &ctx) {
  if (tag != kAlarmTag) return;
  if (action != kAlertSecondary) {
    snoozeUntil_ = 0;
    publishNext(ctx);
    return;
  }
  const LocalTime now = ctx.localTime();
  if (!now.valid) return;
  snoozeUntil_ = now.epoch + kSnoozeSec;
  publishNext(ctx);
  char text[40];
  snprintf(text, sizeof(text), "Snoozed for %lu min", (unsigned long)(kSnoozeSec / 60));
  ctx.toast(text);
  ctx.scheduleWake(kSnoozeSec * 1000u);
}

// ------------------------------------------------------------------ list

void AlarmsApp::ListScreen::layout(const Rect &content, const Theme &t) {
  theme_ = t;
  area_ = content.inset(t.m.margin, t.m.gapS);
  arrange();
}

void AlarmsApp::ListScreen::arrange() {
  const Theme &t = theme_;
  Rect r[2];
  column(area_, {px(t.m.control), flex()}, t.m.gapM, r);
  header_ = r[0];
  body_ = r[1];
  Rect h[2];
  row(header_, {flex(), px(t.dp(84))}, t.m.gapM, h);
  add_.bounds = h[1];
  add_.label = "Add";
  add_.icon = icons::plus;
  const int rowH = t.m.row + t.dp(8);
  const int count = app_.book_.count;
  const int pad = t.dp(4);
  const int listH = count * rowH < body_.h - 2 * pad ? count * rowH : body_.h - 2 * pad;
  card_ = Rect(body_.x, body_.y, body_.w, listH + 2 * pad);
  list_.bounds = card_.inset(t.dp(2), pad);
  list_.rowHeight = rowH;
  list_.count = count;
  if (list_.offset > list_.maxOffset()) list_.offset = list_.maxOffset();
  empty_ = Rect(body_.x, body_.y, body_.w, body_.h * 3 / 4);
}

void AlarmsApp::ListScreen::onShow(AppContext &ctx) {
  count_ = -1;
  refresh(ctx);
}

void AlarmsApp::ListScreen::refresh(AppContext &ctx) {
  const AlarmBook &book = app_.book_;
  if (book.count != count_) {
    count_ = book.count;
    arrange();
    ctx.invalidate(ctx.contentRect());
  }
  bool rowsChanged = false;
  for (int i = 0; i < book.count; ++i) {
    const AlarmEntry &a = book.alarms[i];
    char time[sizeof(times_[0])], suffix[sizeof(suffixes_[0])], days[sizeof(days_[0])];
    app_.formatAlarm(ctx, a.hour, a.minute, time, sizeof(time), suffix, sizeof(suffix));
    describeDays(a.days, days, sizeof(days));
    if (strcmp(time, times_[i]) || strcmp(suffix, suffixes_[i]) || strcmp(days, days_[i]) || a.enabled != enabled_[i]) {
      memcpy(times_[i], time, sizeof(time));
      memcpy(suffixes_[i], suffix, sizeof(suffix));
      memcpy(days_[i], days, sizeof(days));
      enabled_[i] = a.enabled;
      rowsChanged = true;
    }
  }
  if (rowsChanged) ctx.invalidate(list_.bounds);

  const LocalTime now = ctx.localTime();
  char next[sizeof(next_)];
  int minutes = 0;
  if (!now.valid) snprintf(next, sizeof(next), "Alarms need the time: join Wi-Fi");
  else if (nextAlarm(book, now, minutes) < 0) snprintf(next, sizeof(next), book.count ? "All alarms off" : "No alarms");
  else {
    char wait[24];
    describeWait(minutes, wait, sizeof(wait));
    snprintf(next, sizeof(next), "Next alarm in %s", wait);
  }
  timeKnown_ = now.valid;
  if (strcmp(next, next_)) {
    memcpy(next_, next, sizeof(next));
    ctx.invalidate(header_);
  }
  add_.enabled = book.count < AlarmBook::kMax;
}

void AlarmsApp::ListScreen::drawAlarm(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed,
                                      void *self) {
  const ListScreen &s = *static_cast<const ListScreen *>(self);
  const Palette &p = t.color;
  if (pressed) c.fillRoundRect(row.inset(t.dp(4), 1), t.m.radiusControl, p.accent, 36);
  const Rect inner = row.inset(t.m.padding, 0);
  if (index < s.count_ - 1) paintHairline(c, t, inner.x, row.bottom() - 1, inner.w);
  const bool on = s.enabled_[index];
  Toggle toggle;
  toggle.bounds = inner;
  toggle.on = on;
  toggle.draw(c, t);
  Rect lines[2];
  column(center(inner, inner.w, t.font.title->lineHeight + t.font.caption->lineHeight),
         {px(t.font.title->lineHeight), px(t.font.caption->lineHeight)}, 0, lines);
  const Color timeColor = on ? p.text : p.textMuted;
  const int w = c.drawText(*t.font.title, s.times_[index], lines[0].x, lines[0].y + t.font.title->ascent, timeColor);
  if (s.suffixes_[index][0])
    c.drawText(*t.font.caption, s.suffixes_[index], lines[0].x + w + t.dp(4), lines[0].y + t.font.title->ascent,
               p.textMuted);
  c.drawTextIn(*t.font.caption, s.days_[index], lines[1], p.textMuted);
}

void AlarmsApp::ListScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  Rect h[2];
  row(header_, {flex(), px(add_.bounds.w)}, t.m.gapM, h);
  c.drawTextIn(*t.font.caption, next_, h[0], timeKnown_ ? p.textMuted : p.accent);
  add_.draw(c, t);
  if (app_.book_.count) {
    paintGlass(c, t, card_, t.m.radiusCard);
    list_.draw(c, t, drawAlarm, this);
    return;
  }
  // Empty state: a quiet bubble and two lines.
  const int r = t.m.icon;
  Rect parts[3];
  column(center(empty_, empty_.w, 2 * r + t.m.gapM + t.font.body->lineHeight + t.font.caption->lineHeight),
         {px(2 * r + t.m.gapM), px(t.font.body->lineHeight), px(t.font.caption->lineHeight)}, 0, parts);
  paintBubble(c, t, parts[0].centerX(), parts[0].y + r, r, 3);
  icons::bell(c, center(Rect(parts[0].x, parts[0].y, parts[0].w, 2 * r), r, r), p.accent);
  c.drawTextIn(*t.font.body, "No alarms yet", parts[1], p.text, Align::Center);
  c.drawTextIn(*t.font.caption, "Tap Add to set one", parts[2], p.textMuted, Align::Center);
}

bool AlarmsApp::ListScreen::onEvent(const Event &e, AppContext &ctx) {
  if (add_.handle(e, ctx)) {
    app_.edit_.edit(-1);
    ctx.push(app_.edit_);
    return true;
  }
  const int index = list_.handle(e, ctx);
  if (index < 0) return true;
  // The switch (and a margin around it) toggles; the rest of the row edits.
  Toggle probe;
  probe.bounds = list_.bounds.inset(theme_.m.padding, 0);
  if (e.x >= probe.track(theme_).x - theme_.dp(12)) {
    AlarmEntry &a = app_.book_.alarms[index];
    a.enabled = a.enabled ? 0 : 1;
    app_.save(ctx);
    refresh(ctx);
    if (a.enabled) {
      const LocalTime now = ctx.localTime();
      const int minutes = minutesUntil(a, now);
      if (minutes > 0) {
        char wait[24], text[48];
        describeWait(minutes, wait, sizeof(wait));
        snprintf(text, sizeof(text), "Alarm in %s", wait);
        ctx.toast(text);
      }
    }
    return true;
  }
  app_.edit_.edit(index);
  ctx.push(app_.edit_);
  return true;
}

// ------------------------------------------------------------------ edit

void AlarmsApp::EditScreen::edit(int index) {
  index_ = index;
  entry_ = index >= 0 ? app_.book_.alarms[index] : AlarmEntry{};
  dragColumn_ = -1;
  pressedDay_ = -1;
}

void AlarmsApp::EditScreen::layout(const Rect &content, const Theme &t) {
  theme_ = t;
  content_ = content;
  arrange();
}

void AlarmsApp::EditScreen::arrange() {
  const Theme &t = theme_;
  const Font &display = *t.font.display;
  Rect r[4];
  column(content_.inset(t.m.margin, t.m.gapS),
         {flex(), px(t.font.caption->lineHeight), px(t.dp(34)), px(t.m.control)}, t.m.gapM, r);
  time_ = r[0];
  summary_ = r[1];
  daysRow_ = r[2];
  buttons_ = r[3];

  // Hour : minute (and AM/PM in 12-hour mode), centred together.
  int digitW = Canvas::textWidth(display, "00") + t.dp(12);
  const int colonW = Canvas::textWidth(display, ":") + t.dp(4);
  int meridiemW = clock24h_ ? 0 : t.dp(48);
  int gap = clock24h_ ? 0 : t.m.gapM;
  if (2 * digitW + colonW + gap + meridiemW > time_.w) {  // narrow (portrait) panels
    digitW -= t.dp(12);
    meridiemW = clock24h_ ? 0 : t.dp(40);
    gap = clock24h_ ? 0 : t.m.gapS;
  }
  const Rect group = center(time_, 2 * digitW + colonW + gap + meridiemW, time_.h);
  Rect parts[5];
  row(group, {px(digitW), px(colonW), px(digitW), px(gap), px(meridiemW)}, 0, parts);
  columns_[0] = parts[0];
  colon_ = parts[1];
  columns_[1] = parts[2];
  meridiem_ = clock24h_ ? Rect() : center(parts[4], meridiemW, t.dp(34));

  if (index_ >= 0) {
    Rect b[2];
    row(buttons_, {flex(), flex()}, t.m.gapM, b);
    delete_.bounds = b[0];
    save_.bounds = b[1];
  } else {
    delete_.bounds = Rect();
    save_.bounds = buttons_;
  }
  delete_.label = "Delete";
  delete_.style = ButtonStyle::Glass;
  save_.label = "Save";
  save_.style = ButtonStyle::Accent;
}

void AlarmsApp::EditScreen::onShow(AppContext &ctx) {
  const bool clock24h = ctx.settings().clock24h;
  if (clock24h != clock24h_) {
    clock24h_ = clock24h;
    arrange();
  }
  arrange();  // the button row depends on new vs existing
  setTime(entry_.hour, entry_.minute, ctx);
  refreshSummary(ctx);
}

void AlarmsApp::EditScreen::setTime(int hour, int minute, AppContext &ctx) {
  entry_.hour = (uint8_t)wrap(hour, 24);
  entry_.minute = (uint8_t)wrap(minute, 60);
  const int shown = clock24h_ ? entry_.hour : (entry_.hour % 12 == 0 ? 12 : entry_.hour % 12);
  char hours[4], minutes[4], meridiem[4];
  snprintf(hours, sizeof(hours), "%02d", shown);
  snprintf(minutes, sizeof(minutes), "%02d", entry_.minute);
  snprintf(meridiem, sizeof(meridiem), "%s", clock24h_ ? "" : entry_.hour < 12 ? "AM" : "PM");
  if (strcmp(hours, digits_[0]) || strcmp(minutes, digits_[1]) || strcmp(meridiem, meridiemText_)) {
    memcpy(digits_[0], hours, sizeof(hours));
    memcpy(digits_[1], minutes, sizeof(minutes));
    memcpy(meridiemText_, meridiem, sizeof(meridiem));
    ctx.invalidate(time_);
    refreshSummary(ctx);
  }
}

void AlarmsApp::EditScreen::refreshSummary(AppContext &ctx) {
  char days[32], text[sizeof(summaryText_)];
  describeDays(entry_.days, days, sizeof(days));
  AlarmEntry probe = entry_;
  probe.enabled = 1;
  const int minutes = minutesUntil(probe, ctx.localTime());
  if (minutes > 0) {
    char wait[24];
    describeWait(minutes, wait, sizeof(wait));
    snprintf(text, sizeof(text), "%s \xc2\xb7 rings in %s", days, wait);
  } else {
    snprintf(text, sizeof(text), "%s", days);
  }
  if (strcmp(text, summaryText_)) {
    memcpy(summaryText_, text, sizeof(text));
    ctx.invalidate(summary_);
  }
}

int AlarmsApp::EditScreen::columnAt(int x, int y) const {
  for (int i = 0; i < 2; ++i)
    if (columns_[i].contains(x, y)) return i;
  return -1;
}

void AlarmsApp::EditScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  const Font &display = *t.font.display;
  const int baseline = time_.centerY() + display.ascent / 2;
  const int chevron = t.dp(14);
  for (int i = 0; i < 2; ++i) {
    const Rect &col = columns_[i];
    if (dragColumn_ == i) c.fillRoundRect(col.inset(0, t.dp(2)), t.m.radiusControl, p.accent, 30);
    c.drawTextIn(display, digits_[i], Rect(col.x, baseline - display.ascent, col.w, display.ascent), p.text,
                 Align::Center);
    // Quiet hints that the digits step up (top half) and down (bottom half).
    icons::chevronUp(c, center(Rect(col.x, col.y, col.w, baseline - display.ascent - col.y), chevron, chevron),
                     p.textMuted);
    icons::chevronDown(c, center(Rect(col.x, baseline, col.w, col.bottom() - baseline), chevron, chevron),
                       p.textMuted);
  }
  c.drawTextIn(display, ":", Rect(colon_.x, baseline - display.ascent, colon_.w, display.ascent), p.textMuted,
               Align::Center);
  if (!meridiem_.empty()) {
    paintGlass(c, t, meridiem_, meridiem_.h / 2, Elevation::Flat);
    if (meridiemPressed_) c.fillRoundRect(meridiem_, meridiem_.h / 2, p.accent, 46);
    c.drawTextIn(*t.font.body, meridiemText_, meridiem_, p.accent, Align::Center);
  }

  c.drawTextIn(*t.font.caption, summaryText_, summary_, p.textMuted, Align::Center);

  for (int i = 0; i < 7; ++i) {
    const Rect chip = grid(daysRow_, 7, 1, i, t.m.gapS);
    const bool on = entry_.days & (1u << kChipDay[i]);
    const int radius = chip.h / 2;
    if (on) {
      c.fillRoundRect(chip, radius, p.accent, pressedDay_ == i ? 210 : 255);
      c.drawTextIn(*t.font.body, kChipLabel[i], chip, p.accentText, Align::Center);
    } else {
      paintGlass(c, t, chip, radius, Elevation::Flat);
      if (pressedDay_ == i) c.fillRoundRect(chip, radius, p.accent, 46);
      c.drawTextIn(*t.font.body, kChipLabel[i], chip, p.textMuted, Align::Center);
    }
  }

  if (!delete_.bounds.empty()) delete_.draw(c, t);
  save_.draw(c, t);
}

bool AlarmsApp::EditScreen::onEvent(const Event &e, AppContext &ctx) {
  const Theme &t = ctx.theme();
  if (save_.handle(e, ctx)) {
    entry_.enabled = 1;
    if (!app_.setAlarm(index_, entry_, ctx)) return true;
    const int minutes = minutesUntil(entry_, ctx.localTime());
    if (minutes > 0) {
      char wait[24], text[48];
      describeWait(minutes, wait, sizeof(wait));
      snprintf(text, sizeof(text), "Alarm in %s", wait);
      ctx.toast(text);
    }
    ctx.pop();
    return true;
  }
  if (!delete_.bounds.empty() && delete_.handle(e, ctx)) {
    AlarmBook &book = app_.book_;
    if (index_ >= 0 && index_ < book.count) {
      for (int i = index_ + 1; i < book.count; ++i) book.alarms[i - 1] = book.alarms[i];
      --book.count;
      app_.save(ctx);
      ctx.toast("Alarm deleted");
    }
    ctx.pop();
    return true;
  }
  if (!meridiem_.empty() && trackPress(meridiem_, e, meridiemPressed_, ctx, meridiem_)) {
    setTime(entry_.hour + 12, entry_.minute, ctx);
    return true;
  }

  // Day chips toggle on tap.
  for (int i = 0; i < 7; ++i) {
    const Rect chip = grid(daysRow_, 7, 1, i, t.m.gapS);
    bool pressed = pressedDay_ == i;
    const bool tapped = trackPress(chip.inset(0, -t.dp(2)), e, pressed, ctx, daysRow_);
    if (pressed != (pressedDay_ == i)) pressedDay_ = pressed ? i : -1;
    if (tapped) {
      entry_.days ^= (uint8_t)(1u << kChipDay[i]);
      ctx.invalidate(daysRow_);
      refreshSummary(ctx);
      return true;
    }
  }

  // Digits: tap the top half for +1, the bottom half for -1; drag to scroll.
  switch (e.type) {
    case EventType::Press: {
      const int col = columnAt(e.x, e.y);
      if (col >= 0) {
        dragColumn_ = col;
        dragBase_ = col == 0 ? entry_.hour : entry_.minute;
        ctx.invalidate(time_);
      }
      return true;
    }
    case EventType::Drag:
      if (dragColumn_ >= 0) {
        const int steps = (e.startY - e.y) / t.dp(12);
        if (dragColumn_ == 0) setTime(dragBase_ + steps, entry_.minute, ctx);
        else setTime(entry_.hour, dragBase_ + steps, ctx);
      }
      return true;
    case EventType::Release:
      if (dragColumn_ >= 0) {
        dragColumn_ = -1;
        ctx.invalidate(time_);
      }
      return true;
    case EventType::Tap: {
      const int col = columnAt(e.x, e.y);
      if (col < 0) return true;
      const int step = e.y < columns_[col].centerY() ? 1 : -1;
      if (col == 0) setTime(entry_.hour + step, entry_.minute, ctx);
      else setTime(entry_.hour, entry_.minute + step, ctx);
      return true;
    }
    default:
      return false;
  }
}

}  // namespace apps
}  // namespace stella
