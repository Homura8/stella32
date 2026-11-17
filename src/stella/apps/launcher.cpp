#include "launcher.h"

#include <stdio.h>
#include <string.h>

#include "../ui/layout.h"
#include "../ui/materials.h"
#include "common.h"

namespace stella {
namespace apps {

using namespace layout;

const AppInfo &LauncherApp::info() const {
  static const AppInfo kInfo{"home", "Home", icons::home, 1, kAppHome | kAppHidden};
  return kInfo;
}

void LauncherApp::onTopic(uint16_t topicId, AppContext &ctx) {
  if (topicId == topics::kNetwork) home_.refreshText(ctx);
}

int LauncherApp::HomeScreen::pageCount() const {
  const int n = (int)apps_.size();
  return n == 0 ? 1 : (n + perPage() - 1) / perPage();
}

void LauncherApp::HomeScreen::layout(const Rect &content, const Theme &t) {
  theme_ = t;
  area_ = content.inset(t.m.margin, 0);
  arrange();
}

// The header (the time, large, with the greeting and the date beside it), the
// grid of apps, the page dots and, when Wi-Fi is not set up, a note: laid out
// with the *same* space above, between and below, so nothing looks squeezed
// against one edge.
void LauncherApp::HomeScreen::arrange() {
  const Theme &t = theme_;
  columns_ = t.density == Density::Regular ? 5 : 4;
  gap_ = t.m.gapM;
  const int count = (int)apps_.size();
  // While Wi-Fi setup is pending the note must stay visible: one row, paged.
  rows_ = count > columns_ && !setupNote_ ? 2 : 1;
  // Square tiles, as wide as the columns allow.
  const int tileW = (area_.w - gap_ * (columns_ - 1)) / columns_;
  const int tileH = tileW;
  const bool paged = count > columns_ * rows_;
  const int headerH = t.font.headline->lineHeight > t.font.body->lineHeight + t.font.caption->lineHeight
                          ? t.font.headline->lineHeight
                          : t.font.body->lineHeight + t.font.caption->lineHeight;
  const int gridH = rows_ * tileH + (rows_ - 1) * gap_;
  const int dotsH = paged ? t.dp(10) : 0;
  const int noteH = rows_ == 1 ? t.m.row + t.dp(8) : 0;
  const int used = headerH + gridH + (paged ? t.dp(4) + dotsH : 0) + noteH;
  const int slots = noteH ? 4 : 3;  // gaps: above, between, [before the note], below
  int space = (area_.h - used) / slots;
  space = space < t.m.gapS ? t.m.gapS : space > t.dp(16) ? t.dp(16) : space;
  int y = area_.y + space;
  header_ = Rect(area_.x, y, area_.w, headerH);
  y += headerH + space;
  grid_ = Rect(area_.x, y, area_.w, gridH);
  y += gridH;
  if (paged) {
    dots_ = Rect(area_.x, y + t.dp(4), area_.w, dotsH);
    y += t.dp(4) + dotsH;
  } else {
    dots_ = Rect();
  }
  note_ = noteH ? Rect(area_.x, y + space, area_.w, noteH) : Rect();
  // With several pages, a pill of previous/next buttons sits at the right end
  // of the header; swiping still works too.
  Rect textArea = header_;
  if (paged) {
    pager_ = trailing(header_, t.dp(88));
    pager_ = center(pager_, pager_.w, t.m.control);
    Rect halves[2];
    row(pager_, {flex(), flex()}, 0, halves);
    prev_ = halves[0];
    next_ = halves[1];
    textArea.w = area_.w - pager_.w - t.m.gapM;
  } else {
    pager_ = prev_ = next_ = Rect();
  }
  // The time (the widest it can be) on the left, the words right-aligned beside it.
  const int timeW = Canvas::textWidth(*t.font.headline, "88:88 AM") < textArea.w / 2
                        ? Canvas::textWidth(*t.font.headline, "88:88 AM")
                        : textArea.w / 2;
  Rect parts[2];
  row(textArea, {px(timeW), flex()}, t.m.gapM, parts);
  time_ = Rect(parts[0].x, header_.y, parts[0].w, headerH);
  Rect lines[2];
  column(center(parts[1], parts[1].w, t.font.body->lineHeight + t.font.caption->lineHeight),
         {px(t.font.body->lineHeight), px(t.font.caption->lineHeight)}, 0, lines);
  greeting_ = lines[0];
  date_ = lines[1];
}

bool LauncherApp::HomeScreen::showPage(int page, AppContext &ctx) {
  if (page < 0 || page >= pageCount() || page == page_) return false;
  page_ = page;
  const Theme &t = ctx.theme();
  ctx.invalidate(grid_.inset(-glassOutset(t)).unite(dots_).unite(pager_.inset(-glassOutset(t))));
  return true;
}

Rect LauncherApp::HomeScreen::tile(int slot) const { return grid(grid_, columns_, rows_, slot, gap_); }

int LauncherApp::HomeScreen::tileAt(int x, int y) const {
  for (int slot = 0; slot < perPage(); ++slot) {
    const int index = page_ * perPage() + slot;
    if (index >= (int)apps_.size()) break;
    if (tile(slot).contains(x, y)) return index;
  }
  return -1;
}

void LauncherApp::HomeScreen::refreshText(AppContext &ctx) {
  const LocalTime now = ctx.localTime();
  char clock[sizeof(timeText_)], greeting[sizeof(greetingText_)], date[sizeof(dateText_)];
  if (now.valid) ctx.formatTime(now, clock, sizeof(clock));
  else snprintf(clock, sizeof(clock), "--:--");
  snprintf(greeting, sizeof(greeting), "%s", greetingFor(now));
  if (now.valid) {
    snprintf(date, sizeof(date), "%.3s %d %.3s \xc2\xb7 %s", weekdayName(now.weekday), now.day, monthName(now.month),
             phaseName(ctx.theme().phase));
  } else {
    snprintf(date, sizeof(date), "Set the time in Settings");
  }
  if (strcmp(clock, timeText_) != 0) {
    memcpy(timeText_, clock, sizeof(clock));
    ctx.invalidate(time_);
  }
  if (strcmp(greeting, greetingText_) != 0) {
    memcpy(greetingText_, greeting, sizeof(greeting));
    ctx.invalidate(greeting_);
  }
  if (strcmp(date, dateText_) != 0) {
    memcpy(dateText_, date, sizeof(date));
    ctx.invalidate(date_);
  }
  char title[sizeof(noteTitle_)], text[sizeof(noteText_)];
  const NetworkStatus net = ctx.network().status();
  const bool setup = net.present && net.state == WifiState::NotConfigured;
  if (setup != setupNote_) {
    setupNote_ = setup;
    arrange();
    if (page_ >= pageCount()) page_ = 0;
    ctx.invalidate(ctx.contentRect());
  }
  if (setup) {
    snprintf(title, sizeof(title), "Set up Wi-Fi");
    snprintf(text, sizeof(text), "For the time and the weather");
  } else if (now.valid) {
    const MoonPhase moon = moonPhase(now.epoch);
    snprintf(title, sizeof(title), "%s", moon.name);
    snprintf(text, sizeof(text), "%d%% lit \xc2\xb7 %s sky", (int)(moon.illumination * 100 + 0.5f),
             phaseName(ctx.theme().phase));
  } else {
    snprintf(title, sizeof(title), "StellaOS");
    snprintf(text, sizeof(text), "Waiting for the time");
  }
  if (strcmp(title, noteTitle_) || strcmp(text, noteText_)) {
    memcpy(noteTitle_, title, sizeof(title));
    memcpy(noteText_, text, sizeof(text));
    ctx.invalidate(note_.inset(-glassOutset(ctx.theme())));
  }
}

void LauncherApp::HomeScreen::onShow(AppContext &ctx) {
  apps_.clear();
  for (size_t i = 0; i < ctx.appCount(); ++i) {
    const AppInfo *info = ctx.appAt(i);
    if (info && !(info->flags & (kAppHome | kAppHidden))) apps_.push(info);
  }
  arrange();
  if (page_ >= pageCount()) page_ = 0;
  pressed_ = -1;
  refreshText(ctx);
}

uint32_t LauncherApp::HomeScreen::onTick(AppContext &ctx) {
  refreshText(ctx);
  return 15000;
}

void LauncherApp::HomeScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  c.drawTextIn(*t.font.headline, timeText_, time_, p.text);
  c.drawTextIn(*t.font.body, greetingText_, greeting_, p.text, Align::Right);
  c.drawTextIn(*t.font.caption, dateText_, date_, p.textMuted, Align::Right);

