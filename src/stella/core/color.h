#pragma once
// RGB565 colour helpers. Pixels are stored in native CPU order; display drivers
// perform any byte swapping their bus needs.
#include <stdint.h>

namespace stella {

using Color = uint16_t;

constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (Color)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr Color rgbHex(uint32_t hex) { return rgb((hex >> 16) & 0xff, (hex >> 8) & 0xff, hex & 0xff); }

constexpr uint8_t red8(Color c) { return (uint8_t)(((c >> 11) & 31) * 255 / 31); }
constexpr uint8_t green8(Color c) { return (uint8_t)(((c >> 5) & 63) * 255 / 63); }
constexpr uint8_t blue8(Color c) { return (uint8_t)((c & 31) * 255 / 31); }

// Blend `fg` over `bg` by `a32` in 0..32, on packed RGB565 with guard bits:
// one multiply per pixel. Per channel: bg + floor((fg - bg) * a32 / 32).
inline Color blend32(Color bg, Color fg, uint32_t a32) {
  const uint32_t b = (bg | ((uint32_t)bg << 16)) & 0x07E0F81Fu;
  const uint32_t f = (fg | ((uint32_t)fg << 16)) & 0x07E0F81Fu;
  const uint32_t r = ((((f - b) * a32) >> 5) + b) & 0x07E0F81Fu;
  return (Color)(r | (r >> 16));
}

// Blend `fg` over `bg` with coverage 0..255 (quantised to 33 levels).
inline Color blend(Color bg, Color fg, uint8_t alpha) {
  if (alpha < 4) return bg;
  if (alpha > 251) return fg;
  return blend32(bg, fg, ((uint32_t)alpha + 4) >> 3);
}

// Halve brightness (used for modal scrims). Exact for every RGB565 value.
constexpr Color darken50(Color c) { return (Color)((c & 0xF7DE) >> 1); }

}  // namespace stella
