#include "settings_app.h"

#include <stdio.h>
#include <string.h>

#include "../core/text.h"
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

SettingsApp::SettingsApp() : main_(*this), wifi_(*this) {}

const AppInfo &SettingsApp::info() const {
  static const AppInfo kInfo{"settings", "Settings", icons::gear, 1, 0};
  return kInfo;
}

void SettingsApp::onTopic(uint16_t topicId, AppContext &ctx) {
  if (topicId == topics::kNetwork || topicId == topics::kWifiScan) wifi_.refresh(ctx);
}

void SettingsApp::onRoute(const char *route, AppContext &ctx) {
  if (route && strcmp(route, "wifi") == 0) ctx.push(wifi_);
}

// ------------------------------------------------------------------ main list

void SettingsApp::MainScreen::layout(const Rect &content, const Theme &t) {
  const Rect area = content.inset(t.m.margin, t.m.gapS);
  // Rows shrink a little on short panels so all five stay visible.
  const int rowH = kRows * t.m.row <= area.h ? t.m.row : area.h / kRows;
  card_ = Rect(area.x, area.y, area.w, kRows * rowH);
  for (int i = 0; i < kRows; ++i) {
    rows_[i].bounds = Rect(card_.x, card_.y + i * rowH, card_.w, rowH);
    rows_[i].separator = i < kRows - 1;
  }
}

void SettingsApp::MainScreen::onShow(AppContext &ctx) {
  const SystemSettings &s = ctx.settings();
  snprintf(appearance_, sizeof(appearance_), "%s", kAppearanceLabels[(int)s.appearance]);
  snprintf(time_, sizeof(time_), "%s \xc2\xb7 %s", s.clock24h ? "24 h" : "12 h", zoneLabel(s.timezone));
  const NetworkStatus net = ctx.network().status();
  snprintf(wifi_, sizeof(wifi_), "%s",
           !net.present                              ? "Not available"
           : net.state == WifiState::Connected       ? net.ssid
           : net.state == WifiState::Connecting      ? "Joining..."
           : net.state == WifiState::NotConfigured   ? "Off"
                                                     : "Not connected");
  const struct {
    const char *title;
    IconFn icon;
    uint8_t tint;
    const char *detail;
  } rows[kRows] = {{"Wi-Fi", icons::wifi, 2, wifi_},
                   {"Display", icons::sun, 3, nullptr},
                   {"Appearance", icons::appearance, 1, appearance_},
                   {"Time", icons::clock, 2, time_},
                   {"About", icons::info, 0, nullptr}};
  for (int i = 0; i < kRows; ++i) {
    rows_[i].title = rows[i].title;
    rows_[i].icon = rows[i].icon;
    rows_[i].tint = rows[i].tint;
    rows_[i].detail = rows[i].detail;
  }
}

void SettingsApp::MainScreen::draw(Canvas &c, const Theme &t) {
  paintGlass(c, t, card_, t.m.radiusCard);
  for (const ListRow &row : rows_) row.draw(c, t);
}

bool SettingsApp::MainScreen::onEvent(const Event &e, AppContext &ctx) {
  Screen *targets[kRows] = {&app_.wifi_, &app_.display_, &app_.appearance_, &app_.time_, &app_.about_};
  for (int i = 0; i < kRows; ++i) {
    if (rows_[i].handle(e, ctx)) {
      ctx.push(*targets[i]);
      return true;
    }
  }
  return false;
}

// ------------------------------------------------------------------ wi-fi

