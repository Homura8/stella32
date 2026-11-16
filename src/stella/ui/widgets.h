#pragma once
// Celestia Glow controls. Widgets are small value types owned by a screen:
// the screen assigns `bounds` in layout(), calls draw() from draw(), and
// forwards events to handle(), which returns true when the user acted.
// Widgets invalidate their own area on visual state changes.
#include <stdint.h>

#include "../gfx/canvas.h"
#include "../input/event.h"
#include "icons.h"
#include "theme.h"

namespace stella {

class Invalidator {
 public:
  virtual ~Invalidator() = default;
  virtual void invalidate(const Rect &area) = 0;
};

enum class ButtonStyle : uint8_t { Glass, Accent, Quiet };

struct Button {
  Rect bounds;
  const char *label = "";
  IconFn icon = nullptr;
  ButtonStyle style = ButtonStyle::Glass;
  bool enabled = true;
  bool pressed = false;

  void draw(Canvas &c, const Theme &t) const;
  bool handle(const Event &e, Invalidator &inv);  // true on activation
};

// Pill switch, like the site's light/dark toggle.
struct Toggle {
  Rect bounds;  // the whole row area that accepts taps
  bool on = false;

  Rect track(const Theme &t) const;  // the visible switch, right-aligned in bounds
  void draw(Canvas &c, const Theme &t) const;
  bool handle(const Event &e, Invalidator &inv);  // true when toggled
};

struct Slider {
  Rect bounds;
  int value = 0, minimum = 0, maximum = 100;
  bool dragging = false;

  void draw(Canvas &c, const Theme &t) const;
  bool handle(const Event &e, Invalidator &inv, const Theme &t);  // true when value changed
};

// Row of mutually exclusive choices, like the site's sky chooser.
struct Segmented {
  Rect bounds;
  const char *const *labels = nullptr;
  uint8_t count = 0;
  uint8_t selected = 0;

  Rect segment(int i) const;
  void draw(Canvas &c, const Theme &t) const;
  bool handle(const Event &e, Invalidator &inv);  // true when selection changed
};

// One row inside a glass group: icon bubble, title, optional detail and chevron.
struct ListRow {
  Rect bounds;
  const char *title = "";
  const char *detail = nullptr;  // right-aligned value
  IconFn icon = nullptr;
  uint8_t tint = 0;
  bool chevron = true;
  bool separator = false;  // hairline under the row
  bool pressed = false;

  void draw(Canvas &c, const Theme &t) const;
  bool handle(const Event &e, Invalidator &inv);  // true on tap
};

// Vertically scrolling list of fixed-height rows (drag to scroll, tap to pick).
// Rows are drawn by a callback, so any row design works and nothing is copied.
struct ListView {
  using DrawRow = void (*)(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed, void *context);

  Rect bounds;
  int rowHeight = 40;
  int count = 0;
  int offset = 0;     // scroll position in pixels
  int pressed = -1;   // row under the finger, -1 if none

  int maxOffset() const;
  void scrollTo(int index);  // make row `index` visible
  void draw(Canvas &c, const Theme &t, DrawRow drawRow, void *context) const;
  // Returns the tapped row index, or -1.
  int handle(const Event &e, Invalidator &inv);

 private:
  int dragStartOffset_ = 0;
  bool dragging_ = false;
};

// Shared pressed-state logic for tappable areas.
bool trackPress(const Rect &hit, const Event &e, bool &pressed, Invalidator &inv, const Rect &repaint);

}  // namespace stella
