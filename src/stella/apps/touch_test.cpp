#include "touch_test.h"

#include <stdio.h>

#include "../ui/layout.h"
#include "../ui/materials.h"

namespace stella {
namespace apps {

using namespace layout;

namespace {
const char *kIdleText = "Touch anywhere \xc2\xb7 hold to clear";
}

bool fitTouchAxis(const int *measured, const int *target, int n, int16_t &scale1024, int16_t &offset) {
  if (n < 2) return false;
  float mm = 0.f, mt = 0.f;
  for (int i = 0; i < n; ++i) {
    mm += (float)measured[i];
    mt += (float)target[i];
  }
  mm /= (float)n;
  mt /= (float)n;
  float cov = 0.f, var = 0.f;
  for (int i = 0; i < n; ++i) {
    cov += (measured[i] - mm) * (target[i] - mt);
    var += (measured[i] - mm) * (measured[i] - mm);
  }
  if (var < 1.f) return false;
  const float a = cov / var, b = mt - a * mm;
  if (a < 0.8f || a > 1.25f || b < -60.f || b > 60.f) return false;
  scale1024 = (int16_t)(a * 1024.f + (a > 0 ? 0.5f : -0.5f));
  offset = (int16_t)(b + (b > 0 ? 0.5f : -0.5f));
  return true;
}

const AppInfo &TouchTestApp::info() const {
  static const AppInfo kInfo{"touch", "Touch test", icons::touch, 0, kAppHidden};  // opened from Settings > Display > Touch;
  return kInfo;
}

void TouchTestApp::TouchScreen::layout(const Rect &content, const Theme &t) {
  theme_ = t;
  Rect r[2];
  column(content.inset(t.m.margin, t.m.gapS), {flex(), px(t.m.control)}, t.m.gapS, r);
  pad_ = r[0];
  Rect bottom[2];
  row(r[1], {flex(), px(t.dp(104))}, t.m.gapM, bottom);
  readout_ = bottom[0];
  calibrate_.bounds = bottom[1];
}

void TouchTestApp::TouchScreen::clear() {
  count_ = 0;
  head_ = 0;
  last_ = Point{-1, -1};
  snprintf(text_, sizeof(text_), "%s", kIdleText);
}

void TouchTestApp::TouchScreen::onShow(AppContext &) {
  clear();
  calibrate_.label = "Calibrate";
}

void TouchTestApp::TouchScreen::onHide(AppContext &ctx) {
  if (calibrating()) cancelCalibration(ctx);
  clear();  // leaving the test clears the trail
}

Point TouchTestApp::TouchScreen::target(int i) const {
  const int inset = theme_.dp(24);
  const int x = (i == 0 || i == 3) ? pad_.x + inset : pad_.right() - 1 - inset;
  const int y = i < 2 ? pad_.y + inset : pad_.bottom() - 1 - inset;
  return Point{(int16_t)x, (int16_t)y};
}

void TouchTestApp::TouchScreen::startCalibration(AppContext &ctx) {
  saved_ = ctx.settings();
  SystemSettings raw = saved_;  // measure uncorrected positions
  raw.touchScaleX = raw.touchScaleY = 1024;
  raw.touchOffsetX = raw.touchOffsetY = 0;
  ctx.applySettings(raw);
  step_ = 0;
  pressAt_ = Point{-1, -1};
  count_ = 0;
  last_ = Point{-1, -1};
  calibrate_.label = "Cancel";
  snprintf(text_, sizeof(text_), "Tap the centre of the cross \xc2\xb7 1 of 4");
  ctx.invalidate(ctx.contentRect());
}

void TouchTestApp::TouchScreen::cancelCalibration(AppContext &ctx) {
  step_ = -1;
  ctx.applySettings(saved_);
  calibrate_.label = "Calibrate";
  snprintf(text_, sizeof(text_), "%s", kIdleText);
  ctx.invalidate(ctx.contentRect());
}

void TouchTestApp::TouchScreen::finishCalibration(AppContext &ctx) {
  step_ = -1;
  calibrate_.label = "Calibrate";
  int targetX[4], targetY[4];
  for (int i = 0; i < 4; ++i) {
    targetX[i] = target(i).x;
    targetY[i] = target(i).y;
  }
  SystemSettings s = saved_;
  if (fitTouchAxis(measuredX_, targetX, 4, s.touchScaleX, s.touchOffsetX) &&
      fitTouchAxis(measuredY_, targetY, 4, s.touchScaleY, s.touchOffsetY)) {
    ctx.applySettings(s);
    snprintf(text_, sizeof(text_), "Calibrated \xc2\xb7 shift %+d, %+d px", s.touchOffsetX, s.touchOffsetY);
    ctx.toast("Touch calibrated");
  } else {
    ctx.applySettings(saved_);
    snprintf(text_, sizeof(text_), "That didn't fit \xc2\xb7 try again");
  }
  ctx.invalidate(ctx.contentRect());
}

void TouchTestApp::TouchScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  paintGlass(c, t, pad_, t.m.radiusCard, Elevation::Flat);
  if (calibrating()) {
    for (int i = 0; i < 4; ++i) {
      const Point k = target(i);
      const bool active = i == step_;
      const Color color = active ? p.accent : blend(p.glass, p.textMuted, 90);
      const int arm = t.dp(active ? 14 : 8);
      c.hline(k.x - arm, k.y, 2 * arm + 1, color);
      c.vline(k.x, k.y - arm, 2 * arm + 1, color);
      if (active) c.strokeCircle(k.x, k.y, t.dp(9), 2, color);
    }
  } else {
    // Corner targets: a well-calibrated panel puts touches on their centres.
    for (int i = 0; i < 4; ++i) {
      const Point k = target(i);
      c.strokeCircle(k.x, k.y, t.dp(5), 1, p.textMuted);
    }
    for (int i = 0; i < count_; ++i) {
      const Point &q = trail_[(head_ - count_ + i + kTrail) % kTrail];
      const uint8_t alpha = (uint8_t)(60 + 195 * (i + 1) / count_);
      c.fillCircle(q.x, q.y, t.dp(3), p.accent, alpha);
    }
    if (last_.x >= 0) {
      c.strokeCircle(last_.x, last_.y, t.dp(10), 2, p.accent);
      c.hline(pad_.x, last_.y, pad_.w, p.accent, 70);
      c.vline(last_.x, pad_.y, pad_.h, p.accent, 70);
    }
  }
  c.drawTextIn(*t.font.caption, text_, readout_, p.textMuted);
  calibrate_.draw(c, t);
}

bool TouchTestApp::TouchScreen::onEvent(const Event &e, AppContext &ctx) {
  if (calibrate_.handle(e, ctx)) {
    if (calibrating()) cancelCalibration(ctx);
    else startCalibration(ctx);
    return true;
  }
  if (!e.isPointer() || !pad_.contains(e.startX, e.startY)) return false;
  if (calibrating()) {
    // Each cross: the mean of where the touch landed and where it lifted.
    if (e.type == EventType::Press) pressAt_ = Point{e.x, e.y};
    if (e.type == EventType::Release && pressAt_.x >= 0) {
      measuredX_[step_] = (pressAt_.x + e.x) / 2;
      measuredY_[step_] = (pressAt_.y + e.y) / 2;
      pressAt_ = Point{-1, -1};
      if (++step_ == 4) {
        finishCalibration(ctx);
      } else {
        snprintf(text_, sizeof(text_), "Tap the centre of the cross \xc2\xb7 %d of 4", step_ + 1);
        ctx.invalidate(pad_);
        ctx.invalidate(readout_);
      }
    }
    return true;
  }
  if (e.type == EventType::LongPress) {
    clear();
    snprintf(text_, sizeof(text_), "Cleared");
    ctx.invalidate(pad_);
    ctx.invalidate(readout_);
    return true;
  }
  if (e.type == EventType::Press || e.type == EventType::Drag) {
    last_ = Point{e.x, e.y};
    trail_[head_] = last_;
    head_ = (head_ + 1) % kTrail;
    if (count_ < kTrail) ++count_;
    // Older dots fade one step: the whole trail changes, so repaint the pad.
    ctx.invalidate(pad_);
    snprintf(text_, sizeof(text_), "x %d  y %d", e.x, e.y);
    ctx.invalidate(readout_);
    return true;
  }
  return false;
}

}  // namespace apps
}  // namespace stella
