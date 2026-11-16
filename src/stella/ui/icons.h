#pragma once
// Procedural line icons. Drawn from geometry rather than bitmaps, so they cost
// a few hundred bytes of code, scale to any density and stay anti-aliased.
// Each icon fills `box` (keep it square) using stroke weights relative to size.
#include "../gfx/canvas.h"

namespace stella {

using IconFn = void (*)(Canvas &c, const Rect &box, Color color);

namespace icons {
void sparkle(Canvas &c, const Rect &box, Color color);  // Celestia four-point star
void clock(Canvas &c, const Rect &box, Color color);
void gear(Canvas &c, const Rect &box, Color color);
void touch(Canvas &c, const Rect &box, Color color);
void back(Canvas &c, const Rect &box, Color color);
void chevronRight(Canvas &c, const Rect &box, Color color);
void home(Canvas &c, const Rect &box, Color color);
void info(Canvas &c, const Rect &box, Color color);
void moon(Canvas &c, const Rect &box, Color color);
void sun(Canvas &c, const Rect &box, Color color);
void wifi(Canvas &c, const Rect &box, Color color);
void appearance(Canvas &c, const Rect &box, Color color);
void check(Canvas &c, const Rect &box, Color color);
}  // namespace icons

}  // namespace stella
