#include "timer_app.h"

#include <stdio.h>
#include <string.h>

#include "../core/deadline.h"
#include "../ui/layout.h"
#include "../ui/materials.h"

namespace stella {
namespace apps {

using namespace layout;

namespace {

const char *const kPresetLabels[] = {"1 min", "5 min", "10 min", "15 min", "30 min"};
constexpr uint32_t kPresetMinutes[] = {1, 5, 10, 15, 30};
constexpr uint8_t kPresets = sizeof(kPresetMinutes) / sizeof(kPresetMinutes[0]);
constexpr uint32_t kMinuteMs = 60000;

// "5 min", "1 h 30 min".
void describeDuration(uint32_t ms, char *out, size_t capacity) {
  const uint32_t minutes = (ms + kMinuteMs - 1) / kMinuteMs;
  if (minutes < 60) snprintf(out, capacity, "%lu min", (unsigned long)minutes);
  else if (minutes % 60) snprintf(out, capacity, "%lu h %lu min", (unsigned long)(minutes / 60), (unsigned long)(minutes % 60));
  else snprintf(out, capacity, "%lu h", (unsigned long)(minutes / 60));
}

}  // namespace

TimerApp::TimerApp() : screen_(*this) {}

const AppInfo &TimerApp::info() const {
  static const AppInfo kInfo{"timer", "Timer", icons::hourglass, 2, 0};
  return kInfo;
}

uint32_t TimerApp::remaining(uint32_t nowMs) const { return running_ ? remainingMs(nowMs, endAt_) : remainingMs_; }

uint32_t TimerApp::onWake(AppContext &ctx) {
  if (!running_) return kNoTick;
  const uint32_t left = remaining(ctx.millis());
  if (left > 0) return left;
  running_ = false;
  remainingMs_ = durationMs_;
  char message[40];
  describeDuration(durationMs_, message, sizeof(message));
  strncat(message, " timer", sizeof(message) - strlen(message) - 1);
  Alert alert;
  alert.title = "Time's up";
  alert.message = message;
  alert.icon = icons::hourglass;
  alert.primary = "Done";
  alert.secondary = "Again";
  alert.tag = kDoneTag;
  alert.sound = true;
  alert.timeoutMs = kRingMs;
  ctx.showAlert(alert);
  if (screen_.isVisible()) screen_.refresh(ctx);
  return kNoTick;
}

void TimerApp::onAlert(uint16_t tag, int action, AppContext &ctx) {
  if (tag == kDoneTag && action == kAlertSecondary) start(ctx);
}

void TimerApp::setDuration(uint32_t ms, AppContext &ctx) {
  durationMs_ = ms > kMaxMs ? kMaxMs : ms;
  running_ = false;
  remainingMs_ = durationMs_;
  if (screen_.isVisible()) screen_.refresh(ctx);
}

void TimerApp::start(AppContext &ctx) {
  if (running_ || remainingMs_ == 0) return;
  running_ = true;
  endAt_ = ctx.millis() + remainingMs_;
  ctx.scheduleWake(remainingMs_);
  if (screen_.isVisible()) {
    screen_.refresh(ctx);
    ctx.requestTick();  // the countdown ticks while visible
  }
}

void TimerApp::pause(AppContext &ctx) {
  if (!running_) return;
  remainingMs_ = remaining(ctx.millis());
  running_ = false;
  if (screen_.isVisible()) screen_.refresh(ctx);
}

void TimerApp::reset(AppContext &ctx) {
  running_ = false;
  remainingMs_ = durationMs_;
  if (screen_.isVisible()) screen_.refresh(ctx);
}

void TimerApp::addMinute(AppContext &ctx) {
  const uint32_t now = ctx.millis();
  if (remaining(now) + kMinuteMs > kMaxMs) return;
  durationMs_ = durationMs_ + kMinuteMs > kMaxMs ? kMaxMs : durationMs_ + kMinuteMs;
  if (running_) endAt_ += kMinuteMs;
  else remainingMs_ += kMinuteMs;
  if (screen_.isVisible()) screen_.refresh(ctx);
}

// ------------------------------------------------------------------ screen

void TimerApp::TimerScreen::layout(const Rect &content, const Theme &t) {
  const Font &display = *t.font.display;
  Rect r[7];
  column(content.inset(t.m.margin, t.m.gapS),
         {flex(), px(display.ascent + t.dp(6)), px(t.dp(6)), px(t.font.caption->lineHeight), flex(2), px(t.m.control),
          px(t.m.control)},
         t.m.gapM, r);
  digits_ = r[1];
  const int barW = r[2].w * 2 / 3;
  progress_ = center(r[2], barW, r[2].h);
  caption_ = r[3];
  presets_.bounds = r[5];
  presets_.labels = kPresetLabels;
  presets_.count = kPresets;
  Rect b[3];
  row(r[6], {flex(), flex(), flex()}, t.m.gapM, b);
  reset_.bounds = b[0];
  reset_.label = "Reset";
  plus_.bounds = b[1];
  plus_.label = "+1 min";
  start_.bounds = b[2];
  start_.style = ButtonStyle::Accent;
}

void TimerApp::TimerScreen::onShow(AppContext &ctx) {
  for (uint8_t i = 0; i < kPresets; ++i)
    if (app_.durationMs_ == kPresetMinutes[i] * kMinuteMs) presets_.selected = i;
  progressW_ = -1;
  refresh(ctx);
}

uint32_t TimerApp::TimerScreen::onTick(AppContext &ctx) {
  refresh(ctx);
  if (!app_.running_) return kNoTick;
  // Just after the displayed second changes (the display rounds up).
  return app_.remaining(ctx.millis()) % 1000u + 5u;
}

void TimerApp::TimerScreen::refresh(AppContext &ctx) {
  const uint32_t now = ctx.millis();
  const uint32_t left = app_.remaining(now);
  const uint32_t seconds = (left + 999) / 1000;
  char digits[sizeof(digitsText_)], caption[sizeof(captionText_)];
  if (seconds >= 3600) {
    snprintf(digits, sizeof(digits), "%lu:%02lu:%02lu", (unsigned long)(seconds / 3600),
             (unsigned long)(seconds / 60 % 60), (unsigned long)(seconds % 60));
  } else {
    snprintf(digits, sizeof(digits), "%02lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
  }
  if (app_.running_) {
    LocalTime end = ctx.localTime();
    if (end.valid) {
      const int minutes = end.hour * 60 + end.minute + (int)((end.second + seconds) / 60);
      end.hour = minutes / 60 % 24;
      end.minute = minutes % 60;
      char time[12];
      ctx.formatTime(end, time, sizeof(time));
      snprintf(caption, sizeof(caption), "Ends at %s", time);
    } else {
      snprintf(caption, sizeof(caption), "Running");
    }
  } else if (left < app_.durationMs_) {
    snprintf(caption, sizeof(caption), "Paused");
  } else {
    char duration[24];
    describeDuration(app_.durationMs_, duration, sizeof(duration));
    snprintf(caption, sizeof(caption), "%s timer", duration);
  }
  // The digits are drawn bright while running and muted otherwise, so a change of state
  // repaints them even when the numbers are the same (pausing, or finishing).
  if (strcmp(digits, digitsText_) || app_.running_ != digitsRunning_) {
    memcpy(digitsText_, digits, sizeof(digits));
    digitsRunning_ = app_.running_;
    ctx.invalidate(digits_);
  }
  if (strcmp(caption, captionText_)) {
    memcpy(captionText_, caption, sizeof(caption));
    ctx.invalidate(caption_);
  }
  const int width = app_.durationMs_ ? (int)((uint64_t)progress_.w * left / app_.durationMs_) : 0;
  if (width != progressW_) {
    progressW_ = width;
    ctx.invalidate(progress_);
  }
  const char *label = app_.running_ ? "Pause" : left < app_.durationMs_ ? "Resume" : "Start";
  if (strcmp(label, start_.label) != 0) {
    start_.label = label;
    ctx.invalidate(start_.bounds.inset(-glassOutset(ctx.theme())));
  }
  const bool canReset = app_.running_ || left != app_.durationMs_;
  if (canReset != reset_.enabled) {
    reset_.enabled = canReset;
    ctx.invalidate(reset_.bounds.inset(-glassOutset(ctx.theme())));
  }
}

void TimerApp::TimerScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  const Font &display = *t.font.display;
  c.drawTextIn(display, digitsText_, Rect(digits_.x, digits_.y + t.dp(3), digits_.w, display.ascent),
               app_.running_ ? p.text : p.textMuted, Align::Center);
  const int r = progress_.h / 2;
  c.fillRoundRect(progress_, r, p.line, (uint8_t)(p.lineAlpha * 2));
  if (progressW_ > 0) c.fillRoundRect(Rect(progress_.x, progress_.y, progressW_ > 2 * r ? progressW_ : 2 * r, progress_.h), r, p.accent);
  c.drawTextIn(*t.font.caption, captionText_, caption_, p.textMuted, Align::Center);
  presets_.draw(c, t);
  reset_.draw(c, t);
  plus_.draw(c, t);
  start_.draw(c, t);
}

bool TimerApp::TimerScreen::onEvent(const Event &e, AppContext &ctx) {
  if (presets_.handle(e, ctx)) {
    app_.setDuration(kPresetMinutes[presets_.selected] * kMinuteMs, ctx);
    return true;
  }
  if (start_.handle(e, ctx)) {
    if (app_.running_) app_.pause(ctx);
    else app_.start(ctx);
    return true;
  }
  if (plus_.handle(e, ctx)) {
    app_.addMinute(ctx);
    return true;
  }
  if (reset_.handle(e, ctx)) {
    app_.reset(ctx);
    return true;
  }
  return false;
}

}  // namespace apps
}  // namespace stella
