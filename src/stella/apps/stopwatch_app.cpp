#include "stopwatch_app.h"

#include <stdio.h>
#include <string.h>

#include "../core/deadline.h"
#include "../ui/layout.h"
#include "../ui/materials.h"

namespace stella {
namespace apps {

using namespace layout;

namespace {
constexpr uint32_t kHourMs = 60u * 60u * 1000u;
}

StopwatchApp::StopwatchApp() : screen_(*this) {}

const AppInfo &StopwatchApp::info() const {
  static const AppInfo kInfo{"stopwatch", "Stopwatch", icons::stopwatch, 1, 0};
  return kInfo;
}

void StopwatchApp::format(uint32_t ms, bool tenths, char *out, size_t capacity) {
  const uint32_t seconds = ms / 1000;
  if (seconds >= 3600) {
    snprintf(out, capacity, "%lu:%02lu:%02lu", (unsigned long)(seconds / 3600), (unsigned long)(seconds / 60 % 60),
             (unsigned long)(seconds % 60));
  } else if (tenths) {
    snprintf(out, capacity, "%02lu:%02lu.%lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60),
             (unsigned long)(ms / 100 % 10));
  } else {
    snprintf(out, capacity, "%02lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
  }
}

uint32_t StopwatchApp::elapsed(uint32_t nowMs) const {
  const uint64_t total = (uint64_t)accumMs_ + (running_ ? elapsedMs(nowMs, startAt_) : 0u);
  return total > kMaxMs ? kMaxMs : (uint32_t)total;
}

void StopwatchApp::fold(uint32_t nowMs) {
  if (!running_) return;
  accumMs_ = elapsed(nowMs);
  startAt_ = nowMs;
}

// Moves the running segment into the total now and then, so a stopwatch left
// running for weeks never lets the segment's own counter wrap.
uint32_t StopwatchApp::onWake(AppContext &ctx) {
  if (!running_) return kNoTick;
  fold(ctx.millis());
  return 12u * kHourMs;
}

void StopwatchApp::start(AppContext &ctx) {
  if (running_) return;
  if (accumMs_ >= kMaxMs) return;  // full: reset first
  running_ = true;
  startAt_ = ctx.millis();
  ctx.scheduleWake(12u * kHourMs);
  if (screen_.isVisible()) {
    screen_.refresh(ctx);
    ctx.requestTick();
  }
}

void StopwatchApp::stop(AppContext &ctx) {
  if (!running_) return;
  fold(ctx.millis());
  running_ = false;
  if (screen_.isVisible()) screen_.refresh(ctx);
}

void StopwatchApp::lap(AppContext &ctx) {
  if (!running_ || lapCount_ >= kMaxLaps) return;
  const uint32_t total = elapsed(ctx.millis());
  if (lapCount_ && total <= laps_[lapCount_ - 1]) return;  // the same instant
  laps_[lapCount_++] = total;
  if (screen_.isVisible()) screen_.refresh(ctx);
}

void StopwatchApp::reset(AppContext &ctx) {
  running_ = false;
  accumMs_ = 0;
  lapCount_ = 0;
  if (screen_.isVisible()) screen_.refresh(ctx);
}

// ------------------------------------------------------------------ screen

void StopwatchApp::StopwatchScreen::layout(const Rect &content, const Theme &t) {
  const Font &display = *t.font.display;
  Rect r[3];
  column(content.inset(t.m.margin, t.m.gapS), {px(display.ascent + t.dp(6)), flex(), px(t.m.control)}, t.m.gapM, r);
  digits_ = r[0];
  // The big digits keep one fixed slot (the width of "00:00", right-aligned)
  // with the tenths beside it, so nothing shifts as the numbers change.
  const Font &tenths = *t.font.title;
  const int mainW = Canvas::textWidth(display, "00:00");
  int tenthsW = 0;  // the widest of ".0" .. ".9": the digits may differ in width
  for (char d = '0'; d <= '9'; ++d) {
    const char sample[3] = {'.', d, '\0'};
    const int w = Canvas::textWidth(tenths, sample);
    if (w > tenthsW) tenthsW = w;
  }
  tenthsW += t.dp(2);
  const int x0 = digits_.centerX() - (mainW + tenthsW) / 2;
  mainRect_ = Rect(x0, digits_.y, mainW, digits_.h);
  tenthsRect_ = Rect(x0 + mainW, digits_.y, tenthsW + t.dp(2), digits_.h);
  card_ = r[1];
  list_.bounds = card_.inset(t.dp(2), t.dp(4));
  list_.rowHeight = t.dp(28);
  list_.buttonWidth = t.dp(32);
  Rect b[2];
  row(r[2], {flex(), flex(2)}, t.m.gapM, b);
  reset_.bounds = b[0];
  start_.bounds = b[1];
  start_.style = ButtonStyle::Accent;
  hint_ = card_.inset(t.m.padding, t.m.padding);
}

void StopwatchApp::StopwatchScreen::onShow(AppContext &ctx) {
  shownLaps_ = -1;
  refresh(ctx);
}

uint32_t StopwatchApp::StopwatchScreen::onTick(AppContext &ctx) {
  refresh(ctx);
  if (!app_.running_) return kNoTick;
  const uint32_t ms = app_.elapsed(ctx.millis());
  if (ms >= kMaxMs) return kNoTick;
  return 100u - ms % 100u + 3u;  // just after the next tenth
}

void StopwatchApp::StopwatchScreen::refresh(AppContext &ctx) {
  const uint32_t ms = app_.elapsed(ctx.millis());
  hours_ = ms >= kHourMs;
  const bool fresh = ms == 0 && app_.lapCount_ == 0;
  if (fresh != fresh_) {
    fresh_ = fresh;
    ctx.invalidate(digits_);
  }
  char text[sizeof(mainText_)];
  char tenths[sizeof(tenthsText_)] = "";
  if (ms >= kHourMs) {
    StopwatchApp::format(ms, false, text, sizeof(text));
  } else {
    char full[16];
    StopwatchApp::format(ms, true, full, sizeof(full));  // "MM:SS.t"
    memcpy(text, full, 5);
    text[5] = '\0';
    tenths[0] = '.';
    tenths[1] = full[6];
    tenths[2] = '\0';
  }
  if (strcmp(text, mainText_) != 0) {
    memcpy(mainText_, text, sizeof(text));
    ctx.invalidate(ms >= kHourMs ? digits_ : mainRect_);
    if (ms >= kHourMs) tenthsText_[0] = '\0';
  }
  if (strcmp(tenths, tenthsText_) != 0) {
    const bool wasEmpty = tenthsText_[0] == '\0';
    memcpy(tenthsText_, tenths, sizeof(tenths));
    ctx.invalidate(wasEmpty || !tenths[0] ? digits_ : tenthsRect_);
  }
  if (app_.lapCount_ != shownLaps_) {
    shownLaps_ = app_.lapCount_;
    list_.count = app_.lapCount_;
    list_.offset = 0;  // the newest lap is on top
    best_ = worst_ = -1;
    if (app_.lapCount_ >= 3) {
      for (int i = 0; i < app_.lapCount_; ++i) {
        if (best_ < 0 || app_.lapTime(i) < app_.lapTime(best_)) best_ = i;
        if (worst_ < 0 || app_.lapTime(i) > app_.lapTime(worst_)) worst_ = i;
      }
    }
    ctx.invalidate(card_.inset(-glassOutset(ctx.theme())));
  }
  const bool running = app_.running_;
  if (running != shownRunning_) {
    shownRunning_ = running;
    if (!app_.lapCount_) ctx.invalidate(hint_);  // the hint depends on it
  }
  const char *startLabel = running ? "Stop" : ms > 0 ? "Resume" : "Start";
  if (strcmp(startLabel, start_.label) != 0) {
    start_.label = startLabel;
    ctx.invalidate(start_.bounds.inset(-glassOutset(ctx.theme())));
  }
  const bool canStart = running || ms < kMaxMs;
  if (canStart != start_.enabled) {
    start_.enabled = canStart;
    ctx.invalidate(start_.bounds.inset(-glassOutset(ctx.theme())));
  }
  const char *leftLabel = running ? "Lap" : "Reset";
  const bool leftEnabled = running ? app_.lapCount_ < kMaxLaps : ms > 0;
  if (strcmp(leftLabel, reset_.label) != 0 || leftEnabled != reset_.enabled) {
    reset_.label = leftLabel;
    reset_.enabled = leftEnabled;
    ctx.invalidate(reset_.bounds.inset(-glassOutset(ctx.theme())));
  }
}

void StopwatchApp::StopwatchScreen::drawLap(Canvas &c, const Theme &t, int index, const Rect &r, bool, void *self) {
  const StopwatchScreen &s = *static_cast<const StopwatchScreen *>(self);
  const StopwatchApp &app = s.app_;
  const int lap = app.lapCount_ - 1 - index;  // newest first
  const Palette &p = t.color;
  char label[16], split[16], total[16];
  snprintf(label, sizeof(label), "Lap %d", lap + 1);
  StopwatchApp::format(app.lapTime(lap), true, split, sizeof(split));
  StopwatchApp::format(app.lapTotal(lap), true, total, sizeof(total));
  const Color splitColor = lap == s.best_ ? p.accent : lap == s.worst_ ? p.danger : p.text;
  Rect parts[3];
  row(r.inset(t.m.padding, 0), {px(t.dp(52)), flex(), px(Canvas::textWidth(*t.font.caption, "00:00.0") + t.dp(4))},
      t.m.gapS, parts);
  c.drawTextIn(*t.font.caption, label, parts[0], p.textMuted);
  c.drawTextIn(*t.font.body, split, parts[1], splitColor, Align::Right);
  c.drawTextIn(*t.font.caption, total, parts[2], p.textMuted, Align::Right);
  if (index < app.lapCount_ - 1) paintHairline(c, t, r.x + t.m.padding, r.bottom() - 1, r.w - 2 * t.m.padding);
}

void StopwatchApp::StopwatchScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  const Font &display = *t.font.display;
  const Color digitColor = fresh_ ? p.textMuted : p.text;
  if (hours_) {
    // An hour or more: seconds resolution, centred as a whole.
    c.drawTextIn(display, mainText_, Rect(digits_.x, digits_.y + t.dp(3), digits_.w, display.ascent), digitColor,
                 Align::Center);
  } else {
    c.drawTextIn(display, mainText_, Rect(mainRect_.x, mainRect_.y + t.dp(3), mainRect_.w, display.ascent), digitColor,
                 Align::Right);
    const Font &small = *t.font.title;
    c.drawText(small, tenthsText_, tenthsRect_.x + t.dp(2), mainRect_.y + t.dp(3) + display.ascent - t.dp(4), p.textMuted);
  }
  paintGlass(c, t, card_, t.m.radiusCard);
  if (app_.lapCount_ == 0) {
    c.drawWrapped(*t.font.caption, app_.running_ ? "Tap Lap to record a lap." : "Start, then Lap to record laps.",
                  hint_, p.textMuted, 3, Align::Center);
  } else {
    list_.draw(c, t, drawLap, this);
  }
  reset_.draw(c, t);
  start_.draw(c, t);
}

bool StopwatchApp::StopwatchScreen::onEvent(const Event &e, AppContext &ctx) {
  if (start_.handle(e, ctx)) {
    if (app_.running_) app_.stop(ctx);
    else app_.start(ctx);
    return true;
  }
  if (reset_.handle(e, ctx)) {
    if (app_.running_) app_.lap(ctx);
    else app_.reset(ctx);
    return true;
  }
  list_.handle(e, ctx);
  return false;
}

}  // namespace apps
}  // namespace stella