void SettingsApp::WifiScreen::layout(const Rect &content, const Theme &t) {
  Rect r[3];
  column(content.inset(t.m.margin, t.m.gapS), {px(t.m.row + t.dp(4)), px(t.m.control - t.dp(8)), flex()},
         t.m.gapS, r);
  statusCard_ = r[0];
  Rect s[3];
  row(r[0].inset(t.m.padding, 0), {px(t.m.icon + t.dp(4)), flex(), px(t.dp(64))}, t.m.gapM, s);
  statusIcon_ = s[0];
  statusText_ = s[1];
  forget_.bounds = layout::center(s[2], s[2].w, t.m.control - t.dp(10));
  forget_.label = "Forget";
  forget_.style = ButtonStyle::Quiet;
  header_ = r[1];
  Rect h[2];
  row(r[1], {flex(), px(t.dp(64))}, t.m.gapS, h);
  scan_.bounds = h[1];
  scan_.label = "Scan";
  scan_.style = ButtonStyle::Quiet;
  listCard_ = r[2];
  list_.bounds = r[2].inset(t.dp(2), t.dp(4));
  list_.rowHeight = t.dp(32);
}

void SettingsApp::WifiScreen::refresh(AppContext &ctx) {
  net_ = ctx.network().status();
  networks_ = ctx.network().scan();
  list_.count = networks_.count;
  if (list_.offset > list_.maxOffset()) list_.offset = list_.maxOffset();
  switch (net_.state) {
    case WifiState::Unavailable:
      snprintf(headline_, sizeof(headline_), "No Wi-Fi on this device");
      detail_[0] = '\0';
      break;
    case WifiState::NotConfigured:
      snprintf(headline_, sizeof(headline_), "Not connected");
      snprintf(detail_, sizeof(detail_), "Choose a network below");
      break;
    case WifiState::Connecting:
      snprintf(headline_, sizeof(headline_), "Joining %s", net_.ssid);
      snprintf(detail_, sizeof(detail_), "Connecting\xe2\x80\xa6");
      break;
    case WifiState::Connected:
      snprintf(headline_, sizeof(headline_), "%s", net_.ssid);
      snprintf(detail_, sizeof(detail_), "Connected \xc2\xb7 %s", net_.ip);
      break;
    case WifiState::Disconnected:
      snprintf(headline_, sizeof(headline_), "%s", net_.ssid);
      snprintf(detail_, sizeof(detail_), "%.32s \xc2\xb7 retrying", net_.error[0] ? net_.error : "Not connected");
      break;
  }
  forget_.enabled = net_.state != WifiState::NotConfigured && net_.state != WifiState::Unavailable;
  scan_.enabled = net_.present && !networks_.scanning;
  scan_.label = networks_.scanning ? "Scanning" : "Scan";
  if (isVisible()) ctx.invalidate(ctx.contentRect());
}

void SettingsApp::WifiScreen::onShow(AppContext &ctx) {
  refresh(ctx);
  if (net_.present) ctx.network().requestScan();
}

void SettingsApp::WifiScreen::drawNetwork(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed,
                                          void *self) {
  const WifiScreen &screen = *static_cast<const WifiScreen *>(self);
  const WifiNetwork &n = screen.networks_.networks[index];
  const bool current = screen.net_.state == WifiState::Connected && strcmp(n.ssid, screen.net_.ssid) == 0;
  if (pressed) c.fillRoundRect(row.inset(t.dp(4), 1), t.m.radiusControl, t.color.accent, 36);
  Rect parts[3];
  layout::row(row.inset(t.m.padding, 0), {flex(), px(t.dp(16)), px(t.dp(18))}, t.m.gapS, parts);
  c.drawTextIn(*t.font.body, n.ssid, parts[0], current ? t.color.accent : t.color.text);
  if (n.secured) icons::lock(c, center(parts[1], t.dp(16), t.dp(16)), t.color.textMuted);
  icons::wifiBars(c, center(parts[2], t.dp(16), t.dp(16)), current ? t.color.accent : t.color.textMuted,
                  signalBars(n.rssi));
  if (index < screen.networks_.count - 1)
    paintHairline(c, t, row.x + t.m.padding, row.bottom() - 1, row.w - 2 * t.m.padding);
}

