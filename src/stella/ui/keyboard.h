#pragma once
// On-screen keyboard. Geometry is derived from `bounds` (four rows, ten key
// units wide), so it fits any panel; keys repaint individually on press.
//   lower / upper (one-shot shift) / symbols / more symbols
// Long-pressing backspace clears the whole field.
#include <stdint.h>

#include "../gfx/canvas.h"
#include "../input/event.h"
#include "theme.h"
#include "widgets.h"

namespace stella {

struct KeyInput {
  enum Kind : uint8_t { None, Char, Backspace, Clear, Done };
  Kind kind = None;
  char ch = 0;
};

class Keyboard {
 public:
  enum class Mode : uint8_t { Lower, Upper, Symbols, More };
  static constexpr int kMaxKeys = 34;

  Rect bounds;
  Mode mode = Mode::Lower;
  const char *doneLabel = "Done";
  bool doneEnabled = true;

  void draw(Canvas &c, const Theme &t) const;
  KeyInput handle(const Event &e, Invalidator &inv);
  // Where a character's key is in the current mode (empty if not present),
  // and the action (Done) key. For tests and key-highlighting.
  Rect keyFor(char ch) const;
  Rect actionKey() const;
  Rect modeKey() const;   // bottom-left mode switch
  Rect shiftKey() const;  // row-3 shift / symbol-page key

 private:
  enum KeyKind : uint8_t { Letter, Shift, Back, ModeSwitch, Space, DoneKey };
  struct Key {
    Rect r;
    char ch;
    KeyKind kind;
    const char *label;
  };
  int layoutKeys(Key (&keys)[kMaxKeys]) const;
  int keyAt(int x, int y) const;
  Rect keyRect(int index) const;

  int pressed_ = -1;
};

}  // namespace stella
