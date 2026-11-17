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
  area_ = content.inset(t.m.margin, t.m.gapS);
  arrange();
}

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
  Rect parts[6];
  column(area_,
         {px(t.font.headline->lineHeight), px(t.font.caption->lineHeight), px(rows_ * tileH + (rows_ - 1) * gap_),
          px(paged ? t.dp(10) : 0), flex(), px(rows_ == 1 ? t.m.row + t.dp(8) : 0)},
         t.m.gapS, parts);
  greeting_ = parts[0];
  date_ = parts[1];
  grid_ = parts[2];
  dots_ = parts[3];
  note_ = parts[5];
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
  char greeting[sizeof(greetingText_)], date[sizeof(dateText_)];
  snprintf(greeting, sizeof(greeting), "%s", greetingFor(now));
  if (now.valid) {
    snprintf(date, sizeof(date), "%s %d %s \xc2\xb7 %s", weekdayName(now.weekday), now.day, monthName(now.month),
             phaseName(ctx.theme().phase));
  } else {
    snprintf(date, sizeof(date), "Set the time in Settings");
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
    snprintf(title, sizeof(title), "Stella");
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
  c.drawTextIn(*t.font.headline, greetingText_, greeting_, p.text);
  c.drawTextIn(*t.font.caption, dateText_, date_, p.textMuted);

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
        const int next = page_ + (dx < 0 ? 1 : -1);
        if (next >= 0 && next < pageCount()) {
          page_ = next;
          ctx.invalidate(grid_.inset(-glassOutset(t)).unite(dots_));
        }
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
