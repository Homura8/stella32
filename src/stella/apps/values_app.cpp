#include "values_app.h"

#include <stdio.h>
#include <string.h>

#include "../core/deadline.h"
#include "../core/text.h"
#include "../ui/layout.h"
#include "../ui/materials.h"

namespace stella {
namespace apps {

using namespace layout;

const AppInfo &ValuesApp::info() const {
  static const AppInfo kInfo{"values", "Stats", icons::bars, 0, kAppOptional};
  return kInfo;
}

void ValuesApp::onTopic(uint16_t topicId, AppContext &ctx) {
  if (topicId == topics::kValues && screen_.isVisible()) screen_.refresh(ctx);
}

void ValuesApp::ValuesScreen::layout(const Rect &content, const Theme &t) {
  content_ = content.inset(t.m.margin, t.m.gapS);
  theme_ = t;
  arrange();
}

// Tiles for the values there are: one big, two columns up to four, three up to six, else four.
void ValuesApp::ValuesScreen::arrange() {
  const Theme &t = theme_;
  const int n = values_.count;
  for (Rect &r : tiles_) r = Rect();
  message_ = content_;
  if (n == 0) return;
  const int cols = n == 1 ? 1 : n <= 4 ? 2 : n <= 6 ? 3 : 4;
  const int rows = (n + cols - 1) / cols;
  for (int i = 0; i < n; ++i) {
    // the last row is centred when it is not full
    const int inRow = i / cols == rows - 1 ? n - (rows - 1) * cols : cols;
    Rect row = grid(content_, 1, rows, i / cols, t.m.gapM);
    const int pad = (cols - inRow) * (row.w - (cols - 1) * t.m.gapM) / cols / 2 + (cols - inRow) * t.m.gapM / 2;
    row = row.inset(pad, 0);
    tiles_[i] = grid(row, inRow, 1, i % cols, t.m.gapM);
  }
}

void ValuesApp::ValuesScreen::refresh(AppContext &ctx) {
  const PushedValues next = ctx.pushedValues();
  const bool countChanged = next.count != values_.count;
  values_ = next;
  const uint32_t now = ctx.millis();
  for (int i = 0; i < values_.count; ++i) {
    const uint32_t age = elapsedMs(now, values_.items[i].updatedMs);
    stale_[i] = age >= ValuesApp::kStaleMs;
    const uint32_t minutes = age / 60000u;
    if (!stale_[i]) ageText_[i][0] = '\0';
    else if (minutes < 60) snprintf(ageText_[i], sizeof(ageText_[i]), "%lu min ago", (unsigned long)minutes);
    else snprintf(ageText_[i], sizeof(ageText_[i]), "%lu h ago", (unsigned long)(minutes / 60));
  }
  if (countChanged || tiles_[0].empty() != (values_.count == 0)) arrange();
  const RemoteInfo remote = ctx.remote().status();
  snprintf(hint_, sizeof(hint_), "Numbers from your computer show here. Send name=value pairs to http://%s.local/api/push (PIN: Settings, Web page).",
           remote.host[0] ? remote.host : "stella-xxxx");
  if (isVisible()) ctx.invalidate(ctx.contentRect());
}

void ValuesApp::ValuesScreen::onShow(AppContext &ctx) { refresh(ctx); }

// Dimming and "12 min ago" move on once a minute.
uint32_t ValuesApp::ValuesScreen::onTick(AppContext &ctx) {
  bool changed = false;
  const uint32_t now = ctx.millis();
  for (int i = 0; i < values_.count; ++i) {
    const bool stale = elapsedMs(now, values_.items[i].updatedMs) >= ValuesApp::kStaleMs;
    changed |= stale != stale_[i] || (stale && elapsedMs(now, values_.items[i].updatedMs) / 60000u !=
                                                   (elapsedMs(now, values_.items[i].updatedMs) - 1000u) / 60000u);
  }
  if (changed) refresh(ctx);
  return 30000;
}

void ValuesApp::ValuesScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  if (values_.count == 0) {
    const Rect box = message_.inset(t.m.padding, 0);
    Rect parts[4];
    column(box, {flex(), px(t.font.headline->lineHeight), px(4 * t.font.body->lineHeight), flex()}, t.m.gapM, parts);
    c.drawTextIn(*t.font.headline, "Nothing to show yet", parts[1], p.text, Align::Center);
    c.drawWrapped(*t.font.body, hint_, parts[2], p.textMuted, 4, Align::Center);
    return;
  }
  for (int i = 0; i < values_.count; ++i) {
    const Rect tile = tiles_[i];
    paintGlass(c, t, tile, t.m.radiusCard);
    const PushedValue &v = values_.items[i];
    const Color ink = stale_[i] ? p.textMuted : p.text;
    const Rect inner = tile.inset(t.dp(6), t.dp(6));
    Rect parts[3];
    column(inner, {px(t.font.caption->lineHeight), flex(), px(t.font.caption->lineHeight)}, 0, parts);
    c.drawTextIn(*t.font.caption, v.key, parts[0], p.textMuted, Align::Center);
    // the biggest font the value fits in
    const Font *font = t.font.headline;
    if (Canvas::textWidth(*font, v.value) > inner.w) font = t.font.title;
    if (Canvas::textWidth(*font, v.value) > inner.w) font = t.font.body;
    c.drawTextIn(*font, v.value, parts[1], ink, Align::Center);
    if (stale_[i]) c.drawTextIn(*t.font.caption, ageText_[i], parts[2], p.textMuted, Align::Center);
  }
}

bool ValuesApp::ValuesScreen::onEvent(const Event &e, AppContext &) { return e.isPointer(); }

}  // namespace apps
}  // namespace stella
