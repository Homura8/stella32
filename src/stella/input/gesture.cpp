#include "gesture.h"

#include "../core/deadline.h"

namespace stella {
namespace {

Event make(EventType type, int x, int y, int sx, int sy, bool dragged = false) {
  Event e;
  e.type = type;
  e.x = (int16_t)x;
  e.y = (int16_t)y;
  e.startX = (int16_t)sx;
  e.startY = (int16_t)sy;
  e.dragged = dragged;
  return e;
}

inline int absi(int v) { return v < 0 ? -v : v; }

}  // namespace

uint32_t GestureRecognizer::nextDeadline() const {
  if (!down_) return 0;
  if (!dragging_ && !longFired_ && !consumed_) return pressedMs_ + cfg_.longPressMs;
  return lastSeenMs_ + cfg_.releaseMs;
}

int GestureRecognizer::update(const TouchSample &s, uint32_t now, Event (&out)[2]) {
  int n = 0;
  if (s.pressed) {
    lastSeenMs_ = now;
    if (!down_) {
      down_ = true;
      dragging_ = longFired_ = false;
      startX_ = lastX_ = s.x;
      startY_ = lastY_ = s.y;
      pressedMs_ = now;
      if (!consumed_) out[n++] = make(EventType::Press, s.x, s.y, s.x, s.y);
      return n;
    }
    lastX_ = s.x;
    lastY_ = s.y;
    if (consumed_) return 0;
    if (!dragging_ && (absi(s.x - startX_) > cfg_.slop || absi(s.y - startY_) > cfg_.slop)) dragging_ = true;
    if (dragging_) {
      out[n++] = make(EventType::Drag, s.x, s.y, startX_, startY_, true);
    } else if (!longFired_ && elapsedMs(now, pressedMs_) >= cfg_.longPressMs) {
      longFired_ = true;
      out[n++] = make(EventType::LongPress, lastX_, lastY_, startX_, startY_);
    }
    return n;
  }
  if (!down_) {
    consumed_ = false;
    return 0;
  }
  // No contact: wait for a stable release before ending the gesture.
  if (elapsedMs(now, lastSeenMs_) < cfg_.releaseMs) {
    if (!consumed_ && !dragging_ && !longFired_ && elapsedMs(now, pressedMs_) >= cfg_.longPressMs) {
      longFired_ = true;
      out[n++] = make(EventType::LongPress, lastX_, lastY_, startX_, startY_);
    }
    return n;
  }
  down_ = false;
  if (consumed_) {
    consumed_ = false;
    return 0;
  }
  out[n++] = make(EventType::Release, lastX_, lastY_, startX_, startY_, dragging_);
  const uint32_t held = elapsedMs(lastSeenMs_, pressedMs_);
  if (!dragging_ && !longFired_ && held >= cfg_.minTapMs) {
    out[n++] = make(EventType::Tap, startX_, startY_, startX_, startY_);
  }
  return n;
}

}  // namespace stella
