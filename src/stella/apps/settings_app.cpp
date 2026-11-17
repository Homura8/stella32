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
const char *const kToneLabels[] = {"Soft", "Rich", "Vivid"};  // ColorTone order
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


}  // namespace

SettingsApp::SettingsApp() : main_(*this), wifi_(*this), display_(*this) {}

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
  list_.buttonWidth = t.dp(32);
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

namespace {

const char *const kBedsideLabels[] = {"Off", "Night", "Always"};
const char *const kRefreshLabels[] = {"Default", "90 Hz", "100 Hz", "119 Hz"};
const uint8_t kRefreshHz[] = {0, 90, 100, 119};
const char *const kTabLabels[] = {"Tone", "Colour", "Panel"};

// Rows of a settings card: as tall as a control when they fit, never below
// the 32 dp touch minimum (the card then takes what it needs).
int cardRowHeight(const Theme &t, int available, int rows) {
  const int want = t.m.control + t.dp(4);
  const int fit = (available - 2 * t.dp(4)) / rows;
  const int minimum = t.dp(32);
  return want <= fit ? want : fit < minimum ? minimum : fit;
}

}  // namespace

void SettingsApp::DisplayScreen::layout(const Rect &content, const Theme &t) {
  const Rect area = content.inset(t.m.margin, t.m.gapS);
  const int rowH = cardRowHeight(t, area.h, kRows);
  card_ = Rect(area.x, area.y, area.w, kRows * rowH + 2 * t.dp(4));
  const Rect inner = card_.inset(t.m.padding, t.dp(4));
  for (int i = 0; i < kRows; ++i) {
    rows_[i] = Rect(inner.x, inner.y + i * rowH, inner.w, rowH);
    Rect parts[2];
    row(rows_[i], {px(t.dp(96)), flex()}, t.m.gapM, parts);
    labels_[i] = parts[0];
    const Rect control = parts[1].inset(0, (rowH - t.m.control) / 2 > 0 ? (rowH - t.m.control) / 2 : 0);
    if (i == 0) brightness_.bounds = control;
    if (i == 1) nightDim_.bounds = rows_[i];
    if (i == 2) dim_.bounds = control.inset(0, t.dp(2));
    if (i == 3) off_.bounds = control.inset(0, t.dp(2));
    if (i == 4) bedside_.bounds = control.inset(0, t.dp(2));
  }
  calibrate_.bounds = Rect(card_.x, rows_[5].y, card_.w, rows_[5].h);
  calibrate_.title = "Calibrate";
  calibrate_.icon = nullptr;
  calibrate_.chevron = true;
}

void SettingsApp::DisplayScreen::onShow(AppContext &ctx) {
  const SystemSettings &s = ctx.settings();
  brightness_.minimum = 16;
  brightness_.maximum = 255;
  brightness_.value = s.brightness;
  nightDim_.on = s.nightDim;
  dim_.labels = kDimLabels;
  dim_.count = 4;
  dim_.selected = nearestIndex(kDimSeconds, 4, s.dimAfterSec);
  off_.labels = kOffLabels;
  off_.count = 4;
  off_.selected = nearestIndex(kOffSeconds, 4, s.sleepAfterSec);
  bedside_.labels = kBedsideLabels;
  bedside_.count = 3;
  bedside_.selected = s.bedside;
  const LocalTime now = ctx.localTime();
  night_ = !now.valid || phaseForHour(now.hour) == SkyPhase::Night;
  snprintf(calibrateDetail_, sizeof(calibrateDetail_), "%s \xc2\xb7 %u%%", toneName(s.colorTone),
           (unsigned)(ctx.theme().color.dark ? s.contrastDark : s.contrastLight));
  calibrate_.detail = calibrateDetail_;
}

