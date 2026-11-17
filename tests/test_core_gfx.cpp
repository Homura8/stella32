// Core utilities, canvas primitives, fonts, dirty regions and the renderer.
#include <string.h>

#include <vector>

#include "host_board.h"
#include "stella/core/deadline.h"
#include "stella/core/text.h"
#include "stella/fonts/fonts.h"
#include "stella/gfx/renderer.h"
#include "stella/ui/layout.h"
#include "test.h"

using namespace stella;

TEST(geometry_intersect_unite) {
  const Rect a(0, 0, 10, 10), b(5, 5, 10, 10);
  CHECK(a.intersect(b) == Rect(5, 5, 5, 5));
  CHECK(a.unite(b) == Rect(0, 0, 15, 15));
  CHECK(a.intersect(Rect(20, 20, 5, 5)).empty());
  CHECK(Rect().unite(b) == b);
  CHECK(Rect(0, 0, -5, 3).empty());  // negative sizes clamp to empty
}

TEST(deadline_wraps) {
  CHECK(reached(5, 0xFFFFFFF0u));            // 21 ms after wrap
  CHECK(!reached(0xFFFFFFF0u, 5));
  CHECK_EQ(elapsedMs(5, 0xFFFFFFF0u), 21u);
  CHECK_EQ(elapsedMs(100, 200), 0u);         // sampled on another core: not 49 days
  CHECK_EQ(remainingMs(0xFFFFFFF0u, 10), 26u);
}

TEST(copy_text_respects_utf8) {
  char out[6];
  CHECK(!copyText(out, sizeof(out), "abcd\xc3\xa9z"));  // é would straddle the limit
  CHECK(strcmp(out, "abcd") == 0);
  CHECK(copyText(out, sizeof(out), "ok"));
  const char *p = "\xe2\x80\xa6x";
  CHECK_EQ(nextCodepoint(p), 0x2026u);
  CHECK_EQ(nextCodepoint(p), (uint32_t)'x');
  const char *bad = "\xe2\x80";
  CHECK_EQ(nextCodepoint(bad), 0xFFFDu);
}

TEST(font_lookup_and_metrics) {
  for (const fonts::FontSet *set : {&fonts::kCompact, &fonts::kRegular}) {
    for (const Font *f : {set->caption, set->body, set->title, set->headline}) {
      CHECK_EQ(f->codepoints[f->find('A')], (uint16_t)'A');
      CHECK_EQ(f->find(0x1F600), f->fallback);  // emoji -> '?'
      CHECK(f->find(0x2026) != f->fallback);     // ellipsis is included
      CHECK(f->lineHeight >= f->ascent + f->descent);
    }
    CHECK(set->display->find('7') != set->display->fallback);
    CHECK(Canvas::textWidth(*set->body, "Stella") > 0);
  }
}

namespace {

struct Surface {
  explicit Surface(int w, int h) : px((size_t)w * h, 0), c(px.data(), Rect(0, 0, w, h)) {}
  std::vector<Color> px;
  Canvas c;
};

}  // namespace

TEST(canvas_clips_everything) {
  Surface s(16, 16);
  s.c.fill(rgb(255, 255, 255));
  const Rect old = s.c.pushClip(Rect(4, 4, 4, 4));
  s.c.fillRect(Rect(-100, -100, 400, 400), 0);
  s.c.fillCircle(5, 5, 50, 0);
  s.c.drawText(*fonts::kCompact.body, "clip", -10, 30, 0);
  s.c.restoreClip(old);
  int black = 0;
  for (Color p : s.px) black += p == 0;
  CHECK_EQ(black, 16);  // only the 4x4 clip changed
}

TEST(canvas_antialiasing_is_symmetric) {
  Surface s(21, 21);
  s.c.fill(0);
  s.c.fillCircle(10, 10, 7, rgb(255, 255, 255));
  for (int y = 0; y < 21; ++y)
    for (int x = 0; x < 21; ++x) {
      CHECK_EQ(s.px[y * 21 + x], s.px[y * 21 + (20 - x)]);
      CHECK_EQ(s.px[y * 21 + x], s.px[x * 21 + y]);
    }
  CHECK_EQ(s.px[10 * 21 + 10], rgb(255, 255, 255));
  const Color edge = s.px[10 * 21 + 17];
  CHECK(edge != 0 && edge != rgb(255, 255, 255));  // partially covered edge pixel
}

TEST(canvas_translucent_fill_blends) {
  Surface s(4, 4);
  s.c.fill(rgb(0, 0, 0));
  s.c.fillRect(Rect(0, 0, 4, 4), rgb(255, 255, 255), 128);
  CHECK(red8(s.px[0]) > 110 && red8(s.px[0]) < 145);
}

TEST(text_is_shortened_with_ellipsis) {
  Surface wide(200, 30), narrow(200, 30);
  const Font &f = *fonts::kCompact.body;
  wide.c.fill(0);
  narrow.c.fill(0);
  narrow.c.drawTextIn(f, "A rather long settings label", Rect(0, 0, 60, 30), rgb(255, 255, 255));
  int lastInk = -1;
  for (int x = 0; x < 200; ++x)
    for (int y = 0; y < 30; ++y)
      if (narrow.px[y * 200 + x]) lastInk = x;
  CHECK(lastInk >= 40 && lastInk < 62);  // stays inside the box (+ AA bleed)
}

