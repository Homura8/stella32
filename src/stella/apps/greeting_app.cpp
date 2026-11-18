#include "greeting_app.h"

#include <string.h>

#include "../core/text.h"
#include "../ui/icons.h"
#include "../ui/layout.h"
#include "../ui/materials.h"
#include "clock_app.h"  // the bedside clock's twinkle timing: the same calm night

namespace stella {
namespace apps {

using namespace layout;

namespace {

// Star positions as 1/256 of the display, kept to the edges so text is never under one.
constexpr uint8_t kStarX[16] = {20, 232, 72, 190, 12, 246, 40, 220, 16, 238, 92, 168, 122, 4, 252, 150};
constexpr uint8_t kStarY[16] = {30, 22, 12, 44, 100, 92, 170, 178, 226, 232, 244, 240, 8, 150, 150, 250};

Point at256(const Rect &area, int fx, int fy) {
  return Point{(int16_t)(area.x + area.w * fx / 256), (int16_t)(area.y + area.h * fy / 256)};
}

}  // namespace

GreetingWords GreetingWords::from(const GreetingContent &content) {
  GreetingWords w;
  memset(&w, 0, sizeof(w));  // so two copies of the same words compare equal byte for byte
  copyText(w.name, sizeof(w.name), content.name);
  copyText(w.headline, sizeof(w.headline), content.headline);
  copyText(w.wish, sizeof(w.wish), content.wish);
  int n = content.pageCount < kMaxPages ? content.pageCount : kMaxPages;
  for (int i = 0; i < n; ++i) copyText(w.pages[i], sizeof(w.pages[i]), content.pages[i]);
  w.pageCount = (uint8_t)(n > 0 ? n : 0);
  return w;
}

bool GreetingWords::same(const GreetingWords &other) const { return memcmp(this, &other, sizeof(*this)) == 0; }

uint32_t GreetingWords::checksum() const {
  uint32_t h = 2166136261u;  // FNV-1a
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(this);
  for (size_t i = 0; i < sizeof(*this); ++i) h = (h ^ bytes[i]) * 16777619u;
  return h;
}

namespace {

struct Head {  // the first saved record: everything but the pages
  char name[GreetingWords::kNameLen];
  char headline[GreetingWords::kHeadlineLen];
  char wish[GreetingWords::kWishLen];
  uint8_t pageCount;
  uint32_t check;
};

void pageKey(int i, char (&key)[4]) {
  key[0] = 'p';
  key[1] = (char)('0' + i);
  key[2] = '\0';
  key[3] = '\0';
}

}  // namespace

bool GreetingApp::loadWords(ServiceHost &host) {
  Head head;
  if (host.loadRecordFor("greeting", "head", kWordsVersion, &head, sizeof(head)) != RecordStatus::Ok) return false;
  if (head.pageCount > GreetingWords::kMaxPages) return false;
  GreetingWords w;
  memset(&w, 0, sizeof(w));
  memcpy(w.name, head.name, sizeof(w.name));
  memcpy(w.headline, head.headline, sizeof(w.headline));
  memcpy(w.wish, head.wish, sizeof(w.wish));
  w.pageCount = head.pageCount;
  for (int i = 0; i < head.pageCount; ++i) {
    char key[4];
    pageKey(i, key);
    if (host.loadRecordFor("greeting", key, kWordsVersion, w.pages[i], sizeof(w.pages[i])) != RecordStatus::Ok) return false;
  }
  if (w.checksum() != head.check) return false;  // a torn or foreign set: as if there were none
  words_ = w;
  return true;
}

void GreetingApp::saveWords(AppContext &ctx) {
  for (int i = 0; i < words_.pageCount; ++i) {
    char key[4];
    pageKey(i, key);
    ctx.saveAppRecord(key, kWordsVersion, words_.pages[i], sizeof(words_.pages[i]));
  }
  Head head;
  memset(&head, 0, sizeof(head));
  memcpy(head.name, words_.name, sizeof(head.name));
  memcpy(head.headline, words_.headline, sizeof(head.headline));
  memcpy(head.wish, words_.wish, sizeof(head.wish));
  head.pageCount = words_.pageCount;
  head.check = words_.checksum();
  ctx.saveAppRecord("head", kWordsVersion, &head, sizeof(head));  // last: it vouches for the pages
}

GreetingApp::GreetingApp() : screen_(*this) {
  memset(&words_, 0, sizeof(words_));
  copyText(words_.name, sizeof(words_.name), "Hello");
  copyText(words_.headline, sizeof(words_.headline), "No message yet");  // only ever seen if opened by hand with no words
  shown_ = AppInfo{"greeting", words_.name, icons::sparkle, 1, 0};
  hidden_ = AppInfo{"greeting", words_.name, icons::sparkle, 1, kAppHidden};
}

void GreetingApp::onInstall(ServiceHost &host) {
  uint8_t pending = 1;
  // A device that has never shown it (no record) shows it.
  pending_ = host.loadRecordFor("greeting", "pending", kStateVersion, &pending, sizeof(pending)) != RecordStatus::Ok || pending != 0;
  hasWords_ = loadWords(host);
  if (offered_) {
    // A build made for one person: its words are the device's words from now on, and new words are new to read.
    const GreetingWords wanted = GreetingWords::from(*offered_);
    if (!hasWords_ || !wanted.same(words_)) {
      words_ = wanted;
      hasWords_ = true;
      saveWords_ = true;
      if (!pending_) {
        pending_ = true;
        savePending_ = true;
      }
    }
  }
}

uint32_t GreetingApp::onWake(AppContext &ctx) {
  const uint8_t pending = pending_ ? 1 : 0;
  if (saveWords_) saveWords(ctx);
  if (savePending_) ctx.saveAppRecord("pending", kStateVersion, &pending, sizeof(pending));
  saveWords_ = savePending_ = false;
  return kNoTick;
}

void GreetingApp::setPending(bool pending, AppContext &ctx) {
  if (!hasWords_ || pending == pending_) return;  // a dormant app writes nothing; nothing changed, nothing to write
  pending_ = pending;
  const uint8_t v = pending ? 1 : 0;
  ctx.saveAppRecord("pending", kStateVersion, &v, sizeof(v));
}

int GreetingApp::linesFor(const Font &font, const char *text, int width) {
  static Color scratch[4];
  Canvas c(scratch, Rect(0, 0, 2, 2));  // drawing is clipped to it; only the count matters
  return c.drawWrapped(font, text, Rect(0, 0, width, font.lineHeight * 64), 0);
}

// ------------------------------------------------------------------ screen

int GreetingApp::GreetingScreen::starLevel(int star, int frame) { return ((star + frame) & 3) == 0 ? 1 : 0; }

Rect GreetingApp::GreetingScreen::starBox(int star) const {
  return Rect(stars_[star].x - 1, stars_[star].y - 1, starSize_ + 2, starSize_ + 2);
}

void GreetingApp::GreetingScreen::layout(const Rect &area, const Theme &t) {
  area_ = area;
  const GreetingWords &words = app_.words_;
  const Rect inner = area.inset(t.m.margin, t.m.margin);
  fits_ = true;

  // The first page: sparkles, the big line, the soft line, Continue.
  const int headLines = linesFor(*t.font.headline, words.headline, inner.w);
  const int wishLines = linesFor(*t.font.title, words.wish, inner.w);
  Rect r[4];
  column(inner, {flex(), px(headLines * t.font.headline->lineHeight), px(wishLines * t.font.title->lineHeight), px(t.m.control)},
         t.m.gapM, r);
  art_ = r[0];
  headline_ = r[1];
  wish_ = r[2];
  continue_.bounds = center(r[3], t.dp(160) < inner.w ? t.dp(160) : inner.w, t.m.control);
  continue_.label = words.pageCount > 0 ? "Continue" : "Done";
  sparkle_ = t.dp(34) < art_.h ? t.dp(34) : art_.h;
  if (art_.h < t.dp(20)) fits_ = false;
  // The constellation's sparkles: where each sits (the three that twinkle get a box of their own to repaint).
  const int sx = art_.w / 5 < sparkle_ * 2 ? art_.w / 5 : sparkle_ * 2;
  const int small = sparkle_ / 2;
  mid_ = Point{(int16_t)art_.centerX(), (int16_t)art_.centerY()};
  left_ = Point{(int16_t)(mid_.x - sx), (int16_t)(mid_.y + sparkle_ / 6)};
  right_ = Point{(int16_t)(mid_.x + sx), (int16_t)(mid_.y - sparkle_ / 5)};
  farLeft_ = Point{(int16_t)(mid_.x - 2 * sx), (int16_t)(mid_.y - sparkle_ / 4)};
  farRight_ = Point{(int16_t)(mid_.x + 2 * sx), (int16_t)(mid_.y + sparkle_ / 3)};
  twinkle_[0] = Rect(mid_.x - sparkle_ / 2 - 1, mid_.y - sparkle_ / 2 - 1, sparkle_ + 2, sparkle_ + 2);
  twinkle_[1] = Rect(left_.x - small / 2 - 1, left_.y - small / 2 - 1, small + 2, small + 2);
  twinkle_[2] = Rect(right_.x - small / 2 - 1, right_.y - small / 2 - 1, small + 2, small + 2);

  // The pages: the words, their dots, Back and Next.
  Rect q[3];
  column(inner, {flex(), px(t.dp(10)), px(t.m.control)}, t.m.gapM, q);
  dots_ = q[1];
  Rect b[2];
  row(q[2], {flex(), flex()}, t.m.gapM, b);
  back_.bounds = b[0];
  back_.label = "Back";
  next_.bounds = b[1];
  next_.label = stage_ >= words.pageCount ? "Done" : "Next";
  for (int i = 0; i < words.pageCount; ++i) {
    const int h = linesFor(*t.font.body, words.pages[i], q[0].w) * t.font.body->lineHeight;
    if (h > q[0].h) fits_ = false;
    const int used = h < q[0].h ? h : q[0].h;
    text_[i] = Rect(q[0].x, q[0].y + (q[0].h - used) / 2, q[0].w, used);
  }

  starSize_ = t.density == Density::Regular ? 2 : 1;
  for (int i = 0; i < kStars; ++i) stars_[i] = at256(area, kStarX[i], kStarY[i]);
}

void GreetingApp::GreetingScreen::drawPill(Canvas &c, const Theme &t, const Button &b, bool primary) const {
  const Palette &p = t.color;
  const int radius = t.m.radiusPill;
  if (primary) {
    c.fillRoundRect(b.bounds, radius, blend(p.bedBg, p.bedClock, b.pressed ? 150 : 90));
    c.strokeRoundRect(b.bounds, radius, 1, p.bedClock);
  } else {
    if (b.pressed) c.fillRoundRect(b.bounds, radius, blend(p.bedBg, p.bedMuted, 70));
    c.strokeRoundRect(b.bounds, radius, 1, p.bedMuted);
  }
  c.drawTextIn(*t.font.body, b.label, b.bounds, primary ? p.bedText : p.bedMuted, Align::Center);
}

void GreetingApp::GreetingScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  const GreetingWords &words = app_.words_;
  c.fill(p.bedBg);  // black, like the bedside clock: low backlight, nothing to dither
  const Color faint = blend(p.bedBg, p.bedStar, 150);
  for (int i = 0; i < kStars; ++i)
    c.fillRect(Rect(stars_[i].x, stars_[i].y, starSize_, starSize_), starLevel(i, frame_) ? p.bedStar : faint);

