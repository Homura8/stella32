#pragma once
// Celestia Glow materials: the pastel sky backdrop and frosted-glass surfaces.
// All painters are pure functions of (theme, geometry, pixel position), so any
// dirty sub-rectangle repaints identically to a full-screen redraw.
#include "../gfx/canvas.h"
#include "theme.h"

namespace stella {

// Diagonal three-stop sky gradient with soft pink / tint / gold washes, ordered
// dithering to avoid RGB565 banding. `screen` is the full display rectangle.
void paintSky(Canvas &c, const Theme &t, const Rect &screen);

enum class Elevation : uint8_t { Flat, Raised };

// Frosted card: soft shadow, translucent glass fill, 1 px highlight rim.
void paintGlass(Canvas &c, const Theme &t, const Rect &r, int radius, Elevation e = Elevation::Raised);
// How far shadows reach outside a raised surface (add to invalidation rects).
int glassOutset(const Theme &t);

// Hairline separator using the theme line colour.
void paintHairline(Canvas &c, const Theme &t, int x, int y, int w);

// Pastel circle behind an icon (tint index 0..3).
void paintBubble(Canvas &c, const Theme &t, int cx, int cy, int radius, uint8_t tint);

}  // namespace stella
