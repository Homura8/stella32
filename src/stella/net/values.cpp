#include "values.h"

#include <string.h>

#include "../core/deadline.h"
#include "../core/text.h"

namespace stella {

namespace {

bool keyChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-' || c == '.';
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// The text of a value: %XX decoded where two hex digits follow, control characters
// dropped, cut at a UTF-8 boundary. Returns false for invalid UTF-8.
bool cleanValue(const char *s, size_t n, char *out, size_t capacity) {
  char tmp[64];
  size_t used = 0;
  for (size_t i = 0; i < n && used + 1 < sizeof(tmp); ++i) {
    unsigned char c = (unsigned char)s[i];
    if (c == '%' && i + 2 < n && hexValue(s[i + 1]) >= 0 && hexValue(s[i + 2]) >= 0) {
      c = (unsigned char)(hexValue(s[i + 1]) * 16 + hexValue(s[i + 2]));
      i += 2;
    }
    if (c < 0x20 || c == 0x7F) continue;  // controls (also a decoded %0A)
    tmp[used++] = (char)c;
  }
  tmp[used] = '\0';
  // Valid UTF-8 only: a stray byte would draw as nothing or as a wrong glyph.
  for (const unsigned char *p = (const unsigned char *)tmp; *p;) {
    int extra = *p < 0x80 ? 0 : (*p >> 5) == 6 ? 1 : (*p >> 4) == 14 ? 2 : (*p >> 3) == 30 ? 3 : -1;
    if (extra < 0) return false;
    ++p;
    for (int k = 0; k < extra; ++k, ++p)
      if ((*p & 0xC0) != 0x80) return false;
  }
  copyText(out, capacity, tmp);
  return true;
}

int findKey(const PushedValues &state, const char *key) {
  for (int i = 0; i < state.count; ++i)
    if (strcmp(state.items[i].key, key) == 0) return i;
  return -1;
}

}  // namespace

PushResult applyPush(PushedValues &state, const char *body, size_t length, uint32_t nowMs) {
  PushResult result;
  size_t start = 0;
  while (start <= length) {
    size_t end = start;
    while (end < length && body[end] != '&' && body[end] != '\n' && body[end] != '\r') ++end;
    if (end > start) {
      // one "key=value"
      const char *pair = body + start;
      const size_t n = end - start;
      const char *eq = (const char *)memchr(pair, '=', n);
      if (!eq) {
        ++result.rejected;
      } else {
        size_t k0 = 0, k1 = (size_t)(eq - pair);
        while (k0 < k1 && pair[k0] == ' ') ++k0;
        while (k1 > k0 && pair[k1 - 1] == ' ') --k1;
        char key[kPushKeyBytes];
        bool ok = k1 > k0 && k1 - k0 < kPushKeyBytes;
        for (size_t i = k0; ok && i < k1; ++i) ok = keyChar(pair[i]);
        char value[kPushValueBytes];
        if (ok) {
          memcpy(key, pair + k0, k1 - k0);
          key[k1 - k0] = '\0';
          const char *v = eq + 1;
          size_t vn = n - (size_t)(eq + 1 - pair);
          while (vn > 0 && *v == ' ') ++v, --vn;
          while (vn > 0 && v[vn - 1] == ' ') --vn;
          ok = cleanValue(v, vn, value, sizeof(value));
        }
        if (!ok) {
          ++result.rejected;
        } else {
          const int at = findKey(state, key);
          if (value[0] == '\0') {  // an empty value removes the key
            if (at >= 0) {
              for (int i = at + 1; i < state.count; ++i) state.items[i - 1] = state.items[i];
              state.items[--state.count] = PushedValue{};
            }
            ++result.accepted;
          } else {
            int slot = at;
            if (slot < 0) {
              if (state.count < kMaxPushedValues) {
                slot = state.count++;
              } else {  // full: the one that has been quiet longest makes room, order kept
                int oldest = 0;
                for (int i = 1; i < state.count; ++i)
                  if (elapsedMs(nowMs, state.items[i].updatedMs) > elapsedMs(nowMs, state.items[oldest].updatedMs)) oldest = i;
                for (int i = oldest + 1; i < state.count; ++i) state.items[i - 1] = state.items[i];
                slot = state.count - 1;
              }
              copyText(state.items[slot].key, sizeof(state.items[slot].key), key);
            }
            copyText(state.items[slot].value, sizeof(state.items[slot].value), value);
            state.items[slot].updatedMs = nowMs;
            ++result.accepted;
          }
        }
      }
    }
    start = end + 1;
  }
  if (result.accepted > 0) ++state.generation;
  return result;
}

}  // namespace stella