void SettingsApp::DisplayScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  paintGlass(c, t, card_, t.m.radiusCard);
  static const char *const kLabels[kRows - 1] = {"Brightness", "Dim at night", "Dim when idle", "Screen off",
                                                 "Bedside"};
  for (int i = 0; i < kRows - 1; ++i) {
    c.drawTextIn(*t.font.body, kLabels[i], labels_[i], p.text);
    paintHairline(c, t, rows_[i].x, rows_[i].bottom() - 1, rows_[i].w);
  }
  // While night dimming is lowering the backlight, say so beside the slider.
  if (nightDim_.on && night_) {
    const int icon = t.dp(12);
    icons::moon(c, trailing(labels_[0], icon), p.accent);
  }
  brightness_.draw(c, t);
  nightDim_.draw(c, t);
  dim_.draw(c, t);
  off_.draw(c, t);
  bedside_.draw(c, t);
  calibrate_.draw(c, t);
}

bool SettingsApp::DisplayScreen::onEvent(const Event &e, AppContext &ctx) {
  SystemSettings s = ctx.settings();
  if (brightness_.handle(e, ctx, ctx.theme())) {
    s.brightness = (uint8_t)brightness_.value;
    ctx.applySettings(s);
    return true;
  }
  if (brightness_.dragging) return true;
  if (nightDim_.handle(e, ctx)) {
    s.nightDim = nightDim_.on;
    ctx.applySettings(s);
    ctx.invalidate(labels_[0]);
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
  if (bedside_.handle(e, ctx)) {
    s.bedside = bedside_.selected;
    ctx.applySettings(s);
    return true;
  }
  if (calibrate_.handle(e, ctx)) {
    ctx.push(app_.calibrate_);
    return true;
  }
  return false;
}

// ------------------------------------------------------------------ calibrate

void SettingsApp::CalibrateScreen::layout(const Rect &content, const Theme &t) {
  theme_ = t;
  content_ = content;
  dark_ = t.color.dark;
  arrange();
}

void SettingsApp::CalibrateScreen::arrange() {
  const Theme &t = theme_;
  const Rect area = content_.inset(t.m.margin, t.m.gapS);
  Rect r[2];
  column(area, {px(t.m.control), flex()}, t.m.gapM, r);
  Rect top[2];
  row(r[0], {flex(), px(t.dp(72))}, t.m.gapM, top);
  tabs_.bounds = top[0];
  reset_.bounds = top[1];
  reset_.label = "Reset";
  const Rect body = r[1];
  const int rowH = cardRowHeight(t, body.h * 3 / 5, 3);
  const int rows = rowCount();
  const Rect upper(body.x, body.y, body.w, body.h - rows * rowH - t.m.gapS);
  if (tabs_.selected == 2) {
    note_ = upper;
    strip_ = Rect();
  } else {
    strip_ = upper;
    note_ = Rect();
  }
  const int top0 = body.bottom() - rows * rowH;
  for (int i = 0; i < 3; ++i) {
    if (i >= rows) {
      rows_[i] = labels_[i] = Rect();
      continue;
    }
    rows_[i] = Rect(body.x, top0 + i * rowH, body.w, rowH);
    Rect parts[2];
    row(rows_[i].inset(t.dp(4), 0), {px(t.dp(104)), flex()}, t.m.gapM, parts);
    labels_[i] = parts[0];
    const Rect control = parts[1].inset(0, (rowH - t.m.control) / 2 > 0 ? (rowH - t.m.control) / 2 : 0);
    switch (tabs_.selected * 3 + i) {
      case 0: contrast_.bounds = control; break;
      case 1: whites_.bounds = control; break;
      case 2: warmth_.bounds = control; break;
      case 3: tone_.bounds = control.inset(0, t.dp(2)); break;
      case 4: dither_.bounds = rows_[i].inset(t.dp(4), 0); break;
      case 6: vcom_.bounds = control; break;
      case 7: refresh_.bounds = control.inset(0, t.dp(2)); break;
      default: break;
    }
  }
}

void SettingsApp::CalibrateScreen::load(const SystemSettings &s) {
  contrast_.value = dark_ ? s.contrastDark : s.contrastLight;
  whites_.value = s.whiteLevel;
  warmth_.value = s.warmth;
  tone_.selected = (uint8_t)s.colorTone;
  dither_.on = s.dither;
  vcom_.value = s.panelVcom;
  refresh_.selected = 0;
  for (uint8_t i = 0; i < 4; ++i)
    if (kRefreshHz[i] == s.panelRefreshHz) refresh_.selected = i;
}

void SettingsApp::CalibrateScreen::onShow(AppContext &ctx) {
  tabs_.labels = kTabLabels;
  tabs_.count = 3;
  tone_.labels = kToneLabels;
  tone_.count = 3;
  refresh_.labels = kRefreshLabels;
  refresh_.count = 4;
  Slider *const percent[3] = {&contrast_, &whites_, &warmth_};
  for (Slider *sl : percent) {
    sl->minimum = 0;
    sl->maximum = 100;
  }
  vcom_.minimum = -30;
  vcom_.maximum = 30;
  panelTrim_ = ctx.status().panelTrim;
  load(ctx.settings());
  arrange();
}

void SettingsApp::CalibrateScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  tabs_.draw(c, t);
  reset_.draw(c, t);
  if (!strip_.empty() && c.visible(strip_)) {
    // Compare with the same screen on a good monitor (docs/screenshots):
    // a grey ramp, the text inks, the accent and the tints.
    Rect parts[2];
    column(strip_, {flex(), flex()}, t.dp(4), parts);
    for (int i = 0; i < 8; ++i) {
      const uint8_t v = (uint8_t)(i * 255 / 7);
      c.fillRect(grid(parts[0], 8, 1, i, 0), lookColor(t.look, (uint32_t)v << 16 | (uint32_t)v << 8 | v));
    }
    Rect swatches[6];
    row(parts[1], {flex(), flex(), flex(), flex(), flex(), flex()}, t.dp(3), swatches);
    const Color inks[6] = {p.text, p.textMuted, p.accent, p.tints[0], p.tints[2], p.tints[3]};
    for (int i = 0; i < 6; ++i) c.fillRoundRect(swatches[i], t.dp(4), inks[i]);
    c.drawTextIn(*t.font.body, "Aa", swatches[0], p.accentText, Align::Center);
  }
  if (!note_.empty()) {
    c.drawWrapped(*t.font.caption,
                  panelTrim_ ? "These start at the panel driver's values, which most CYD projects use. Change them "
                               "only if the screen shimmers; Reset restores them."
                             : "This display has no panel adjustments.",
                  note_.inset(t.dp(4), t.dp(2)), p.textMuted, 4);
  }
  static const char *const kLabels[3][3] = {
      {nullptr, "White level", "Warmth"}, {"Colour", "Smooth sky", nullptr}, {"Flicker", "Refresh", nullptr}};
  for (int i = 0; i < rowCount(); ++i) {
    const char *label = kLabels[tabs_.selected][i];
    if (!label) label = dark_ ? "Dark contrast" : "Light contrast";
    c.drawTextIn(*t.font.body, label, labels_[i], tabs_.selected == 2 && !panelTrim_ ? p.textMuted : p.text);
  }
  switch (tabs_.selected) {
    case 0:
      contrast_.draw(c, t);
      whites_.draw(c, t);
      warmth_.draw(c, t);
      break;
    case 1:
      tone_.draw(c, t);
      dither_.draw(c, t);
      break;
    default:
      if (panelTrim_) {
        vcom_.draw(c, t);
        refresh_.draw(c, t);
      }
  }
}

