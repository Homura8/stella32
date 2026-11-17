#include "wallpaper.h"

#include <string.h>

namespace stella {
namespace wallpaper {

uint32_t crc32(const void *data, size_t size, uint32_t crc) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  crc = ~crc;
  for (size_t i = 0; i < size; ++i) {
    crc ^= p[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

bool parseHeader(const uint8_t bytes[kHeaderBytes], Header &out) {
  memcpy(&out, bytes, sizeof(out));  // little-endian on the ESP32 and the host
  return out.magic == kMagic && out.width == kWidth && out.height == kHeight && out.colors == kColors;
}

bool load(WallpaperStore &store, uint16_t palette[kColors]) {
  if (store.capacity() < kTotalBytes) return false;
  uint8_t head[kHeaderBytes];
  Header h;
  if (!store.read(0, head, sizeof(head)) || !parseHeader(head, h)) return false;
  uint32_t crc = 0;
  uint8_t buffer[256];
  for (size_t offset = kHeaderBytes; offset < kTotalBytes;) {
    const size_t n = kTotalBytes - offset < sizeof(buffer) ? kTotalBytes - offset : sizeof(buffer);
    if (!store.read(offset, buffer, n)) return false;
    crc = crc32(buffer, n, crc);
    offset += n;
  }
  if (crc != h.crc) return false;
  uint8_t raw[kPaletteBytes];
  if (!store.read(kHeaderBytes, raw, sizeof(raw))) return false;
  for (int i = 0; i < kColors; ++i) palette[i] = (uint16_t)(raw[2 * i] | (raw[2 * i + 1] << 8));
  return true;
}

}  // namespace wallpaper
}  // namespace stella
