#include "keyboard.h"

#include <string.h>

#include "materials.h"

namespace stella {
namespace {

const char *const kRows[4][3] = {
    {"qwertyuiop", "asdfghjkl", "zxcvbnm"},
    {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
    {"1234567890", "-/:;()$&@", ".,?!'\"_"},
    {"[]{}#%^*+=", "\\|~<>`;:@", ".,?!'\"_"},
};

}  // namespace

int Keyboard::layoutKeys(Key (&keys)[kMaxKeys]) const {
  const int m = (int)mode;
  const char *const *rows = kRows[m];
  const int rowH = bounds.h / 4;
  const int unit2 = bounds.w * 2 / 10;  // width of 1 key unit, doubled (half-units)
  int n = 0;
  // Places a key spanning `halfUnits` half key-units at half-unit offset `at`.
  auto place = [&](int row, int at, int halfUnits, char ch, KeyKind kind, const char *label) {
    const int x0 = bounds.x + at * unit2 / 4, x1 = bounds.x + (at + halfUnits) * unit2 / 4;
    keys[n++] = Key{Rect(x0, bounds.y + row * rowH, x1 - x0, row == 3 ? bounds.bottom() - (bounds.y + 3 * rowH) : rowH),
                    ch, kind, label};
  };
  // Rows 0-1: characters (row 1 is centred when it has nine keys).
  for (int row = 0; row < 2; ++row) {
    const int count = (int)strlen(rows[row]);
    const int offset = (20 - 2 * count) / 2;
    for (int i = 0; i < count; ++i) place(row, offset + 2 * i, 2, rows[row][i], Letter, nullptr);
  }
  // Row 2: shift / mode key, seven characters, backspace.
  const bool letters = mode == Mode::Lower || mode == Mode::Upper;
  place(2, 0, 3, 0, letters ? Shift : ModeSwitch, letters ? "shift" : mode == Mode::Symbols ? "#+=" : "123");
  for (int i = 0; i < 7; ++i) place(2, 3 + 2 * i, 2, rows[2][i], Letter, nullptr);
  place(2, 17, 3, 0, Back, "del");
  // Row 3: mode, space, full stop, done.
  place(3, 0, 4, 0, ModeSwitch, letters ? "123" : "abc");
  place(3, 4, 10, ' ', Space, "space");
  place(3, 14, 2, '.', Letter, nullptr);
  place(3, 16, 4, 0, DoneKey, doneLabel);
  return n;
}

Rect Keyboard::keyRect(int index) const {
  Key keys[kMaxKeys];
  const int n = layoutKeys(keys);
  return index >= 0 && index < n ? keys[index].r : Rect();
}

Rect Keyboard::keyFor(char ch) const {
  Key keys[kMaxKeys];
  const int n = layoutKeys(keys);
  for (int i = 0; i < n; ++i)
    if ((keys[i].kind == Letter || keys[i].kind == Space) && keys[i].ch == ch) return keys[i].r;
  return Rect();
}

Rect Keyboard::actionKey() const {
  Key keys[kMaxKeys];
  const int n = layoutKeys(keys);
  return keys[n - 1].r;
}

Rect Keyboard::modeKey() const {
  Key keys[kMaxKeys];
  const int n = layoutKeys(keys);
  return keys[n - 4].r;
}

Rect Keyboard::shiftKey() const {
  Key keys[kMaxKeys];
  layoutKeys(keys);
  const int first = (int)strlen(kRows[(int)mode][0]) + (int)strlen(kRows[(int)mode][1]);
  return keys[first].r;
}

int Keyboard::keyAt(int x, int y) const {
  Key keys[kMaxKeys];
  const int n = layoutKeys(keys);
  for (int i = 0; i < n; ++i)
    if (keys[i].r.contains(x, y)) return i;
  return -1;
}

void Keyboard::draw(Canvas &c, const Theme &t) const {
  if (!c.visible(bounds)) return;
  const Palette &p = t.color;
  Key keys[kMaxKeys];
  const int n = layoutKeys(keys);
  const int gap = t.dp(2) > 2 ? t.dp(2) : 2;
  for (int i = 0; i < n; ++i) {
    const Key &k = keys[i];
    if (!c.visible(k.r)) continue;
    const Rect face = k.r.inset(gap, gap);
    const int radius = t.dp(7);
    const bool special = k.kind != Letter && k.kind != Space;
    const bool active = (k.kind == Shift && mode == Mode::Upper);
    Color text = p.text;
    if (k.kind == DoneKey) {
      c.fillRoundRect(face, radius, doneEnabled ? p.accent : p.line, doneEnabled ? 255 : (uint8_t)(p.lineAlpha * 2));
      text = doneEnabled ? p.accentText : p.textMuted;
    } else {
      c.fillRoundRect(face, radius, special ? p.surface : p.glass, special ? 160 : p.glassAlpha);
      c.strokeRoundRect(face, radius, 1, p.rim, p.rimAlpha / 2);
      if (active) c.fillRoundRect(face, radius, p.accent, 70);
      if (special) text = active ? p.accent : p.textMuted;
    }
    if (i == pressed_) c.fillRoundRect(face, radius, p.accent, 64);
    char single[2] = {k.ch, 0};
    const Font &f = k.kind == Letter ? *t.font.body : *t.font.caption;
    c.drawTextIn(f, k.label ? k.label : single, face, text, Align::Center);
  }
}

KeyInput Keyboard::handle(const Event &e, Invalidator &inv) {
  KeyInput out;
  switch (e.type) {
    case EventType::Press:
      pressed_ = keyAt(e.x, e.y);
      if (pressed_ >= 0) inv.invalidate(keyRect(pressed_));
      return out;
    case EventType::Drag:
    case EventType::Release:
      if (pressed_ >= 0) {
        inv.invalidate(keyRect(pressed_));
        pressed_ = -1;
      }
      return out;
    case EventType::LongPress: {
      Key keys[kMaxKeys];
      const int n = layoutKeys(keys);
      const int i = keyAt(e.x, e.y);
      if (i >= 0 && i < n && keys[i].kind == Back) out.kind = KeyInput::Clear;
      return out;
    }
    case EventType::Tap: {
      Key keys[kMaxKeys];
      const int n = layoutKeys(keys);
      const int i = keyAt(e.x, e.y);
      if (i < 0 || i >= n) return out;
      const Key &k = keys[i];
      switch (k.kind) {
        case Letter:
        case Space:
          out.kind = KeyInput::Char;
          out.ch = k.ch;
          if (mode == Mode::Upper) {  // one-shot shift
            mode = Mode::Lower;
            inv.invalidate(bounds);
          }
          break;
        case Shift:
          mode = mode == Mode::Upper ? Mode::Lower : Mode::Upper;
          inv.invalidate(bounds);
          break;
        case ModeSwitch:
          mode = (mode == Mode::Lower || mode == Mode::Upper) ? Mode::Symbols
                 : (k.label && strcmp(k.label, "#+=") == 0) ? Mode::More
                 : (k.label && strcmp(k.label, "123") == 0) ? Mode::Symbols
                                                            : Mode::Lower;
          inv.invalidate(bounds);
          break;
        case Back:
          out.kind = KeyInput::Backspace;
          break;
        case DoneKey:
          if (doneEnabled) out.kind = KeyInput::Done;
          break;
      }
      return out;
    }
    default:
      return out;
  }
}

}  // namespace stella
