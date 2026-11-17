#pragma once
// Turns raw touch samples into Press/Drag/Release/Tap/LongPress events.
//
// Resistive panels (XPT2046) drop individual samples while a finger is down,
// so a release is only reported after the contact has been absent for
// `releaseMs` (lesson carried over from Celestia Nightstand). Taps need a
// minimum contact time and must stay within the touch slop.
//
// Samples can be far apart (a long frame is rendered between polls) and a
// single reading can jump (landing, lift-off), so:
//  * contact time runs from the press to the first sample without contact,
//    never shorter than what was observed;
//  * a press becomes a drag only after two consecutive samples beyond the
//    slop, so one stray reading does not cancel a tap.
#include <stdint.h>

#include "../hal/hal.h"
#include "event.h"

namespace stella {

struct GestureConfig {
  uint16_t releaseMs = 60;     // contact must be gone this long to count as released
  uint16_t minTapMs = 30;      // shorter contacts are noise
  uint16_t longPressMs = 550;
  int16_t slop = 16;           // pixels of movement before a press becomes a drag
};

class GestureRecognizer {
 public:
  explicit GestureRecognizer(GestureConfig config = {}) : cfg_(config) {}
  void configure(const GestureConfig &config) { cfg_ = config; }

  // Feed one sample; writes up to 2 events into `out` and returns how many.
  int update(const TouchSample &sample, uint32_t nowMs, Event (&out)[2]);
  // Swallow the rest of the current gesture (e.g. the touch that wakes the
  // screen): no further events until the finger is released.
  void consumeCurrent() { consumed_ = true; }
  bool active() const { return down_; }
  // Earliest time update() needs to run again to fire a pending release or
  // long press (0 if nothing is pending).
  uint32_t nextDeadline() const;

 private:
  GestureConfig cfg_;
  bool down_ = false, dragging_ = false, longFired_ = false, consumed_ = false;
  bool beyondSlop_ = false;   // the previous sample was outside the slop
  bool absent_ = false;       // contact missing since firstAbsentMs_
  int16_t startX_ = 0, startY_ = 0, lastX_ = 0, lastY_ = 0;
  uint32_t pressedMs_ = 0, lastSeenMs_ = 0, firstAbsentMs_ = 0;
};

}  // namespace stella