bool SettingsApp::CalibrateScreen::onEvent(const Event &e, AppContext &ctx) {
  if (tabs_.handle(e, ctx)) {
    arrange();
    ctx.invalidate(ctx.contentRect());
    return true;
  }
  SystemSettings s = ctx.settings();
  if (reset_.handle(e, ctx)) {
    const SystemSettings &d = ctx.defaultSettings();
    if (tabs_.selected == 0) {
      (dark_ ? s.contrastDark : s.contrastLight) = dark_ ? d.contrastDark : d.contrastLight;
      s.whiteLevel = d.whiteLevel;
      s.warmth = d.warmth;
    } else if (tabs_.selected == 1) {
      s.colorTone = d.colorTone;
      s.dither = d.dither;
    } else {
      s.panelVcom = d.panelVcom;
      s.panelRefreshHz = d.panelRefreshHz;
    }
    ctx.applySettings(s);
    load(s);
    ctx.invalidate(ctx.contentRect());
    ctx.toast("Reset to this display's defaults");
    return true;
  }
  if (tabs_.selected == 0) {
    if (contrast_.handle(e, ctx, ctx.theme())) {
      (dark_ ? s.contrastDark : s.contrastLight) = (uint8_t)contrast_.value;
      ctx.applySettings(s);  // re-themes: the colours change
      return true;
    }
    if (whites_.handle(e, ctx, ctx.theme())) {
      s.whiteLevel = (uint8_t)whites_.value;
      ctx.applySettings(s);
      return true;
    }
    if (warmth_.handle(e, ctx, ctx.theme())) {
      s.warmth = (uint8_t)warmth_.value;
      ctx.applySettings(s);
      return true;
    }
    return contrast_.dragging || whites_.dragging || warmth_.dragging;
  }
  if (tabs_.selected == 1) {
    if (tone_.handle(e, ctx)) {
      s.colorTone = (ColorTone)tone_.selected;
      ctx.applySettings(s);
      return true;
    }
    if (dither_.handle(e, ctx)) {
      s.dither = dither_.on;
      ctx.applySettings(s);
      return true;
    }
    return false;
  }
  if (!panelTrim_) return false;
  if (vcom_.handle(e, ctx, ctx.theme())) {
    s.panelVcom = (int8_t)vcom_.value;
    ctx.applySettings(s);  // sent to the panel at once
    return true;
  }
  if (vcom_.dragging) return true;
  if (refresh_.handle(e, ctx)) {
    s.panelRefreshHz = kRefreshHz[refresh_.selected];
    ctx.applySettings(s);
    return true;
  }
  return false;
}

