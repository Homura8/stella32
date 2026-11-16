#include "clock_app.h"

#include <stdio.h>
#include <string.h>

#include "../ui/layout.h"
#include "../ui/materials.h"
#include "common.h"

namespace stella {
namespace apps {

using namespace layout;

const AppInfo &ClockApp::info() const {
  static const AppInfo kInfo{"clock", "Clock", icons::clock, 2, 0};
  return kInfo;
}

void ClockApp::ClockScreen::layout(const Rect &content, const Theme &t) {
  const Font &display = *t.font.display;
  Rect parts[5];
  column(content.inset(t.m.margin, t.m.gapS),
         {flex(), px(display.ascent + t.dp(4)), px(t.font.title->lineHeight), px(t.m.control), flex()}, t.m.gapS,
         parts);
  time_ = parts[1];
  date_ = parts[2];
  chip_ = parts[3];
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
}

}  // namespace apps
}  // namespace stella