void SettingsApp::WifiScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  paintGlass(c, t, statusCard_, t.m.radiusCard);
  const int r = statusIcon_.w / 2;
  paintBubble(c, t, statusIcon_.centerX(), statusIcon_.centerY(), r, 2);
  icons::wifiBars(c, center(statusIcon_, t.dp(16), t.dp(16)), p.accent,
                  net_.state == WifiState::Connected ? signalBars(net_.rssi) : 0);
  Rect lines[2];
  column(center(statusText_, statusText_.w, t.font.body->lineHeight + t.font.caption->lineHeight),
         {px(t.font.body->lineHeight), px(t.font.caption->lineHeight)}, 0, lines);
  c.drawTextIn(*t.font.body, headline_, lines[0], p.text);
  c.drawTextIn(*t.font.caption, detail_, lines[1], net_.state == WifiState::Disconnected ? p.danger : p.textMuted);
  if (forget_.enabled) forget_.draw(c, t);

  c.drawTextIn(*t.font.caption, "Networks", header_.inset(t.dp(4), 0), p.textMuted);
  scan_.draw(c, t);
  paintGlass(c, t, listCard_, t.m.radiusCard);
  if (networks_.count == 0) {
    const char *empty = !net_.present ? "" : networks_.scanning ? "Looking for networks\xe2\x80\xa6"
                        : networks_.failed                        ? "Scan failed \xc2\xb7 tap Scan to retry"
                                                                  : "No networks found";
    c.drawTextIn(*t.font.caption, empty, listCard_, p.textMuted, Align::Center);
  }
  list_.draw(c, t, drawNetwork, this);
}

void SettingsApp::WifiScreen::submitPassword(const char *text, void *self, AppContext &ctx) {
  WifiScreen &screen = *static_cast<WifiScreen *>(self);
  ctx.network().join(screen.joining_, text);
  ctx.pop();
  char message[48];
  snprintf(message, sizeof(message), "Joining %.30s", screen.joining_);
  ctx.toast(message);
}

bool SettingsApp::WifiScreen::onEvent(const Event &e, AppContext &ctx) {
  if (forget_.enabled && forget_.handle(e, ctx)) {
    ctx.network().forget();
    ctx.toast("Network forgotten");
    return true;
  }
  if (scan_.enabled && scan_.handle(e, ctx)) {
    ctx.network().requestScan();
    return true;
  }
  const int picked = list_.handle(e, ctx);
  if (picked < 0) return true;
  const WifiNetwork &n = networks_.networks[picked];
  copyText(joining_, sizeof(joining_), n.ssid);
  if (n.secured) {
    app_.password_.open(joining_, "Join", /*secret=*/true, submitPassword, this, /*minLength=*/8);
    ctx.push(app_.password_);
  } else {
    // Open network: join directly (no password screen to close).
    ctx.network().join(joining_, "");
    char message[48];
    snprintf(message, sizeof(message), "Joining %.30s", joining_);
    ctx.toast(message);
  }
  return true;
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
  Rect r[4];
  column(content.inset(t.m.margin, t.m.gapS),
         {px(t.m.control), px(2 * t.font.body->lineHeight), px(t.m.control), flex()}, t.m.gapS, r);
  mode_.bounds = r[0];
  note_ = r[1];
  nightCard_ = r[2];
  nightDim_.bounds = r[2].inset(t.m.padding, 0);
  skies_ = Rect(r[3].x, r[3].y, r[3].w, r[3].h < t.dp(64) ? r[3].h : t.dp(64));
}

void SettingsApp::AppearanceScreen::onShow(AppContext &ctx) {
  mode_.labels = kAppearanceLabels;
  mode_.count = 3;
  mode_.selected = (uint8_t)ctx.settings().appearance;
  nightDim_.on = ctx.settings().nightDim;
}

void SettingsApp::AppearanceScreen::draw(Canvas &c, const Theme &t) {
  mode_.draw(c, t);
  paintGlass(c, t, nightCard_, t.m.radiusCard);
  c.drawTextIn(*t.font.body, "Dim at night", nightDim_.bounds, t.color.text);
  nightDim_.draw(c, t);
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
  if (nightDim_.handle(e, ctx)) {
    SystemSettings s = ctx.settings();
    s.nightDim = nightDim_.on;
    ctx.applySettings(s);
    return true;
  }
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