// ------------------------------------------------------------------ appearance

void SettingsApp::AppearanceScreen::layout(const Rect &content, const Theme &t) {
  Rect r[3];
  column(content.inset(t.m.margin, t.m.gapS), {px(t.m.control), px(t.font.caption->lineHeight), flex()}, t.m.gapS, r);
  mode_.bounds = r[0];
  note_ = r[1];
  // Swatches (as tall as fits, up to 88 dp) with their note right below.
  const int noteH = t.font.caption->lineHeight;
  const int room = r[2].h - t.m.gapL - noteH - t.m.gapS;
  skies_ = Rect(r[2].x, r[2].y + t.m.gapL, r[2].w, room < t.dp(88) ? room : t.dp(88));
  skyNote_ = Rect(r[2].x, skies_.bottom() + t.m.gapS, r[2].w, noteH);
}

void SettingsApp::AppearanceScreen::onShow(AppContext &ctx) {
  mode_.labels = kAppearanceLabels;
  mode_.count = 3;
  mode_.selected = (uint8_t)ctx.settings().appearance;
  skyPin_ = ctx.settings().skyPin;
  pressedSky_ = -1;
}

void SettingsApp::AppearanceScreen::draw(Canvas &c, const Theme &t) {
  mode_.draw(c, t);
  c.drawTextIn(*t.font.caption,
               mode_.selected == 0 ? "Light through the day, starlit from dusk"
                                   : "The sky still drifts from dawn to night",
               note_, t.color.textMuted);
  // One swatch per sky phase; tap one to keep that sky, tap it again to let
  // the sky follow the time.
  for (int i = 0; i < 4; ++i) {
    const Rect cell = grid(skies_, 4, 1, i, t.m.gapS);
    if (!c.visible(cell.inset(-glassOutset(t)))) continue;
    const Theme preview = makeTheme(t.density, t.color.dark, (SkyPhase)i, t.look);
    Rect parts[2];
    column(cell, {flex(), px(t.font.caption->lineHeight)}, t.dp(3), parts);
    const Rect sw = parts[0];
    c.fillRoundRect(sw.offset(0, t.dp(2)), t.m.radiusControl, t.color.shadow, t.color.shadowAlpha);
    for (int y = 0; y < sw.h; ++y) {
      const int k = sw.h > 1 ? y * 255 / (sw.h - 1) : 0;
      // Each phase's own gradient, ending in its tint, so they read apart.
      // (Dark skies only borrow a little of the light tint.)
      const int tintMax = t.color.dark ? 90 : 254;
      const Color row = k < 128 ? blend(preview.color.sky[0], preview.color.sky[2], (uint8_t)(k * 2))
                                : blend(preview.color.sky[2], preview.color.skyTint, (uint8_t)((k - 128) * tintMax / 127));
      const Rect line(sw.x, sw.y + y, sw.w, 1);
      const Rect prev = c.pushClip(line);
      c.fillRoundRect(sw, t.m.radiusControl, row);
      c.restoreClip(prev);
    }
    const bool pinned = skyPin_ == i + 1;
    if (pinned) c.strokeRoundRect(sw.inset(-t.dp(2)), t.m.radiusControl + t.dp(2), t.dp(2), t.color.accent);
    else c.strokeRoundRect(sw, t.m.radiusControl, 1, t.color.rim, t.color.rimAlpha);
    if (pressedSky_ == i) c.fillRoundRect(sw, t.m.radiusControl, t.color.accent, 46);
    const bool current = (SkyPhase)i == t.phase;
    c.drawTextIn(*t.font.caption, phaseName((SkyPhase)i), parts[1], current ? t.color.accent : t.color.textMuted,
                 Align::Center);
  }
  char note[64];
  if (skyPin_) snprintf(note, sizeof(note), "Keeping the %s sky \xc2\xb7 tap it again to follow the time",
                        phaseName((SkyPhase)(skyPin_ - 1)));
  else snprintf(note, sizeof(note), "The sky follows the time \xc2\xb7 tap one to keep it");
  c.drawTextIn(*t.font.caption, note, skyNote_, t.color.textMuted, Align::Center);
}

