#include "text.h"

#include <string.h>

namespace stella {

bool copyText(char *dst, size_t capacity, const char *src) {
  if (!dst || capacity == 0) return false;
  if (!src) src = "";
  size_t n = strnlen(src, capacity);
  const bool fits = n < capacity;
  if (!fits) {
    n = capacity - 1;
    // Back up over continuation bytes so a multi-byte character is not split.
    while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) --n;
  }
  memmove(dst, src, n);
  dst[n] = '\0';
  return fits;
}

uint32_t nextCodepoint(const char *&p) {
  const unsigned char c = (unsigned char)*p;
  if (c == 0) return 0;
  ++p;
  if (c < 0x80) return c;
  int extra = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
  if (extra < 0) return 0xFFFD;
  uint32_t cp = c & (0x3F >> extra);
  for (int i = 0; i < extra; ++i) {
    const unsigned char d = (unsigned char)*p;
    if ((d & 0xC0) != 0x80) return 0xFFFD;  // Truncated sequence; do not consume.
    cp = (cp << 6) | (d & 0x3F);
    ++p;
  }
  return cp;
}

uint32_t hash32(const void *data, size_t size) {
  const uint8_t *b = (const uint8_t *)data;
  uint32_t h = 2166136261u;
  while (size--) h = (h ^ *b++) * 16777619u;
  return h;
}

uint32_t hash32(const char *text) { return hash32(text, text ? strlen(text) : 0); }

}  // namespace stella