  for (int slot = 0; slot < perPage(); ++slot) {
    const int index = page_ * perPage() + slot;
    if (index >= (int)apps_.size()) break;
    const Rect r = tile(slot);
    if (!c.visible(r.inset(-glassOutset(t)))) continue;
    const AppInfo &app = *apps_[index];
    paintGlass(c, t, r, t.m.radiusCard);
    if (index == pressed_) c.fillRoundRect(r, t.m.radiusCard, p.accent, 40);
    Rect parts[2];
    column(r.inset(t.dp(6), t.dp(8)), {flex(), px(t.font.caption->lineHeight)}, t.dp(4), parts);
    const int radius = (parts[0].h < parts[0].w ? parts[0].h : parts[0].w) / 2;
    paintBubble(c, t, parts[0].centerX(), parts[0].centerY(), radius, app.tint);
    if (app.icon) app.icon(c, center(parts[0], radius, radius), p.accent);
    c.drawTextIn(*t.font.caption, app.name, parts[1], p.text, Align::Center);
  }

  if (!note_.empty() && c.visible(note_.inset(-glassOutset(t)))) {
    paintGlass(c, t, note_, t.m.radiusCard);
    Rect inner = note_.inset(t.m.padding, 0);
    const int r = t.m.icon / 2 + t.dp(2);
    if (notePressed_) c.fillRoundRect(note_, t.m.radiusCard, p.accent, 40);
    paintBubble(c, t, inner.x + r, inner.centerY(), r, setupNote_ ? 2 : 1);
    (setupNote_ ? icons::wifi : icons::moon)(c, center(Rect(inner.x, inner.y, 2 * r, inner.h), t.dp(16), t.dp(16)),
                                             p.accent);
    inner = Rect(inner.x + 2 * r + t.m.gapM, inner.y, inner.w - 2 * r - t.m.gapM, inner.h);
    Rect lines[2];
    column(center(inner, inner.w, t.font.body->lineHeight + t.font.caption->lineHeight),
           {px(t.font.body->lineHeight), px(t.font.caption->lineHeight)}, 0, lines);
    c.drawTextIn(*t.font.body, noteTitle_, lines[0], p.text);
    c.drawTextIn(*t.font.caption, noteText_, lines[1], p.textMuted);
    (setupNote_ ? icons::chevronRight : icons::sparkle)(c, trailing(inner, t.dp(12)), p.accent);
  }

