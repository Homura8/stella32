#include "host_board.h"

#include <stdio.h>
#include <string.h>

namespace stella {
namespace host {

void SimDisplay::writePixels(const Rect &area, const Color *pixels) {
  for (int y = 0; y < area.h; ++y) {
    for (int x = 0; x < area.w; ++x) {
      const int sx = area.x + x, sy = area.y + y;
      if (sx >= 0 && sx < w_ && sy >= 0 && sy < h_) pixels_[(size_t)sy * w_ + sx] = pixels[y * area.w + x];
    }
  }
  written_ += (uint64_t)area.area();
  ++writes_;
}

bool SimDisplay::writePpm(const std::string &path) const {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", w_, h_);
  for (Color c : pixels_) {
    const unsigned char rgb[3] = {red8(c), green8(c), blue8(c)};
    fwrite(rgb, 1, 3, f);
  }
  return fclose(f) == 0;
}

static std::string keyOf(const char *ns, const char *key) { return std::string(ns) + "/" + key; }

int MemoryStore::read(const char *ns, const char *key, void *out, size_t capacity) {
  auto it = values_.find(keyOf(ns, key));
  if (it == values_.end()) return -1;
  if (it->second.size() <= capacity) memcpy(out, it->second.data(), it->second.size());
  return (int)it->second.size();
}

bool MemoryStore::write(const char *ns, const char *key, const void *data, size_t size) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  values_[keyOf(ns, key)].assign(p, p + size);
  ++writes_;
  return true;
}

bool MemoryStore::erase(const char *ns, const char *key) { return values_.erase(keyOf(ns, key)) > 0; }

std::vector<uint8_t> *MemoryStore::raw(const char *ns, const char *key) {
  auto it = values_.find(keyOf(ns, key));
  return it == values_.end() ? nullptr : &it->second;
}

}  // namespace host
}  // namespace stella
