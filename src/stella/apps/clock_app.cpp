#include "clock_app.h"

#include <stdio.h>
#include <string.h>

#include "../ui/layout.h"
#include "../ui/materials.h"
#include "common.h"

namespace stella {
namespace apps {

using namespace layout;

ClockApp::ClockApp() : screen_(*this) {}

const AppInfo &ClockApp::info() const {
  static const AppInfo kInfo{"clock", "Clock", icons::clock, 2, kAppIdleFace};
  return kInfo;
}

void ClockApp::onRoute(const char *route, AppContext &ctx) {
  if (route && strcmp(route, "bedside") == 0 && !bedsideScreen_.isVisible()) ctx.push(bedsideScreen_);
}

void ClockApp::ClockScreen::layout(const Rect &content, const Theme &t) {
  const Font &display = *t.font.display;
  Rect parts[6];
  column(content.inset(t.m.margin, t.m.gapS),
         {flex(), px(display.ascent + t.dp(4)), px(t.font.title->lineHeight), px(t.m.control), px(t.m.control),
          flex()},
         t.m.gapS, parts);
  time_ = parts[1];
  date_ = parts[2];
  chip_ = parts[3];
  bedside_.bounds = center(parts[4], t.dp(132), t.m.control - t.dp(2));
  bedside_.label = "Bedside";
  bedside_.icon = icons::moon;
  bedside_.style = ButtonStyle::Quiet;
}

void ClockApp::ClockScreen::refresh(AppContext &ctx) {
  const LocalTime now = ctx.localTime();
  char timeText[sizeof(timeText_)], suffix[sizeof(suffix_)] = "", date[sizeof(dateText_)], chip[sizeof(chipText_)];
  ctx.formatTime(now, timeText, sizeof(timeText));
  if (now.valid && !ctx.settings().clock24h) snprintf(suffix, sizeof(suffix), "%s", now.hour < 12 ? "AM" : "PM");
  formatLongDate(now, date, sizeof(date));
  if (now.valid) {
    snprintf(chip, sizeof(chip), "%s \xc2\xb7 %s", moonPhase(now.epoch).name, phaseName(ctx.theme().phase));
  } else {
    snprintf(chip, sizeof(chip), "Connect to the network to set the time");
  }
  if (strcmp(timeText, timeText_) || strcmp(suffix, suffix_)) {
    memcpy(timeText_, timeText, sizeof(timeText));
    memcpy(suffix_, suffix, sizeof(suffix));
    ctx.invalidate(time_);
  }
  if (strcmp(date, dateText_)) {
    memcpy(dateText_, date, sizeof(date));
    ctx.invalidate(date_);
  }
  if (strcmp(chip, chipText_)) {
    memcpy(chipText_, chip, sizeof(chip));
    ctx.invalidate(chip_.inset(-glassOutset(ctx.theme())));
  }
  timeKnown_ = now.valid;
}

void ClockApp::ClockScreen::onShow(AppContext &ctx) { refresh(ctx); }

uint32_t ClockApp::ClockScreen::onTick(AppContext &ctx) {
  refresh(ctx);
  // Wake just after the next minute boundary rather than every second.
  const LocalTime now = ctx.localTime();
  return now.valid ? (uint32_t)(60 - now.second) * 1000u + 50u : 1000u;
}

void ClockApp::ClockScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  const Font &display = *t.font.display;
  const int baseline = time_.y + display.ascent;
  const int timeW = Canvas::textWidth(display, timeText_);
  const int suffixW = suffix_[0] ? Canvas::textWidth(*t.font.title, suffix_) + t.dp(6) : 0;
  const int x = time_.centerX() - (timeW + suffixW) / 2;
  c.drawText(display, timeText_, x, baseline, timeKnown_ ? p.text : p.textMuted);
  if (suffix_[0]) c.drawText(*t.font.title, suffix_, x + timeW + t.dp(6), baseline, p.textMuted);

  c.drawTextIn(*t.font.title, dateText_, date_, p.textMuted, Align::Center);