  const int pages = pageCount();
  if (!pager_.empty() && c.visible(pager_.inset(-glassOutset(t)))) {
    paintGlass(c, t, pager_, pager_.h / 2);
    const int r = pager_.h / 2;
    if (prevPressed_) c.fillRoundRect(prev_, r, p.accent, 46);
    if (nextPressed_) c.fillRoundRect(next_, r, p.accent, 46);
    c.vline(pager_.centerX(), pager_.y + t.dp(8), pager_.h - t.dp(16), p.line, p.lineAlpha);
    const int icon = t.dp(14);
    const Color faint = blend(p.glass, p.textMuted, 110);
    icons::back(c, center(prev_, icon, icon), page_ > 0 ? p.accent : faint);
    icons::chevronRight(c, center(next_, icon, icon), page_ < pages - 1 ? p.accent : faint);
  }
  if (pages > 1) {
    const int dot = t.dp(3), gap = t.dp(10);
    const int x0 = dots_.centerX() - (pages - 1) * gap / 2;
    for (int i = 0; i < pages; ++i)
      c.fillCircle(x0 + i * gap, dots_.centerY(), dot, i == page_ ? p.accent : p.textMuted, i == page_ ? 255 : 110);
  }
}

bool LauncherApp::HomeScreen::onEvent(const Event &e, AppContext &ctx) {
  const Theme &t = ctx.theme();
  if (setupNote_ && trackPress(note_, e, notePressed_, ctx, note_.inset(-glassOutset(t)))) {
    ctx.launch("settings", "wifi");
    return true;
  }
  if (!pager_.empty()) {
    const Rect repaint = pager_.inset(-glassOutset(t));
    if (trackPress(prev_, e, prevPressed_, ctx, repaint)) return showPage(page_ - 1, ctx) || true;
    if (trackPress(next_, e, nextPressed_, ctx, repaint)) return showPage(page_ + 1, ctx) || true;
    if (pager_.contains(e.startX, e.startY) && e.isPointer()) return true;
  }
  switch (e.type) {
    case EventType::Press:
      pressed_ = tileAt(e.x, e.y);
      if (pressed_ >= 0) ctx.invalidate(grid_.inset(-glassOutset(t)));
      return true;
    case EventType::Drag:
      if (pressed_ >= 0) {
        pressed_ = -1;
        ctx.invalidate(grid_.inset(-glassOutset(t)));
      }
      return true;
    case EventType::Release: {
      if (pressed_ >= 0) {
        pressed_ = -1;
        ctx.invalidate(grid_.inset(-glassOutset(t)));
      }
      const int dx = e.x - e.startX;
      if (e.dragged && (dx > t.dp(40) || dx < -t.dp(40))) {
        showPage(page_ + (dx < 0 ? 1 : -1), ctx);
      }
      return true;
    }
    case EventType::Tap: {
      const int index = tileAt(e.x, e.y);
      if (index >= 0) ctx.launch(apps_[index]->id);
      return true;
    }
    default:
      return false;
  }
}

}  // namespace apps
}  // namespace stella
