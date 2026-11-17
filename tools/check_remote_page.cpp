// Checks a wallpaper file with the device's own loader (net/wallpaper.cpp) and prints a few pixels.
//   node tools/check_remote_page.js out.stwp
//   g++ -std=c++17 -I src tools/check_remote_page.cpp src/stella/net/wallpaper.cpp -o /tmp/check && /tmp/check out.stwp
#include <stdio.h>
#include <string.h>
#include <vector>
#include "stella/net/wallpaper.h"
using namespace stella;
struct Mem : WallpaperStore {
  std::vector<uint8_t> b;
  size_t capacity() override { return b.size(); }
  bool write(size_t, const uint8_t *, size_t) override { return false; }
  bool read(size_t o, uint8_t *out, size_t n) override { if (o + n > b.size()) return false; memcpy(out, b.data() + o, n); return true; }
  bool erase() override { return true; }
};
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb"); Mem m; m.b.resize(131072, 0xFF);
  size_t n = fread(m.b.data(), 1, m.b.size(), f); fclose(f);
  printf("file bytes: %zu (expected %zu)\n", n, wallpaper::kTotalBytes);
  uint16_t pal[256];
  const bool ok = wallpaper::load(m, pal);
  printf("device loader accepts it: %s\n", ok ? "yes" : "NO");
  if (!ok) return 1;
  // Compare against the source gradient: sample a few pixels (the source is 640x480 cover-cropped).
  auto px = [&](int x, int y) { return pal[m.b[wallpaper::kHeaderBytes + wallpaper::kPaletteBytes + y * 320 + x]]; };
  auto rgb = [](uint16_t c, int &r, int &g, int &bl) { r = (c >> 11) << 3; g = ((c >> 5) & 63) << 2; bl = (c & 31) << 3; };
  int r, g, bl;
  const int pts[][2] = {{5, 5}, {160, 120}, {314, 234}, {100, 80}};
  for (auto &p : pts) { rgb(px(p[0], p[1]), r, g, bl); printf("(%3d,%3d) -> %3d %3d %3d\n", p[0], p[1], r, g, bl); }
  return 0;
}