bool SettingsApp::AppearanceScreen::onEvent(const Event &e, AppContext &ctx) {
  const Theme &t = ctx.theme();
  for (int i = 0; i < 4; ++i) {
    const Rect cell = grid(skies_, 4, 1, i, t.m.gapS);
    bool pressed = pressedSky_ == i;
    const bool tapped = trackPress(cell, e, pressed, ctx, skies_.inset(-t.dp(4)));
    if (pressed != (pressedSky_ == i)) pressedSky_ = pressed ? i : -1;
    if (!tapped) continue;
    SystemSettings s = ctx.settings();
    s.skyPin = skyPin_ == i + 1 ? 0 : (uint8_t)(i + 1);
    skyPin_ = s.skyPin;
    ctx.applySettings(s);  // re-themes when the sky changes
    ctx.invalidate(skies_.inset(-t.dp(4)).unite(skyNote_));
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
  zones_.buttonWidth = t.dp(32);
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
  list_.buttonWidth = t.dp(32);
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
  // Last frame's render time (not a frame count: that would change on every
  // repaint of this very screen and keep it redrawing).
  snprintf(next[8], 40, "%lu ms to render", (unsigned long)s.lastFrameMs);
  if (s.uiStackFree || s.serviceStackFree)
    snprintf(next[9], 40, "ui %lu B \xc2\xb7 svc %lu B", (unsigned long)s.uiStackFree,
             (unsigned long)s.serviceStackFree);
  else
    snprintf(next[9], 40, "Not measured");
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
  static const char *const kLabels[kRows] = {"Board",      "Software",   "Display", "Uptime",  "Memory",
                                             "Lowest free", "Last reset", "Network", "Frame",  "Stack free"};
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
