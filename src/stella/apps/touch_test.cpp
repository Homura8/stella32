#include "touch_test.h"

#include <stdio.h>

#include "../ui/layout.h"
#include "../ui/materials.h"

namespace stella {
namespace apps {

using namespace layout;

const AppInfo &TouchTestApp::info() const {
  static const AppInfo kInfo{"touch", "Touch test", icons::touch, 0, 0};
  return kInfo;
}

void TouchTestApp::TouchScreen::layout(const Rect &content, const Theme &t) {
  Rect r[2];
  column(content.inset(t.m.margin, t.m.gapS), {flex(), px(t.font.caption->lineHeight)}, t.m.gapS, r);
  pad_ = r[0];
  readout_ = r[1];
}

void TouchTestApp::TouchScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  paintGlass(c, t, pad_, t.m.radiusCard, Elevation::Flat);
  // Corner targets: a well-calibrated panel puts touches on their centres.
  const int inset = t.dp(14), r = t.dp(5);
  const Point corners[4] = {{(int16_t)(pad_.x + inset), (int16_t)(pad_.y + inset)},
                            {(int16_t)(pad_.right() - inset), (int16_t)(pad_.y + inset)},
                            {(int16_t)(pad_.x + inset), (int16_t)(pad_.bottom() - inset)},
                            {(int16_t)(pad_.right() - inset), (int16_t)(pad_.bottom() - inset)}};
  for (const Point &k : corners) c.strokeCircle(k.x, k.y, r, 1, p.textMuted);
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
  c.drawTextIn(*t.font.caption, text_, readout_, p.textMuted, Align::Center);
}

bool TouchTestApp::TouchScreen::onEvent(const Event &e, AppContext &ctx) {
  if (!e.isPointer() || !pad_.contains(e.x, e.y)) return false;
  if (e.type == EventType::LongPress) {
    count_ = 0;
    last_ = Point{-1, -1};
    snprintf(text_, sizeof(text_), "Cleared");
    ctx.invalidate(pad_);
    ctx.invalidate(readout_);
    return true;
  }
  if (e.type == EventType::Press || e.type == EventType::Drag) {
    // Repaint only around the old and new crosshair lines and the new dot.
    const int r = ctx.theme().dp(12);
    if (last_.x >= 0) {
      ctx.invalidate(Rect(pad_.x, last_.y - 1, pad_.w, 3));
      ctx.invalidate(Rect(last_.x - 1, pad_.y, 3, pad_.h));
      ctx.invalidate(Rect(last_.x - r, last_.y - r, 2 * r, 2 * r));
    }
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
