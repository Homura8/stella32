#include "settings_app.h"

#include <stdio.h>
#include <string.h>

#include "../ui/layout.h"
#include "../ui/materials.h"

namespace stella {
namespace apps {

using namespace layout;

namespace {

const char *const kAppearanceLabels[] = {"Auto", "Light", "Dark"};
const char *const kDimLabels[] = {"Never", "30 s", "1 min", "5 min"};
const uint16_t kDimSeconds[] = {0, 30, 60, 300};
const char *const kOffLabels[] = {"Never", "5 min", "15 min", "1 h"};
const uint16_t kOffSeconds[] = {0, 300, 900, 3600};

uint8_t nearestIndex(const uint16_t *values, int count, uint16_t value) {
  int best = 0;
  for (int i = 1; i < count; ++i) {
    const int d = values[i] > value ? values[i] - value : value - values[i];
    const int bd = values[best] > value ? values[best] - value : value - values[best];
    if (d < bd) best = i;
  }
  return (uint8_t)best;
}

const char *zoneLabel(const char *posix) {
  int count = 0;
  const TimezonePreset *presets = timezonePresets(count);
  for (int i = 0; i < count; ++i)
    if (strcmp(presets[i].posix, posix) == 0) return presets[i].label;
  return "Custom";
}

const char *resetName(ResetCause c) {
  switch (c) {
    case ResetCause::PowerOn: return "Power on";
    case ResetCause::Software: return "Software restart";
    case ResetCause::Watchdog: return "Watchdog";
    case ResetCause::Brownout: return "Brownout";
    case ResetCause::Panic: return "Crash";
    case ResetCause::External: return "Reset button";
    default: return "Unknown";
  }
}

// Section label above a control, in the muted caption style.
void sectionLabel(Canvas &c, const Theme &t, const Rect &r, const char *text) {
  c.drawTextIn(*t.font.caption, text, r, t.color.textMuted);
}

}  // namespace

SettingsApp::SettingsApp() : main_(*this) {}

const AppInfo &SettingsApp::info() const {
  static const AppInfo kInfo{"settings", "Settings", icons::gear, 1, 0};
  return kInfo;
}

void SettingsApp::onTopic(uint16_t, AppContext &) {}

// ------------------------------------------------------------------ main list

void SettingsApp::MainScreen::layout(const Rect &content, const Theme &t) {
  const Rect area = content.inset(t.m.margin, t.m.gapS);
  card_ = Rect(area.x, area.y, area.w, 4 * t.m.row);
  for (int i = 0; i < 4; ++i) {
    rows_[i].bounds = Rect(card_.x, card_.y + i * t.m.row, card_.w, t.m.row);
    rows_[i].separator = i < 3;
  }
}

void SettingsApp::MainScreen::onShow(AppContext &ctx) {
  const SystemSettings &s = ctx.settings();
  snprintf(appearance_, sizeof(appearance_), "%s", kAppearanceLabels[(int)s.appearance]);
  snprintf(time_, sizeof(time_), "%s \xc2\xb7 %s", s.clock24h ? "24 h" : "12 h", zoneLabel(s.timezone));
  rows_[0].title = "Display";
  rows_[0].icon = icons::sun;
  rows_[0].tint = 3;
  rows_[1].title = "Appearance";
  rows_[1].icon = icons::appearance;
  rows_[1].tint = 1;
  rows_[1].detail = appearance_;
  rows_[2].title = "Time";
  rows_[2].icon = icons::clock;
  rows_[2].tint = 2;
  rows_[2].detail = time_;
  rows_[3].title = "About";
  rows_[3].icon = icons::info;
  rows_[3].tint = 0;
}

void SettingsApp::MainScreen::draw(Canvas &c, const Theme &t) {
  paintGlass(c, t, card_, t.m.radiusCard);
  for (const ListRow &row : rows_) row.draw(c, t);
}

bool SettingsApp::MainScreen::onEvent(const Event &e, AppContext &ctx) {
  Screen *targets[] = {&app_.display_, &app_.appearance_, &app_.time_, &app_.about_};
  for (int i = 0; i < 4; ++i) {
    if (rows_[i].handle(e, ctx)) {
      ctx.push(*targets[i]);
      return true;
    }
  }
  return false;
}

// ------------------------------------------------------------------ display

void SettingsApp::DisplayScreen::layout(const Rect &content, const Theme &t) {
  card_ = content.inset(t.m.margin, t.m.gapS);
  const int label = t.font.caption->lineHeight;
  Rect r[6];
  column(card_.inset(t.m.padding, t.m.padding),
         {px(label), px(t.m.control), px(label), px(t.m.control), px(label), px(t.m.control)}, t.dp(3), r);
  brightnessLabel_ = r[0];
  brightness_.bounds = r[1];
  dimLabel_ = r[2];
  dim_.bounds = r[3].inset(0, t.dp(2));
  offLabel_ = r[4];
  off_.bounds = r[5].inset(0, t.dp(2));
  card_.h = r[5].bottom() + t.m.padding - card_.y;
}

void SettingsApp::DisplayScreen::onShow(AppContext &ctx) {
  const SystemSettings &s = ctx.settings();
  brightness_.minimum = 16;
  brightness_.maximum = 255;
  brightness_.value = s.brightness;
  dim_.labels = kDimLabels;
  dim_.count = 4;
  dim_.selected = nearestIndex(kDimSeconds, 4, s.dimAfterSec);
  off_.labels = kOffLabels;
  off_.count = 4;
  off_.selected = nearestIndex(kOffSeconds, 4, s.sleepAfterSec);
}

void SettingsApp::DisplayScreen::draw(Canvas &c, const Theme &t) {
  paintGlass(c, t, card_, t.m.radiusCard);
  sectionLabel(c, t, brightnessLabel_, "Brightness");
  brightness_.draw(c, t);
  sectionLabel(c, t, dimLabel_, "Dim when idle");
  dim_.draw(c, t);
  sectionLabel(c, t, offLabel_, "Turn screen off");
  off_.draw(c, t);
}

bool SettingsApp::DisplayScreen::onEvent(const Event &e, AppContext &ctx) {
  SystemSettings s = ctx.settings();
  if (brightness_.handle(e, ctx, ctx.theme())) {
    s.brightness = (uint8_t)brightness_.value;
    ctx.applySettings(s);
    return true;
  }
  if (dim_.handle(e, ctx)) {
    s.dimAfterSec = kDimSeconds[dim_.selected];
    ctx.applySettings(s);
    return true;
  }
  if (off_.handle(e, ctx)) {
    s.sleepAfterSec = kOffSeconds[off_.selected];
    ctx.applySettings(s);
    return true;
  }
  return brightness_.dragging;
}

// ------------------------------------------------------------------ appearance

void SettingsApp::AppearanceScreen::layout(const Rect &content, const Theme &t) {
  Rect r[3];
  column(content.inset(t.m.margin, t.m.gapS), {px(t.m.control), px(2 * t.font.body->lineHeight), flex()},
         t.m.gapM, r);
  mode_.bounds = r[0];
  note_ = r[1];
  skies_ = Rect(r[2].x, r[2].y, r[2].w, r[2].h < t.dp(64) ? r[2].h : t.dp(64));
}

void SettingsApp::AppearanceScreen::onShow(AppContext &ctx) {
  mode_.labels = kAppearanceLabels;
  mode_.count = 3;
  mode_.selected = (uint8_t)ctx.settings().appearance;
}

void SettingsApp::AppearanceScreen::draw(Canvas &c, const Theme &t) {
  mode_.draw(c, t);
  c.drawWrapped(*t.font.body,
                mode_.selected == 0 ? "Auto follows the sky: light through the day, starlit from dusk."
                                    : "The sky still drifts from dawn to night.",
                note_, t.color.textMuted, 2);
  // One swatch per sky phase, previewing the current mode's gradients.
  for (int i = 0; i < 4; ++i) {
    const Rect cell = grid(skies_, 4, 1, i, t.m.gapS);
    if (!c.visible(cell.inset(-glassOutset(t)))) continue;
    const Theme preview = makeTheme(t.density, t.color.dark, (SkyPhase)i);
    Rect parts[2];
    column(cell, {flex(), px(t.font.caption->lineHeight)}, t.dp(3), parts);
    const Rect sw = parts[0];
    c.fillRoundRect(sw.offset(0, t.dp(2)), t.m.radiusControl, t.color.shadow, t.color.shadowAlpha);
    for (int y = 0; y < sw.h; ++y) {
      const int k = sw.h > 1 ? y * 255 / (sw.h - 1) : 0;
      const Color row = k < 128 ? blend(preview.color.sky[0], preview.color.sky[1], (uint8_t)(k * 2))
                                : blend(preview.color.sky[1], preview.color.orbPink, (uint8_t)((k - 128) * 2));
      const Rect line(sw.x, sw.y + y, sw.w, 1);
      const Rect prev = c.pushClip(line);
      c.fillRoundRect(sw, t.m.radiusControl, row);
      c.restoreClip(prev);
    }
    c.strokeRoundRect(sw, t.m.radiusControl, 1, t.color.rim, t.color.rimAlpha);
    c.drawTextIn(*t.font.caption, phaseName((SkyPhase)i), parts[1],
                 (SkyPhase)i == t.phase ? t.color.accent : t.color.textMuted, Align::Center);
  }
}

bool SettingsApp::AppearanceScreen::onEvent(const Event &e, AppContext &ctx) {
  if (!mode_.handle(e, ctx)) return false;
  SystemSettings s = ctx.settings();
  s.appearance = (Appearance)mode_.selected;
  ctx.applySettings(s);
  ctx.invalidate(note_);
  return true;
}

// ------------------------------------------------------------------ time

void SettingsApp::TimeScreen::layout(const Rect &content, const Theme &t) {
  Rect r[3];
  column(content.inset(t.m.margin, t.m.gapS), {px(t.m.row), px(t.font.caption->lineHeight), flex()}, t.m.gapS, r);
  toggleCard_ = r[0];
  clock24_.bounds = r[0].inset(t.m.padding, 0);
  zoneLabel_ = r[1].inset(t.dp(4), 0);
  zoneCard_ = r[2];
  zones_.bounds = r[2].inset(t.dp(2), t.dp(4));
  zones_.rowHeight = t.dp(30);
}

void SettingsApp::TimeScreen::onShow(AppContext &ctx) {
  clock24_.on = ctx.settings().clock24h;
  int count = 0;
  const TimezonePreset *presets = timezonePresets(count);
  zones_.count = count;
  selected_ = -1;
  for (int i = 0; i < count; ++i)
    if (strcmp(presets[i].posix, ctx.settings().timezone) == 0) selected_ = i;
  if (selected_ >= 0) zones_.scrollTo(selected_);
}

void SettingsApp::TimeScreen::drawZone(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed,
                                       void *self) {
  const TimeScreen &screen = *static_cast<const TimeScreen *>(self);
  int count = 0;
  const TimezonePreset *presets = timezonePresets(count);
  if (pressed) c.fillRoundRect(row.inset(t.dp(4), 1), t.m.radiusControl, t.color.accent, 36);
  const Rect inner = row.inset(t.m.padding, 0);
  const bool selected = index == screen.selected_;
  c.drawTextIn(*t.font.body, presets[index].label, inner, selected ? t.color.accent : t.color.text);
  if (selected) icons::check(c, trailing(inner, t.dp(14)), t.color.accent);
}

void SettingsApp::TimeScreen::draw(Canvas &c, const Theme &t) {
  paintGlass(c, t, toggleCard_, t.m.radiusCard);
  c.drawTextIn(*t.font.body, "24-hour clock", clock24_.bounds, t.color.text);
  clock24_.draw(c, t);
  c.drawTextIn(*t.font.caption, "Time zone", zoneLabel_, t.color.textMuted);
  paintGlass(c, t, zoneCard_, t.m.radiusCard);
  zones_.draw(c, t, drawZone, this);
}

bool SettingsApp::TimeScreen::onEvent(const Event &e, AppContext &ctx) {
  SystemSettings s = ctx.settings();
  if (clock24_.handle(e, ctx)) {
    s.clock24h = clock24_.on;
    ctx.applySettings(s);
    return true;
  }
  const int picked = zones_.handle(e, ctx);
  if (picked >= 0) {
    int count = 0;
    const TimezonePreset *presets = timezonePresets(count);
    snprintf(s.timezone, sizeof(s.timezone), "%s", presets[picked].posix);
    selected_ = picked;
    ctx.applySettings(s);
    ctx.invalidate(zones_.bounds);
    return true;
  }
  return true;
}

// ------------------------------------------------------------------ about

void SettingsApp::AboutScreen::layout(const Rect &content, const Theme &t) {
  card_ = content.inset(t.m.margin, t.m.gapS);
  list_.bounds = card_.inset(t.dp(2), t.dp(4));
  list_.rowHeight = t.dp(24);
  list_.count = kRows;
}

void SettingsApp::AboutScreen::refresh(AppContext &ctx) {
  const SystemStatus s = ctx.status();
  char next[kRows][40];
  const uint32_t up = s.uptimeMs / 1000;
  snprintf(next[0], 40, "%s", s.board);
  snprintf(next[1], 40, "Stella %s", s.version);
  snprintf(next[2], 40, "%d x %d %s", s.width, s.height, s.density == Density::Regular ? "regular" : "compact");
  snprintf(next[3], 40, "%lud %02luh %02lum", (unsigned long)(up / 86400), (unsigned long)(up / 3600 % 24),
           (unsigned long)(up / 60 % 60));
  snprintf(next[4], 40, "%lu KB free, %lu KB block", (unsigned long)(s.memory.freeHeap / 1024),
           (unsigned long)(s.memory.largestFreeBlock / 1024));
  snprintf(next[5], 40, "%lu KB", (unsigned long)(s.memory.minimumFreeHeap / 1024));
  snprintf(next[6], 40, "%s", resetName(s.resetCause));
  if (!s.network.present) snprintf(next[7], 40, "Not available");
  else if (s.network.connected) snprintf(next[7], 40, "%.24s %d dBm", s.network.ssid, s.network.rssi);
  else snprintf(next[7], 40, "Disconnected");
  if (memcmp(next, values_, sizeof(next)) != 0) {
    memcpy(values_, next, sizeof(next));
    ctx.invalidate(list_.bounds);
  }
}

void SettingsApp::AboutScreen::onShow(AppContext &ctx) { refresh(ctx); }
uint32_t SettingsApp::AboutScreen::onTick(AppContext &ctx) {
  refresh(ctx);
  return 2000;
}

void SettingsApp::AboutScreen::drawRow(Canvas &c, const Theme &t, int index, const Rect &r, bool, void *self) {
  static const char *const kLabels[kRows] = {"Board",       "Software",     "Display",     "Uptime",
                                             "Memory",      "Lowest free",  "Last reset",  "Network"};
  const AboutScreen &screen = *static_cast<const AboutScreen *>(self);
  Rect parts[2];
  row(r.inset(t.m.padding, 0), {px(t.dp(84)), flex()}, t.m.gapS, parts);
  c.drawTextIn(*t.font.caption, kLabels[index], parts[0], t.color.textMuted);
  c.drawTextIn(*t.font.caption, screen.values_[index], parts[1], t.color.text, Align::Right);
  if (index < kRows - 1) paintHairline(c, t, r.x + t.m.padding, r.bottom() - 1, r.w - 2 * t.m.padding);
}

void SettingsApp::AboutScreen::draw(Canvas &c, const Theme &t) {
  paintGlass(c, t, card_, t.m.radiusCard);
  list_.draw(c, t, drawRow, this);
}

bool SettingsApp::AboutScreen::onEvent(const Event &e, AppContext &ctx) {
  list_.handle(e, ctx);
  return true;
}

}  // namespace apps
}  // namespace stella
