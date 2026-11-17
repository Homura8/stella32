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

// Blend `fg` over `bg` with coverage 0..255 (per-channel, rounded).
constexpr Color blend(Color bg, Color fg, uint8_t alpha) {
  return alpha == 0 ? bg : alpha == 255 ? fg
      : (Color)(((((fg >> 11) & 31) * alpha + ((bg >> 11) & 31) * (255 - alpha) + 127) / 255) << 11
              | ((((fg >> 5) & 63) * alpha + ((bg >> 5) & 63) * (255 - alpha) + 127) / 255) << 5
              | (((fg & 31) * alpha + (bg & 31) * (255 - alpha) + 127) / 255));
}

// Halve brightness (used for modal scrims). Exact for every RGB565 value.
constexpr Color darken50(Color c) { return (Color)((c & 0xF7DE) >> 1); }

}  // namespace stella
