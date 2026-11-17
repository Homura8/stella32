#pragma once
// Events delivered to screens and apps on the UI thread. Small and copyable so
// they can travel through fixed-size queues.
#include <stdint.h>

namespace stella {

enum class EventType : uint8_t {
  None,
  // Pointer (from the gesture recognizer). x/y are screen pixels.
  Press,      // finger down (after debounce)
  Drag,       // moved beyond the touch slop; repeated while moving
  Release,    // finger up; `dragged` tells whether it was a drag
  Tap,        // short press-release without drag (delivered after Release)
  LongPress,  // held still for the long-press delay (once per gesture)
  // System
  Topic,      // a data topic changed; `id` is the topic id
};

struct Event {
  EventType type = EventType::None;
  bool dragged = false;
  uint16_t id = 0;
  int16_t x = 0, y = 0;            // current position
  int16_t startX = 0, startY = 0;  // where the gesture began

  bool isPointer() const { return type >= EventType::Press && type <= EventType::LongPress; }
};

}  // namespace stella
