#pragma once
// Raw resistive-touch reading -> screen pixel. Pure and host-tested; boards
// hold one TouchCalibration per panel (see boards/*/).
#include <stdint.h>

namespace stella {

struct TouchCalibration {
  int16_t minX, maxX, minY, maxY;  // raw controller values at the screen edges
  bool swapAxes, invertX, invertY;
  uint16_t minPressure;            // lighter contact is treated as no touch
};

// Returns false for contact lighter than minPressure (x/y untouched).
inline bool mapTouch(const TouchCalibration &c, int rawX, int rawY, int pressure, int width, int height,
                     int &x, int &y) {
  if (pressure < c.minPressure) return false;
  if (c.swapAxes) {
    const int t = rawX;
    rawX = rawY;
    rawY = t;
  }
  long mx = c.maxX == c.minX ? 0 : (long)(rawX - c.minX) * (width - 1) / (c.maxX - c.minX);
  long my = c.maxY == c.minY ? 0 : (long)(rawY - c.minY) * (height - 1) / (c.maxY - c.minY);
  if (c.invertX) mx = width - 1 - mx;
  if (c.invertY) my = height - 1 - my;
  x = (int)(mx < 0 ? 0 : mx > width - 1 ? width - 1 : mx);
  y = (int)(my < 0 ? 0 : my > height - 1 ? height - 1 : my);
  return true;
}

}  // namespace stella