  const int iconSize = t.dp(14);
  const int textW = Canvas::textWidth(*t.font.caption, chipText_);
  int w = textW + iconSize + t.dp(8) + 2 * t.m.padding;
  if (w > chip_.w) w = chip_.w;
  const Rect pill = center(chip_, w, chip_.h);
  paintGlass(c, t, pill, pill.h / 2);
  Rect inner = pill.inset(t.m.padding, 0);
  icons::moon(c, leading(inner, iconSize), p.accent);
  inner = Rect(inner.x + iconSize + t.dp(8), inner.y, inner.w - iconSize - t.dp(8), inner.h);
  c.drawTextIn(*t.font.caption, chipText_, inner, p.text);
  bedside_.draw(c, t);
}

bool ClockApp::ClockScreen::onEvent(const Event &e, AppContext &ctx) {
  if (bedside_.handle(e, ctx)) {
    app_.onRoute("bedside", ctx);
    return true;
  }
  return false;
}

// ------------------------------------------------------------------ bedside

void ClockApp::BedsideScreen::layout(const Rect &area, const Theme &t) {
  area_ = area;
  const Font &display = *t.font.display;
  Rect parts[5];
  column(area.inset(t.m.margin, t.m.margin),
         {flex(), px(display.ascent + t.dp(4)), px(t.font.body->lineHeight), px(t.font.caption->lineHeight + t.dp(4)),
          flex()},
         t.m.gapM, parts);
  time_ = parts[1];
  date_ = parts[2];
  alarm_ = parts[3];
}

void ClockApp::BedsideScreen::refresh(AppContext &ctx) {
  const LocalTime now = ctx.localTime();
  char timeText[sizeof(timeText_)], suffix[sizeof(suffix_)] = "", date[sizeof(dateText_)], alarm[sizeof(alarmText_)] = "";
  ctx.formatTime(now, timeText, sizeof(timeText));
  if (now.valid && !ctx.settings().clock24h) snprintf(suffix, sizeof(suffix), "%s", now.hour < 12 ? "AM" : "PM");
  formatLongDate(now, date, sizeof(date));
  const int64_t next = ctx.status().nextAlarm;
  bool hasAlarm = false;
  if (now.valid && next > now.epoch) {
    const LocalTime at = ctx.localTimeAt(next);
    char when[12], wait[24];
    ctx.formatTime(at, when, sizeof(when));
    describeWait((int)((next - now.epoch + 59) / 60), wait, sizeof(wait));
    snprintf(alarm, sizeof(alarm), "%s%s \xc2\xb7 in %s", when,
             ctx.settings().clock24h ? "" : at.hour < 12 ? " AM" : " PM", wait);
    hasAlarm = true;
  }
  if (strcmp(timeText, timeText_) || strcmp(suffix, suffix_)) {
    memcpy(timeText_, timeText, sizeof(timeText));
    memcpy(suffix_, suffix, sizeof(suffix));
    ctx.invalidate(time_);
  }
  if (strcmp(date, dateText_)) {
    memcpy(dateText_, date, sizeof(date));
    ctx.invalidate(date_);
  }
  if (strcmp(alarm, alarmText_) || hasAlarm != hasAlarm_) {
    memcpy(alarmText_, alarm, sizeof(alarm));
    hasAlarm_ = hasAlarm;
    ctx.invalidate(alarm_);
  }
  timeKnown_ = now.valid;
}

void ClockApp::BedsideScreen::onShow(AppContext &ctx) {
  timeText_[0] = dateText_[0] = alarmText_[0] = 0;  // paint everything afresh
  refresh(ctx);
}

uint32_t ClockApp::BedsideScreen::onTick(AppContext &ctx) {
  refresh(ctx);
  const LocalTime now = ctx.localTime();
  return now.valid ? (uint32_t)(60 - now.second) * 1000u + 50u : 1000u;
}

void ClockApp::BedsideScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  c.fill(p.bedBg);  // a flat background: nothing to dither, nothing to flicker
  const Font &display = *t.font.display;
  const int baseline = time_.y + display.ascent;
  const int timeW = Canvas::textWidth(display, timeText_);
  const int suffixW = suffix_[0] ? Canvas::textWidth(*t.font.title, suffix_) + t.dp(6) : 0;
  const int x = time_.centerX() - (timeW + suffixW) / 2;
  c.drawText(display, timeText_, x, baseline, timeKnown_ ? p.bedText : p.bedMuted);
  if (suffix_[0]) c.drawText(*t.font.title, suffix_, x + timeW + t.dp(6), baseline, p.bedMuted);
  c.drawTextIn(*t.font.body, dateText_, date_, p.bedMuted, Align::Center);
  if (hasAlarm_) {
    const int icon = t.dp(14);
    const int w = icon + t.dp(6) + Canvas::textWidth(*t.font.caption, alarmText_);
    const Rect line = center(alarm_, w < alarm_.w ? w : alarm_.w, alarm_.h);
    icons::bell(c, leading(line, icon), p.bedMuted);
    c.drawTextIn(*t.font.caption, alarmText_, Rect(line.x + icon + t.dp(6), line.y, line.w - icon - t.dp(6), line.h),
                 p.bedMuted);
  }
}

bool ClockApp::BedsideScreen::onEvent(const Event &e, AppContext &ctx) {
  if (e.type == EventType::Tap) ctx.pop();  // any tap leaves the bedside clock
  return e.isPointer();
}

}  // namespace apps
}  // namespace stella