TEST(wrapped_text_respects_line_limit) {
  Surface s(120, 100);
  s.c.fill(0);
  const int lines = s.c.drawWrapped(*fonts::kCompact.body, "one two three four five six seven eight nine ten",
                                    Rect(0, 0, 80, 100), rgb(255, 255, 255), 2);
  CHECK_EQ(lines, 2);
}

TEST(dirty_region_merges_and_bounds) {
  DirtyRegion r;
  r.setBounds(Rect(0, 0, 320, 240));
  r.add(Rect(10, 10, 20, 20));
  r.add(Rect(12, 12, 5, 5));  // contained
  CHECK_EQ(r.count(), 1);
  r.add(Rect(25, 10, 20, 20));  // overlapping -> merged
  CHECK_EQ(r.count(), 1);
  r.add(Rect(300, 200, 50, 50));  // clipped to the screen
  CHECK(r.bounds().right() <= 320 && r.bounds().bottom() <= 240);
  for (int i = 0; i < 40; ++i) r.add(Rect((i * 37) % 300, (i * 53) % 220, 6, 6));
  CHECK(r.count() <= DirtyRegion::kMaxRects);
  r.addAll();
  CHECK_EQ(r.count(), 1);
  CHECK(r[0] == Rect(0, 0, 320, 240));
}

namespace {

// A deterministic scene with translucency, text and AA shapes.
struct Scene {
  int variant = 0;
  static void draw(Canvas &c, void *self) {
    const int v = static_cast<Scene *>(self)->variant;
    for (int y = c.clip().y; y < c.clip().bottom(); ++y)
      for (int x = c.clip().x; x < c.clip().right(); ++x) c.at(x, y) = rgb((uint8_t)x, (uint8_t)y, 90);
    c.fillRoundRect(Rect(20 + v, 30, 120, 70), 14, rgb(255, 250, 255), 180);
    c.strokeRoundRect(Rect(20 + v, 30, 120, 70), 14, 1, rgb(255, 255, 255), 230);
    c.fillCircle(200, 120 + v, 30, rgb(200, 180, 230));
    c.drawText(*fonts::kCompact.title, "Stella", 40, 70 + v, rgb(60, 50, 80));
    c.line(10, 200, 300, 150 + v, 3, rgb(120, 90, 140));
  }
};

}  // namespace

TEST(incremental_render_equals_full_render) {
  host::SimDisplay incremental(320, 240), full(320, 240);
  std::vector<Color> buffer(320 * 20);
  Renderer renderer(buffer.data(), buffer.size());
  Scene scene;
  DirtyRegion region;
  region.setBounds(Rect(0, 0, 320, 240));
  region.addAll();
  renderer.render(incremental, region, Scene::draw, &scene);
  const int dirty[][4] = {{18, 28, 130, 80}, {165, 85, 70, 75}, {35, 45, 70, 35}, {5, 140, 310, 70}};
  for (int step = 1; step <= 4; ++step) {
    scene.variant = step;
    for (int i = 0; i < 4; ++i) region.add(Rect(dirty[i][0], dirty[i][1], dirty[i][2] + step, dirty[i][3] + step));
    incremental.resetCounters();
    renderer.render(incremental, region, Scene::draw, &scene);
    CHECK(incremental.pixelsWritten() < 320u * 240u);  // only dirty areas were sent
  }
  region.addAll();
  renderer.render(full, region, Scene::draw, &scene);
  CHECK(incremental.framebuffer() == full.framebuffer());
}

TEST(renderer_chunks_follow_buffer_size) {
  host::SimDisplay d(320, 240);
  std::vector<Color> buffer(320 * 10);
  Renderer renderer(buffer.data(), buffer.size());
  Scene scene;
  DirtyRegion region;
  region.setBounds(Rect(0, 0, 320, 240));
  region.add(Rect(100, 100, 32, 50));  // narrow: fits in one chunk
  CHECK_EQ(renderer.render(d, region, Scene::draw, &scene).chunks, 1u);
  region.addAll();
  CHECK_EQ(renderer.render(d, region, Scene::draw, &scene).chunks, 24u);
}

TEST(layout_split_fixed_and_flex) {
  using namespace layout;
  Rect r[4];
  column(Rect(0, 0, 100, 200), {px(20), flex(), px(30), flex(2)}, 10, r);
  CHECK(r[0] == Rect(0, 0, 100, 20));
  CHECK_EQ(r[1].h, 40);  // (200 - 20 - 30 - 3 gaps) shared 1:2
  CHECK_EQ(r[3].h, 80);
  CHECK_EQ(r[3].bottom(), 200);
  // Fixed lengths larger than the area shrink instead of overflowing.
  Rect s[2];
  row(Rect(0, 0, 50, 10), {px(40), px(40)}, 4, s);
  CHECK(s[1].right() <= 50);
  CHECK(center(Rect(0, 0, 10, 10), 20, 4) == Rect(0, 3, 10, 4));
}
