#include "renderer.h"

#include <string.h>

namespace stella {
namespace {

// Merging costs this many extra pixels at most; above it, keep rects apart.
constexpr int kMergeSlack = 2048;

}  // namespace

Rect DirtyRegion::bounds() const {
  Rect b;
  for (int i = 0; i < count_; ++i) b = b.unite(rects_[i]);
  return b;
}

void DirtyRegion::add(const Rect &r) {
  const Rect clipped = r.intersect(bounds_);
  if (clipped.empty()) return;
  insertMerged(clipped);
}

void DirtyRegion::insertMerged(Rect r) {
  // Absorb every existing rectangle that is contained, overlapping, or close
  // enough that one bigger rectangle is cheaper than two passes.
  bool merged = true;
  while (merged) {
    merged = false;
    for (int i = 0; i < count_; ++i) {
      const Rect u = r.unite(rects_[i]);
      if (rects_[i].contains(r)) return;  // Already covered.
      if (r.intersects(rects_[i]) || u.area() <= r.area() + rects_[i].area() + kMergeSlack) {
        r = u;
        rects_[i] = rects_[--count_];
        merged = true;
        break;
      }
    }
  }
  if (count_ < kMaxRects) {
    rects_[count_++] = r;
    return;
  }
  // Full: merge the new rect with whichever existing one grows the least.
  int best = 0;
  int bestGrowth = 0x7fffffff;
  for (int i = 0; i < count_; ++i) {
    const int growth = r.unite(rects_[i]).area() - rects_[i].area();
    if (growth < bestGrowth) {
      bestGrowth = growth;
      best = i;
    }
  }
  const Rect u = r.unite(rects_[best]);
  rects_[best] = rects_[--count_];
  insertMerged(u);
}

Renderer::Stats Renderer::render(Display &display, DirtyRegion &region, DrawFn draw, void *context) {
  Stats stats;
  Canvas canvas;
  for (int i = 0; i < region.count(); ++i) {
    const Rect r = region[i];
    const int rows = r.w > 0 ? (int)(capacity_ / (size_t)r.w) : 0;
    if (rows <= 0) continue;  // Wider than the buffer: misconfigured board.
    for (int y = r.y; y < r.bottom(); y += rows) {
      const Rect chunk(r.x, y, r.w, (r.bottom() - y) < rows ? (r.bottom() - y) : rows);
      // Deterministic start state; draw code must still paint every pixel.
      memset(buffer_, 0, (size_t)chunk.area() * sizeof(Color));
      canvas.begin(buffer_, chunk);
      draw(canvas, context);
      display.writePixels(chunk, buffer_);
      ++stats.chunks;
      stats.pixels += (uint32_t)chunk.area();
    }
  }
  region.clear();
  return stats;
}

}  // namespace stella
