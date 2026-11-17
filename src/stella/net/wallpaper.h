#pragma once
// The wallpaper file: one 320x240 picture reduced to a 256-colour palette,
// made on the phone (the page behind the remote server converts any photo) and
// stored in a small flash area. Eight bits a pixel needs no decoder on the
// device and any row can be read in any order, which the chunked renderer
// needs (it asks for arbitrary rows).
//
//   16 bytes   header: "STWP", width, height, palette entries, reserved, CRC-32
//   512 bytes  256 palette entries, RGB565, little-endian
//   76,800     one palette index per pixel, row by row
//
// The CRC covers everything after the header.
#include <stddef.h>
#include <stdint.h>

#include "../hal/hal.h"

namespace stella {
namespace wallpaper {

constexpr uint32_t kMagic = 0x50575453u;  // "STWP" read as little-endian bytes S T W P
constexpr int kWidth = 320, kHeight = 240, kColors = 256;
constexpr size_t kHeaderBytes = 16, kPaletteBytes = kColors * 2;
constexpr size_t kPixelBytes = (size_t)kWidth * kHeight;
constexpr size_t kTotalBytes = kHeaderBytes + kPaletteBytes + kPixelBytes;  // 77,328

struct Header {
  uint32_t magic;
  uint16_t width, height, colors, reserved;
  uint32_t crc;
};
static_assert(sizeof(Header) == kHeaderBytes, "the header is 16 bytes");

// CRC-32 (the usual one: polynomial 0xEDB88320, as zlib and PNG), incremental:
// pass the previous result as `crc` to continue.
uint32_t crc32(const void *data, size_t size, uint32_t crc = 0);

// Reads the store, checks the header and the CRC, and (when valid) copies the
// palette. False when there is no valid picture.
bool load(WallpaperStore &store, uint16_t palette[kColors]);

// Parses the 16 header bytes of an upload; false if they are not a wallpaper
// this firmware understands.
bool parseHeader(const uint8_t bytes[kHeaderBytes], Header &out);

}  // namespace wallpaper
}  // namespace stella