  if (stage_ == 0) {
    // A little constellation of sparkles over the words; the big one breathes.
    const bool twinkle = (frame_ & 2) != 0;
    const int big = twinkle ? sparkle_ : sparkle_ * 3 / 4;
    const int small = sparkle_ / 2, tiny = sparkle_ / 3;
    icons::sparkle(c, Rect(mid_.x - big / 2, mid_.y - big / 2, big, big), twinkle ? p.bedClock : p.bedText);
    const int s2 = twinkle ? small * 3 / 4 : small;
    icons::sparkle(c, Rect(left_.x - s2 / 2, left_.y - s2 / 2, s2, s2), p.bedStar);
    icons::sparkle(c, Rect(right_.x - small / 2, right_.y - small / 2, small, small), twinkle ? p.bedStar : p.bedText);
    icons::sparkle(c, Rect(farRight_.x - tiny / 2, farRight_.y - tiny / 2, tiny, tiny), p.bedMuted);
    icons::sparkle(c, Rect(farLeft_.x - tiny / 2, farLeft_.y - tiny / 2, tiny, tiny), p.bedMuted);
    c.drawWrapped(*t.font.headline, words.headline, headline_, p.bedClock, 0, Align::Center);
    c.drawWrapped(*t.font.title, words.wish, wish_, p.bedText, 0, Align::Center);
    drawPill(c, t, continue_, true);
    return;
  }
  const int page = stage_ - 1;
  c.drawWrapped(*t.font.body, words.pages[page], text_[page], p.bedText, 0, Align::Center);
  // Where you are: one dot per page.
  const int dot = t.dp(6), gap = t.dp(8), n = words.pageCount;
  int x = dots_.centerX() - (n * dot + (n - 1) * gap) / 2;
  for (int i = 0; i < n; ++i, x += dot + gap) c.fillCircle(x + dot / 2, dots_.centerY(), dot / 2, i == page ? p.bedClock : p.bedStar, i == page ? 255 : 160);
  drawPill(c, t, back_, false);
  drawPill(c, t, next_, true);
}

void GreetingApp::GreetingScreen::go(int stage, AppContext &ctx) {
  stage_ = stage;
  next_.label = stage_ >= app_.words_.pageCount ? "Done" : "Next";
  ctx.invalidate(area_);
}

bool GreetingApp::GreetingScreen::onEvent(const Event &e, AppContext &ctx) {
  const int pages = app_.words_.pageCount;
  bool finish = false;
  if (stage_ == 0) {
    if (continue_.handle(e, ctx)) {
      if (pages > 0) go(1, ctx);
      else finish = true;
    }
  } else {
    if (back_.handle(e, ctx)) go(stage_ - 1, ctx);
    else if (next_.handle(e, ctx)) {
      if (stage_ >= pages) finish = true;
      else go(stage_ + 1, ctx);
    }
  }
  if (finish) {
    app_.setPending(false, ctx);  // seen to the end: it will not come up again by itself
    ctx.goHome();
  }
  return e.isPointer();
}

void GreetingApp::GreetingScreen::animate(AppContext &ctx, int frame) {
  if (frame == frame_) return;
  const int old = frame_;
  for (int i = 0; i < kStars; ++i)
    if (starLevel(i, old) != starLevel(i, frame)) ctx.invalidate(starBox(i));
  if (stage_ == 0 && ((old ^ frame) & 2))
    for (const Rect &box : twinkle_) ctx.invalidate(box);
  frame_ = frame;
}

void GreetingApp::GreetingScreen::onShow(AppContext &ctx) {
  stage_ = 0;  // always from the first page
  next_.label = app_.words_.pageCount > 0 ? "Next" : "Done";
  frame_ = bedsideFrameAt(ctx.millis());
  app_.setPending(true, ctx);  // opened, so it comes back until it has been read to the end
}

uint32_t GreetingApp::GreetingScreen::onTick(AppContext &ctx) {
  const uint32_t ms = ctx.millis();
  animate(ctx, bedsideFrameAt(ms));
  return bedsideMsToNextFrame(ms);
}

}  // namespace apps
}  // namespace stella
