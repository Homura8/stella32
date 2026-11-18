#include "settings_app.h"

#include <stdio.h>
#include <string.h>

#include "changelog.h"
#include "../core/deadline.h"
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

namespace {

const char *const kTempChoices[] = {"\xc2\xb0""C", "\xc2\xb0""F"};
const char *const kWeekChoices[] = {"Monday", "Sunday"};
const char *const kDateChoices[] = {"17 Nov", "Nov 17"};
const ChoiceRow kUnitsRows[] = {
    {"Temperature", kTempChoices, 2}, {"Week starts", kWeekChoices, 2}, {"Date", kDateChoices, 2}};
int unitsGet(const SystemSettings &s, int row) {
  return row == 0 ? s.fahrenheit : row == 1 ? s.weekStartsSunday : s.dateOrder;
}
void unitsSet(SystemSettings &s, int row, int v) {
  if (row == 0) s.fahrenheit = v != 0;
  else if (row == 1) s.weekStartsSunday = v != 0;
  else s.dateOrder = (uint8_t)v;
}

const char *const kStartChoices[] = {"Never", "At night", "Always"};
const char *const kFaceChoices[] = {"Clock", "Dashboard"};
const ChoiceRow kIdleRows[] = {{"Show", kStartChoices, 3}, {"Style", kFaceChoices, 2}};
int idleGet(const SystemSettings &s, int row) { return row == 0 ? s.bedside : s.idleFace; }
void idleSet(SystemSettings &s, int row, int v) {
  if (row == 0) s.bedside = (uint8_t)v;
  else s.idleFace = (uint8_t)v;
}

}  // namespace

SettingsApp::SettingsApp() : main_(*this), wifi_(*this), setup_(*this), display_(*this), about_(*this) {
  units_.init("Units", kUnitsRows, 3, unitsGet, unitsSet);
  idle_.init("Screensaver", kIdleRows, 2, idleGet, idleSet,
             "After the screen dims (Settings, Display), a calm full-screen clock or dashboard takes over. Tap to leave it.");
}

const AppInfo &SettingsApp::info() const {
  static const AppInfo kInfo{"settings", "Settings", icons::gear, 1, 0};
  return kInfo;
}

void SettingsApp::onTopic(uint16_t topicId, AppContext &ctx) {
  if (topicId == topics::kNetwork || topicId == topics::kWifiScan) wifi_.refresh(ctx);
  if (topicId == topics::kNetwork && setup_.isVisible()) setup_.refresh(ctx);
  if (topicId == topics::kRemote && remote_.isVisible()) remote_.refresh(ctx);
  if ((topicId == topics::kUpdate || topicId == topics::kNetwork) && softwareUpdate_.isVisible())
    softwareUpdate_.refresh(ctx);
  if (topicId == topics::kUpdate && main_.isVisible()) {
    main_.onShow(ctx);  // the About row's "Update 0.6.1"
    ctx.invalidate(ctx.contentRect());
  }
}

void SettingsApp::onRoute(const char *route, AppContext &ctx) {
  if (route && strcmp(route, "wifi") == 0) ctx.push(wifi_);
  if (route && strcmp(route, "time") == 0) ctx.push(time_);
  if (route && strcmp(route, "whatsnew") == 0) ctx.push(whatsNew_);
  if (route && strcmp(route, "remote") == 0) ctx.push(remote_);
  if (route && strcmp(route, "units") == 0) ctx.push(units_);
  if (route && strcmp(route, "idle") == 0) ctx.push(idle_);
  if (route && strcmp(route, "calendar") == 0) ctx.push(calendar_);
}

// ------------------------------------------------------------------ main list

void SettingsApp::MainScreen::layout(const Rect &content, const Theme &t) {
  const Rect area = content.inset(t.m.margin, t.m.gapS);
  // All rows when they fit; else as tall as the touch minimum and scrolled
  // (with the up and down buttons).
  const int minimum = t.dp(32);
  int rowH = kRows * t.m.row <= area.h ? t.m.row : area.h / kRows;
  if (rowH < minimum) rowH = minimum;
  const int height = kRows * rowH < area.h ? kRows * rowH : area.h;
  card_ = Rect(area.x, area.y, area.w, height);
  list_.bounds = card_;
  list_.rowHeight = rowH;
  list_.count = kRows;
  list_.buttonWidth = t.dp(32);
}

void SettingsApp::MainScreen::onShow(AppContext &ctx) {
  const SystemSettings &s = ctx.settings();
  snprintf(appearance_, sizeof(appearance_), "%s", kAppearanceLabels[(int)s.appearance]);
  // "24 h · Melbourne": the first city of the zone's label, so the row never gets cut off.
  char city[24];
  copyText(city, sizeof(city), zoneLabel(s.timezone));
  if (char *slash = strstr(city, " / ")) *slash = '\0';
  snprintf(time_, sizeof(time_), "%s \xc2\xb7 %s", s.clock24h ? "24 h" : "12 h", city);
  if (s.volume == 0) snprintf(sound_, sizeof(sound_), "Off");
  else snprintf(sound_, sizeof(sound_), "%u%%", (unsigned)s.volume);
  // An offered or installed update shows here, so it is seen without digging.
  const UpdateStatus update = ctx.update().status();
  about_[0] = '\0';
  if (update.state == UpdateState::Available && update.version[0])
    snprintf(about_, sizeof(about_), "Update %.12s", update.version);
  else if (update.state == UpdateState::Ready)
    snprintf(about_, sizeof(about_), "Restart to update");
  snprintf(unitsDetail_, sizeof(unitsDetail_), "%s", s.fahrenheit ? "\xc2\xb0""F" : "\xc2\xb0""C");
  snprintf(idleDetail_, sizeof(idleDetail_), "%s", s.bedside == 0 ? "Off" : s.idleFace == 1 ? "Dashboard" : "Clock");
  snprintf(calendarDetail_, sizeof(calendarDetail_), "%s", ctx.calendar().hasAddress() ? "On" : "Not set");
  const RemoteInfo remote = ctx.remote().status();
  snprintf(remoteDetail_, sizeof(remoteDetail_), "%s", remote.running ? "On" : remote.enabled ? "Waiting" : "Off");
  const NetworkStatus net = ctx.network().status();
  snprintf(wifi_, sizeof(wifi_), "%s",
           !net.present                              ? "Not available"
           : net.state == WifiState::Connected       ? net.ssid
           : net.state == WifiState::Connecting      ? "Joining..."
           : net.state == WifiState::NotConfigured   ? "Off"
                                                     : "Not connected");
}

void SettingsApp::MainScreen::drawRow(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed, void *self) {
  const MainScreen &s = *static_cast<const MainScreen *>(self);
  const struct {
    const char *title;
    IconFn icon;
    uint8_t tint;
    const char *detail;
  } rows[kRows] = {{"Wi-Fi", icons::wifi, 2, s.wifi_},
                   {"Display", icons::sun, 3, nullptr},
                   {"Sound & light", icons::bell, 1, s.sound_},
                   {"Appearance", icons::appearance, 1, s.appearance_},
                   {"Time", icons::clock, 2, s.time_},
                   {"About", icons::info, 0, s.about_},
                   {"Home screen", icons::home, 3, nullptr},
                   {"Units", icons::gear, 0, s.unitsDetail_},
                   {"Screensaver", icons::moon, 1, s.idleDetail_},
                   {"Calendar", icons::calendar, 2, s.calendarDetail_},
                   {"Web page", icons::wifi, 2, s.remoteDetail_}};
  ListRow r;
  r.bounds = row;
  r.title = rows[index].title;
  r.icon = rows[index].icon;
  r.tint = rows[index].tint;
  r.detail = rows[index].detail;
  r.separator = index < kRows - 1;
  r.pressed = pressed;
  r.draw(c, t);
}

void SettingsApp::MainScreen::draw(Canvas &c, const Theme &t) {
  paintGlass(c, t, card_, t.m.radiusCard);
  list_.draw(c, t, drawRow, this);
}

bool SettingsApp::MainScreen::onEvent(const Event &e, AppContext &ctx) {
  const int index = list_.handle(e, ctx);
  if (index < 0) return false;
  Screen *targets[kRows] = {&app_.wifi_, &app_.display_, &app_.sound_, &app_.appearance_, &app_.time_, &app_.about_,
                            nullptr, &app_.units_, &app_.idle_, &app_.calendar_, &app_.remote_};
  if (targets[index]) ctx.push(*targets[index]);
  else ctx.launch("home", "customise");
  return true;
}

// ------------------------------------------------------------------ sound and light

namespace {
int cardRowHeight(const Theme &t, int available, int rows);
const char *const kLedModeLabels[] = {"Off", "Alerts", "Glow"};
}  // namespace

void SettingsApp::SoundScreen::layout(const Rect &content, const Theme &t) {
  const Rect area = content.inset(t.m.margin, t.m.gapS);
  const int rowH = cardRowHeight(t, area.h, kRows);
  card_ = Rect(area.x, area.y, area.w, kRows * rowH + 2 * t.dp(4));
  const Rect inner = card_.inset(t.m.padding, t.dp(4));
  Rect control[kRows];
  for (int i = 0; i < kRows; ++i) {
    rows_[i] = Rect(inner.x, inner.y + i * rowH, inner.w, rowH);
    Rect parts[2];
    row(rows_[i], {px(t.dp(96)), flex()}, t.m.gapM, parts);
    labels_[i] = parts[0];
    control[i] = parts[1].inset(0, (rowH - t.m.control) / 2 > 0 ? (rowH - t.m.control) / 2 : 0);
  }
  volume_.bounds = control[0];
  ledMode_.bounds = control[1].inset(0, t.dp(2));
  ledLevel_.bounds = control[2];
  findLed_.bounds = Rect(card_.x, rows_[3].y, card_.w, rows_[3].h);
  findLed_.title = "Find my LED";
  findLed_.chevron = true;
  autoBrightness_.bounds = rows_[4];
  lightSensor_.bounds = Rect(card_.x, rows_[5].y, card_.w, rows_[5].h);
  lightSensor_.title = "Light sensor";
  lightSensor_.chevron = true;
}

void SettingsApp::SoundScreen::onShow(AppContext &ctx) {
  const SystemSettings &s = ctx.settings();
  const SystemStatus st = ctx.status();
  volume_.minimum = 0;
  volume_.maximum = 100;
  volume_.value = s.volume;
  ledMode_.labels = kLedModeLabels;
  ledMode_.count = 3;
  ledMode_.selected = s.ledMode;
  ledLevel_.minimum = 5;
  ledLevel_.maximum = 100;
  ledLevel_.value = s.ledLevel;
  autoBrightness_.on = s.autoBrightness;
  snprintf(ledDetail_, sizeof(ledDetail_), "%s",
           !st.hasLed ? "No LED" : (s.ledPin[0] != 255 || s.ledPin[1] != 255 || s.ledPin[2] != 255) ? "Set" : "Not set");
  snprintf(lightDetail_, sizeof(lightDetail_), "%s", !st.hasLight ? "No sensor" : s.lightCalibrated() ? "Calibrated" : "Not set");
  findLed_.detail = ledDetail_;
  lightSensor_.detail = lightDetail_;
  changed_ = false;
}

void SettingsApp::SoundScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  paintGlass(c, t, card_, t.m.radiusCard);
  static const char *const kLabels[kRows] = {"Volume", "LED", "LED level", nullptr, "Auto dim", nullptr};
  for (int i = 0; i < kRows; ++i) {
    if (kLabels[i]) c.drawTextIn(*t.font.body, kLabels[i], labels_[i], p.text);
    if (i < kRows - 1) paintHairline(c, t, rows_[i].x, rows_[i].bottom() - 1, rows_[i].w);
  }
  volume_.draw(c, t);
  ledMode_.draw(c, t);
  ledLevel_.draw(c, t);
  findLed_.draw(c, t);
  autoBrightness_.draw(c, t);
  lightSensor_.draw(c, t);
}

bool SettingsApp::SoundScreen::onEvent(const Event &e, AppContext &ctx) {
  SystemSettings s = ctx.settings();
  if (volume_.handle(e, ctx, ctx.theme())) {
    s.volume = (uint8_t)volume_.value;
    ctx.applySettings(s);
    changed_ = true;
    return true;
  }
  if (volume_.dragging) return true;
  if (ledLevel_.handle(e, ctx, ctx.theme())) {
    s.ledLevel = (uint8_t)ledLevel_.value;
    ctx.applySettings(s);
    changed_ = true;
    return true;
  }
  if (ledLevel_.dragging) return true;
  if (e.type == EventType::Release && changed_) {
    changed_ = false;
    ctx.previewChime();                  // the volume, if that is what moved
    ctx.flashLed(255, 255, 255, 600);    // the LED level, if that is what moved
  }
  if (ledMode_.handle(e, ctx)) {
    s.ledMode = ledMode_.selected;
    ctx.applySettings(s);
    if (s.ledMode) ctx.flashLed(255, 255, 255, 600);
    return true;
  }
  if (autoBrightness_.handle(e, ctx)) {
    if (autoBrightness_.on && (!ctx.status().hasLight || !s.lightCalibrated())) {
      autoBrightness_.on = false;
      ctx.invalidate(autoBrightness_.bounds);
      ctx.toast(ctx.status().hasLight ? "Calibrate the light sensor first" : "This board has no light sensor");
      return true;
    }
    s.autoBrightness = autoBrightness_.on;
    ctx.applySettings(s);
    return true;
  }
  if (findLed_.handle(e, ctx)) {
    if (ctx.status().hasLed) ctx.push(app_.ledFind_);
    else ctx.toast("This board has no LED");
    return true;
  }
  if (lightSensor_.handle(e, ctx)) {
    if (ctx.status().hasLight) ctx.push(app_.light_);
    else ctx.toast("This board has no light sensor");
    return true;
  }
  return false;
}

// ------------------------------------------------------------------ find my LED

namespace {
void arrangeButtons(Button *buttons, int count, const Rect &rowRect, int gap) {
  if (count <= 0) return;
  const int w = (rowRect.w - gap * (count - 1)) / count;
  for (int i = 0; i < count; ++i) buttons[i].bounds = Rect(rowRect.x + i * (w + gap), rowRect.y, i == count - 1 ? rowRect.right() - (rowRect.x + i * (w + gap)) : w, rowRect.h);
}
}  // namespace

void SettingsApp::LedFindScreen::layout(const Rect &content, const Theme &t) {
  Rect r[2];
  column(content.inset(t.m.margin, t.m.gapS), {flex(), px(t.m.control)}, t.m.gapM, r);
  body_ = r[0];
  arrangeButtons(buttons_, buttonCount_ ? buttonCount_ : 1, r[1], t.m.gapS);
  buttonRow_ = r[1];
  buttonGap_ = t.m.gapS;
}

void SettingsApp::LedFindScreen::setPhase(Phase p, AppContext &ctx) {
  phase_ = p;
  switch (p) {
    case Phase::Intro:
      snprintf(text_, sizeof(text_),
               "Look at the back of the board. I'll light each pin the LED might be on, one at a time. Tell me what colour you see. About 15 seconds.");
      buttons_[0].label = "Start";
      buttons_[0].style = ButtonStyle::Accent;
      buttonCount_ = 1;
      break;
    case Phase::Asking:
      snprintf(text_, sizeof(text_), "Pin %d of %d: what colour is lit?", index_ + 1, pinCount_);
      buttons_[0].label = "Red";
      buttons_[1].label = "Green";
      buttons_[2].label = "Blue";
      buttons_[3].label = "None";
      for (int i = 0; i < 4; ++i) buttons_[i].style = ButtonStyle::Glass;
      buttonCount_ = 4;
      break;
    case Phase::Found: {
      static const char *const kNames[3] = {"red", "green", "blue"};
      char list[80] = "";
      for (int i = 0; i < 3; ++i) {
        if (found_.pin[i] == kNoPin) continue;
        char one[24];
        snprintf(one, sizeof(one), "%s%s %u", list[0] ? ", " : "", kNames[i], (unsigned)found_.pin[i]);
        strncat(list, one, sizeof(list) - strlen(list) - 1);
      }
      snprintf(text_, sizeof(text_), "Found it: %s. It lights when the pin is %s.", list, found_.inverted ? "low" : "high");
      buttons_[0].label = "Save";
      buttons_[0].style = ButtonStyle::Accent;
      buttons_[1].label = "Try again";
      buttons_[1].style = ButtonStyle::Glass;
      buttonCount_ = 2;
      break;
    }
    case Phase::NotFound:
      snprintf(text_, sizeof(text_),
               "No light seen. This board may have no RGB LED, or it is on pins I don't try (I only touch pins that cannot hurt the display or touch).");
      buttons_[0].label = "Try again";
      buttons_[0].style = ButtonStyle::Glass;
      buttonCount_ = 1;
      break;
  }
  if (buttonRow_.w) arrangeButtons(buttons_, buttonCount_, buttonRow_, buttonGap_);
  ctx.invalidate(ctx.contentRect());
}

void SettingsApp::LedFindScreen::onShow(AppContext &ctx) {
  found_ = LedWiring{};
  setPhase(Phase::Intro, ctx);
}

void SettingsApp::LedFindScreen::onHide(AppContext &ctx) { ctx.ledProbe(kNoPin, true); }

void SettingsApp::LedFindScreen::probe(AppContext &ctx) {
  ctx.ledProbe(pins_[index_], activeLow_);
  setPhase(Phase::Asking, ctx);
}

void SettingsApp::LedFindScreen::answer(int colour, AppContext &ctx) {
  auto advance = [&]() {
    ++index_;
    activeLow_ = polarityKnown_ ? found_.inverted : true;
    if (index_ >= pinCount_) {
      ctx.ledProbe(kNoPin, true);
      setPhase(found_.usable() ? Phase::Found : Phase::NotFound, ctx);
    } else {
      probe(ctx);
    }
  };
  if (colour >= 0) {
    found_.pin[colour] = pins_[index_];
    if (!polarityKnown_) {
      polarityKnown_ = true;
      found_.inverted = activeLow_;
    }
    advance();
  } else if (!polarityKnown_ && activeLow_) {
    activeLow_ = false;  // nothing when driven low: maybe it lights when high
    probe(ctx);
  } else {
    advance();
  }
}

void SettingsApp::LedFindScreen::draw(Canvas &c, const Theme &t) {
  paintGlass(c, t, body_, t.m.radiusCard);
  c.drawWrapped(*t.font.body, text_, body_.inset(t.m.padding, t.m.padding), t.color.text, 7, Align::Center);
  for (int i = 0; i < buttonCount_; ++i) buttons_[i].draw(c, t);
}

bool SettingsApp::LedFindScreen::onEvent(const Event &e, AppContext &ctx) {
  for (int i = 0; i < buttonCount_; ++i) {
    if (!buttons_[i].handle(e, ctx)) continue;
    switch (phase_) {
      case Phase::Intro:
      case Phase::NotFound:
        pinCount_ = ctx.ledCandidates(pins_, sizeof(pins_));
        index_ = 0;
        activeLow_ = true;
        polarityKnown_ = false;
        found_ = LedWiring{};
        if (pinCount_ == 0) setPhase(Phase::NotFound, ctx);
        else probe(ctx);
        break;
      case Phase::Asking: answer(i < 3 ? i : -1, ctx); break;
      case Phase::Found:
        if (i == 0) {
          SystemSettings s = ctx.settings();
          for (int k = 0; k < 3; ++k) s.ledPin[k] = found_.pin[k];
          s.ledInverted = found_.inverted;
          if (s.ledMode == 0) s.ledMode = 1;
          ctx.applySettings(s);
          ctx.flashLed(255, 255, 255, 700);
          ctx.toast("LED saved");
          ctx.pop();
        } else {
          setPhase(Phase::Intro, ctx);
        }
        break;
    }
    return true;
  }
  return false;
}

// ------------------------------------------------------------------ light sensor

void SettingsApp::LightCalibrateScreen::layout(const Rect &content, const Theme &t) {
  Rect r[2];
  column(content.inset(t.m.margin, t.m.gapS), {flex(), px(t.m.control)}, t.m.gapM, r);
  body_ = r[0];
  buttonRow_ = r[1];
  buttonGap_ = t.m.gapS;
  arrangeButtons(buttons_, buttonCount_ ? buttonCount_ : 1, r[1], t.m.gapS);
  bar_ = Rect(body_.x + t.m.padding, body_.bottom() - t.m.padding - t.dp(6), body_.w - 2 * t.m.padding, t.dp(6));
}

void SettingsApp::LightCalibrateScreen::setPhase(Phase p, AppContext &ctx) {
  phase_ = p;
  buttonCount_ = 1;
  buttons_[0].style = ButtonStyle::Accent;
  switch (p) {
    case Phase::Intro:
      snprintf(text_, sizeof(text_),
               "Put the clock where it will live, in its case. Turn the room lights off, or cover the sensor, then tap Start. The screen goes dark for about 6 seconds.");
      buttons_[0].label = "Start";
      break;
    case Phase::MeasuringDark:
      snprintf(text_, sizeof(text_), "Measuring in the dark. Keep it dark and still.");
      buttons_[0].label = "Working";
      buttons_[0].enabled = false;
      break;
    case Phase::AskBright:
      snprintf(text_, sizeof(text_), "Now turn the lights on as bright as you'd normally have them by day, then tap Measure.");
      buttons_[0].label = "Measure";
      buttons_[0].enabled = true;
      break;
    case Phase::MeasuringBright:
      snprintf(text_, sizeof(text_), "Measuring in the light. Keep it lit and still.");
      buttons_[0].label = "Working";
      buttons_[0].enabled = false;
      break;
    case Phase::Done:
      snprintf(text_, sizeof(text_), "Done. The screen will now dim in a dark room and brighten in the light. Auto dim is on.");
      buttons_[0].label = "Close";
      buttons_[0].enabled = true;
      break;
    case Phase::Failed:
      snprintf(text_, sizeof(text_),
               "The sensor barely changed between dark and light. Is it covered by the case, or was the room not bright enough? Try again.");
      buttons_[0].label = "Try again";
      buttons_[0].style = ButtonStyle::Glass;
      buttons_[0].enabled = true;
      break;
  }
  if (buttonRow_.w) arrangeButtons(buttons_, buttonCount_, buttonRow_, buttonGap_);
  ctx.invalidate(ctx.contentRect());
}

void SettingsApp::LightCalibrateScreen::onShow(AppContext &ctx) { setPhase(Phase::Intro, ctx); }

void SettingsApp::LightCalibrateScreen::onHide(AppContext &ctx) { ctx.holdBacklight(-1); }

// Each measurement: hold the backlight at a duty, wait for the sensor and the
// eye to settle, then average readings for a while.
uint32_t SettingsApp::LightCalibrateScreen::onTick(AppContext &ctx) {
  if (phase_ != Phase::MeasuringDark && phase_ != Phase::MeasuringBright) return kNoTick;
  constexpr uint32_t kSettleMs = 700, kSampleMs = 600, kSampleEvery = 50;
  const uint32_t now = ctx.millis();
  const uint32_t t = elapsedMs(now, stepStart_);
  if (t >= kSettleMs && reached(now, nextSample_)) {
    sum_ += ctx.lightReading();
    ++samples_;
    nextSample_ = now + kSampleEvery;
  }
  // Progress, for the bar: the whole run is 5 measurements.
  const int done = phase_ == Phase::MeasuringDark ? step_ : kDarkSteps;
  const int whole = (done * (kSettleMs + kSampleMs) + (t < kSettleMs + kSampleMs ? t : kSettleMs + kSampleMs)) * 100 /
                    ((kDarkSteps + 1) * (kSettleMs + kSampleMs));
  if (whole != progress_) {
    progress_ = whole;
    ctx.invalidate(bar_.inset(-2));
  }
  if (t < kSettleMs + kSampleMs) return kSampleEvery / 2;
  const float mean = samples_ ? (float)sum_ / samples_ : 0.f;
  sum_ = 0;
  samples_ = 0;
  if (phase_ == Phase::MeasuringDark) {
    static const int kDuties[kDarkSteps] = {0, 64, 160, 255};
    duty_[step_] = (float)kDuties[step_];
    reading_[step_] = mean;
    if (++step_ < kDarkSteps) {
      ctx.holdBacklight(kDuties[step_]);
      stepStart_ = ctx.millis();
      nextSample_ = stepStart_ + kSettleMs;
      return kSampleEvery;
    }
    ctx.holdBacklight(-1);
    setPhase(Phase::AskBright, ctx);
    return kNoTick;
  }
  brightRaw_ = mean;
  ctx.holdBacklight(-1);
  finish(ctx);
  return kNoTick;
}

void SettingsApp::LightCalibrateScreen::finish(AppContext &ctx) {
  // Least squares: reading = dark + self * (duty / 255).
  float mx = 0, my = 0;
  for (int i = 0; i < kDarkSteps; ++i) {
    mx += duty_[i] / 255.f;
    my += reading_[i];
  }
  mx /= kDarkSteps;
  my /= kDarkSteps;
  float sxx = 0, sxy = 0;
  for (int i = 0; i < kDarkSteps; ++i) {
    const float dx = duty_[i] / 255.f - mx;
    sxx += dx * dx;
    sxy += dx * (reading_[i] - my);
  }
  const float self = sxx > 0 ? sxy / sxx : 0.f;
  const float dark = my - self * mx;
  const float bright = brightRaw_ - self * (kBrightDuty / 255.f);  // with the screen's own light taken out
  const float range = bright > dark ? bright - dark : dark - bright;
  const bool sane = dark >= 0 && dark <= 4095 && bright >= 0 && bright <= 4095 && range >= (float)kMinLightRange;
  if (!sane) {
    setPhase(Phase::Failed, ctx);
    return;
  }
  SystemSettings s = ctx.settings();
  s.lightDark = (uint16_t)(dark + 0.5f);
  s.lightBright = (uint16_t)(bright + 0.5f);
  s.lightSelf = (int16_t)(self < -32000 ? -32000 : self > 32000 ? 32000 : self + (self < 0 ? -0.5f : 0.5f));
  s.autoBrightness = true;
  ctx.applySettings(s);
  setPhase(Phase::Done, ctx);
}

void SettingsApp::LightCalibrateScreen::draw(Canvas &c, const Theme &t) {
  paintGlass(c, t, body_, t.m.radiusCard);
  c.drawWrapped(*t.font.body, text_, body_.inset(t.m.padding, t.m.padding), t.color.text, 7, Align::Center);
  if (phase_ == Phase::MeasuringDark || phase_ == Phase::MeasuringBright) {
    c.fillRoundRect(bar_, bar_.h / 2, t.color.line, (uint8_t)(t.color.lineAlpha * 2));
    const int w = bar_.w * progress_ / 100;
    if (w > 0) c.fillRoundRect(Rect(bar_.x, bar_.y, w > bar_.h ? w : bar_.h, bar_.h), bar_.h / 2, t.color.accent);
  }
  for (int i = 0; i < buttonCount_; ++i) buttons_[i].draw(c, t);
}

bool SettingsApp::LightCalibrateScreen::onEvent(const Event &e, AppContext &ctx) {
  if (!buttons_[0].handle(e, ctx)) return false;
  switch (phase_) {
    case Phase::Intro:
    case Phase::Failed:
      step_ = 0;
      sum_ = 0;
      samples_ = 0;
      progress_ = 0;
      setPhase(Phase::MeasuringDark, ctx);
      ctx.holdBacklight(0);
      stepStart_ = ctx.millis();
      nextSample_ = stepStart_ + 700;
      ctx.requestTick();
      break;
    case Phase::AskBright:
      sum_ = 0;
      samples_ = 0;
      progress_ = kDarkSteps * 100 / (kDarkSteps + 1);
      setPhase(Phase::MeasuringBright, ctx);
      ctx.holdBacklight(kBrightDuty);
      stepStart_ = ctx.millis();
      nextSample_ = stepStart_ + 700;
      ctx.requestTick();
      break;
    case Phase::Done: ctx.pop(); break;
    default: break;
  }
  return true;
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
  Rect h[3];
  row(r[1], {flex(), px(t.dp(64)), px(t.dp(64))}, t.m.gapS, h);
  phone_.bounds = h[1];
  phone_.label = "Phone";
  phone_.style = ButtonStyle::Quiet;
  scan_.bounds = h[2];
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
  phone_.enabled = net_.present;
  phone_.draw(c, t);
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
  if (phone_.enabled && phone_.handle(e, ctx)) {
    ctx.push(app_.setup_);
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

// ------------------------------------------------------------------ phone setup

void SettingsApp::SetupScreen::layout(const Rect &content, const Theme &t) {
  const Rect area = content.inset(t.m.margin, t.m.gapS);
  const int rows = t.font.title->lineHeight + t.font.headline->lineHeight + t.font.title->lineHeight + 2 * t.dp(4);
  Rect r[3];
  column(area, {px(rows + 2 * t.dp(8)), flex(), px(t.m.control)}, t.m.gapM, r);
  card_ = r[0];
  status_ = r[1];
  cancel_.bounds = layout::center(r[2], t.dp(120), t.m.control);
  cancel_.label = "Cancel";
  Rect lines[3];
  column(r[0].inset(t.m.padding, t.dp(8)),
         {px(t.font.title->lineHeight), px(t.font.headline->lineHeight), px(t.font.title->lineHeight)}, t.dp(4), lines);
  for (int i = 0; i < 3; ++i) {
    Rect parts[2];
    row(lines[i], {px(t.dp(78)), flex()}, t.m.gapM, parts);
    labels_[i] = parts[0];
    values_[i] = parts[1];
  }
}

void SettingsApp::SetupScreen::onShow(AppContext &ctx) {
  info_ = SetupInfo{};
  sawSetup_ = false;
  ctx.network().beginSetup();
  refresh(ctx);
}

void SettingsApp::SetupScreen::onHide(AppContext &ctx) {
  ctx.network().cancelSetup();
  // The password is a temporary secret: do not leave a copy behind.
  volatile char *p = info_.password;
  for (size_t i = 0; i < sizeof(info_.password); ++i) p[i] = 0;
  info_ = SetupInfo{};
}

uint32_t SettingsApp::SetupScreen::onTick(AppContext &ctx) {
  refresh(ctx);
  return 500;  // the access point comes up without a status change; check twice a second
}

void SettingsApp::SetupScreen::refresh(AppContext &ctx) {
  net_ = ctx.network().status();
  SetupInfo info;
  if (ctx.network().setupInfo(info)) info_ = info;
  else info_.up = false;
  if (net_.setup) sawSetup_ = true;
  if (sawSetup_ && !net_.setup) {
    // Closed: it worked (we are connected), it timed out, or it was cancelled.
    char message[48];
    if (net_.connected) snprintf(message, sizeof(message), "Connected to %.28s", net_.ssid);
    else snprintf(message, sizeof(message), "Setup closed");
    ctx.toast(message);
    ctx.pop();
    return;
  }
  char text[sizeof(statusText_)];
  if (net_.error[0] && !net_.setup) snprintf(text, sizeof(text), "%.60s", net_.error);
  else if (!info_.up) snprintf(text, sizeof(text), "Looking for networks\xe2\x80\xa6");
  else if (net_.state == WifiState::Connecting) snprintf(text, sizeof(text), "Connecting to %.32s\xe2\x80\xa6", net_.ssid);
  else if (net_.error[0]) snprintf(text, sizeof(text), "%.40s. Try again on your phone.", net_.error);
  else snprintf(text, sizeof(text), "Join this network on your phone; the page opens by itself.");
  if (strcmp(text, statusText_) != 0) {
    memcpy(statusText_, text, sizeof(text));
    ctx.invalidate(status_);
  }
  if (isVisible()) ctx.invalidate(card_.inset(-glassOutset(ctx.theme())));
}

void SettingsApp::SetupScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  paintGlass(c, t, card_, t.m.radiusCard);
  const char *labels[3] = {"Wi-Fi", "Password", "Page"};
  const char *values[3] = {info_.up ? info_.ssid : "\xe2\x80\xa6", info_.up ? info_.password : "\xe2\x80\xa6",
                           info_.up ? info_.address : "\xe2\x80\xa6"};
  const Font *fonts[3] = {t.font.title, t.font.headline, t.font.title};
  for (int i = 0; i < 3; ++i) {
    c.drawTextIn(*t.font.caption, labels[i], labels_[i], p.textMuted);
    c.drawTextIn(*fonts[i], values[i], values_[i], i == 1 ? p.accent : p.text);
  }
  const bool problem = net_.error[0] != '\0';
  c.drawWrapped(*t.font.body, statusText_, status_, problem ? p.danger : p.textMuted, 3, Align::Center);
  cancel_.draw(c, t);
}

bool SettingsApp::SetupScreen::onEvent(const Event &e, AppContext &ctx) {
  if (cancel_.handle(e, ctx)) {
    ctx.pop();  // onHide closes the access point
    return true;
  }
  return false;
}

// ------------------------------------------------------------------ display

namespace {

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
  }
  calibrate_.bounds = Rect(card_.x, rows_[4].y, card_.w, rows_[4].h);
  calibrate_.title = "Colours";
  touch_.bounds = Rect(card_.x, rows_[5].y, card_.w, rows_[5].h);
  touch_.title = "Touch";
  touch_.detail = "Test and calibrate";
  touch_.icon = nullptr;
  touch_.chevron = true;
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
  const LocalTime now = ctx.localTime();
  night_ = !now.valid || ctx.status().night;
  snprintf(calibrateDetail_, sizeof(calibrateDetail_), "%s \xc2\xb7 %u%%", toneName(s.colorTone),
           (unsigned)(ctx.theme().color.dark ? s.contrastDark : s.contrastLight));
  calibrate_.detail = calibrateDetail_;
}

void SettingsApp::DisplayScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  paintGlass(c, t, card_, t.m.radiusCard);
  static const char *const kLabels[kRows - 1] = {"Brightness", "Dim at night", "Dim when idle", "Screen off"};
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
  calibrate_.draw(c, t);
  touch_.draw(c, t);
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
  if (touch_.handle(e, ctx)) {
    ctx.launch("touch");  // the touch test, which also calibrates
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
  ctx.confirmTimeZone();  // the person has seen the time zone: Home stops asking
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

// ------------------------------------------------------------------ calendar

void SettingsApp::CalendarScreen::layout(const Rect &content, const Theme &t) {
  const Rect area = content.inset(t.m.margin, t.m.gapS);
  const int hintH = t.font.caption->lineHeight * 4 + t.dp(4);
  const int rowH = cardRowHeight(t, area.h - hintH - t.m.gapS, kRows);
  card_ = Rect(area.x, area.y, area.w, kRows * rowH + 2 * t.dp(4));
  const Rect inner = card_.inset(0, t.dp(4));
  for (int i = 0; i < kRows; ++i) rows_[i] = Rect(inner.x, inner.y + i * rowH, inner.w, rowH);
  address_.bounds = rows_[0];
  fetch_.bounds = rows_[1];
  remove_.bounds = rows_[2];
  hint_ = Rect(area.x, card_.bottom() + t.m.gapS, area.w, hintH);
}

void SettingsApp::CalendarScreen::onShow(AppContext &ctx) {
  has_ = ctx.calendar().hasAddress();
  ctx.calendar().host(host_, sizeof(host_));
  address_.title = "Address";
  address_.detail = has_ ? host_ : "Not set";
  address_.icon = icons::calendar;
  address_.tint = 2;
  address_.separator = true;
  fetch_.title = "Fetch now";
  fetch_.icon = icons::cloud;
  fetch_.tint = 1;
  fetch_.chevron = false;
  fetch_.separator = true;
  remove_.title = "Remove address";
  remove_.icon = icons::lock;
  remove_.tint = 0;
  remove_.chevron = false;
}

void SettingsApp::CalendarScreen::draw(Canvas &c, const Theme &t) {
  paintGlass(c, t, card_, t.m.radiusCard);
  address_.draw(c, t);
  fetch_.draw(c, t);
  remove_.draw(c, t);
  c.drawWrapped(*t.font.caption,
                has_ ? "The address is private: it stays on this device and is never shown again. Change it on the web page."
                     : "Tap Address to open the web page, enter the PIN and paste your calendar's private iCal address.",
                hint_.inset(t.dp(4), 0), t.color.textMuted, 4, Align::Center);
}

bool SettingsApp::CalendarScreen::onEvent(const Event &e, AppContext &ctx) {
  if (address_.handle(e, ctx)) {
    ctx.push(app_.remote_);  // where the address is entered
    return true;
  }
  if (fetch_.handle(e, ctx)) {
    if (has_) {
      ctx.calendar().refreshNow();
      ctx.toast("Fetching your calendar");
    } else {
      ctx.toast("No address set yet");
    }
    return true;
  }
  if (remove_.handle(e, ctx)) {
    if (has_) {
      ctx.calendar().clearAddress();
      has_ = false;
      ctx.calendar().host(host_, sizeof(host_));
      address_.detail = "Not set";
      ctx.toast("Calendar address removed");
      ctx.invalidate(ctx.contentRect());
    }
    return true;
  }
  return false;
}

// ------------------------------------------------------------------ choices

void SettingsApp::ChoicesScreen::layout(const Rect &content, const Theme &t) {
  const Rect area = content.inset(t.m.margin, t.m.gapS);
  const int noteH = note_text_ ? 3 * t.font.caption->lineHeight + t.dp(4) + t.m.gapS : 0;
  const int rowH = cardRowHeight(t, area.h - noteH, count_);
  card_ = Rect(area.x, area.y, area.w, count_ * rowH + 2 * t.dp(4));
  note_ = note_text_ ? Rect(area.x, card_.bottom() + t.m.gapS, area.w, 3 * t.font.caption->lineHeight + t.dp(4)) : Rect();
  const Rect inner = card_.inset(t.m.padding, t.dp(4));
  for (int i = 0; i < count_ && i < kMax; ++i) {
    rowRects_[i] = Rect(inner.x, inner.y + i * rowH, inner.w, rowH);
    Rect parts[2];
    row(rowRects_[i], {px(t.dp(96)), flex()}, t.m.gapM, parts);
    labels_[i] = parts[0];
    const int pad = (rowH - t.m.control) / 2 > 0 ? (rowH - t.m.control) / 2 : 0;
    seg_[i].bounds = parts[1].inset(0, pad + t.dp(2));
  }
}

void SettingsApp::ChoicesScreen::onShow(AppContext &ctx) {
  for (int i = 0; i < count_ && i < kMax; ++i) {
    seg_[i].labels = rows_[i].choices;
    seg_[i].count = rows_[i].count;
    seg_[i].selected = (uint8_t)get_(ctx.settings(), i);
  }
}

void SettingsApp::ChoicesScreen::draw(Canvas &c, const Theme &t) {
  paintGlass(c, t, card_, t.m.radiusCard);
  for (int i = 0; i < count_ && i < kMax; ++i) {
    c.drawTextIn(*t.font.body, rows_[i].label, labels_[i], t.color.text);
    if (i < count_ - 1) paintHairline(c, t, rowRects_[i].x, rowRects_[i].bottom() - 1, rowRects_[i].w);
    seg_[i].draw(c, t);
  }
  if (note_text_) c.drawWrapped(*t.font.caption, note_text_, note_.inset(t.dp(4), 0), t.color.textMuted, 3, Align::Center);
}

bool SettingsApp::ChoicesScreen::onEvent(const Event &e, AppContext &ctx) {
  for (int i = 0; i < count_ && i < kMax; ++i) {
    if (seg_[i].handle(e, ctx)) {
      SystemSettings s = ctx.settings();
      set_(s, i, seg_[i].selected);
      ctx.applySettings(s);
      return true;
    }
  }
  return false;
}

// ------------------------------------------------------------------ remote page

void SettingsApp::RemoteScreen::layout(const Rect &content, const Theme &t) {
  const Rect area = content.inset(t.m.margin, t.m.gapS);
  const int hintH = t.font.caption->lineHeight * 2 + t.dp(4);
  const int rowH = cardRowHeight(t, area.h - hintH - t.m.gapS, kRows);
  card_ = Rect(area.x, area.y, area.w, kRows * rowH + 2 * t.dp(4));
  const Rect inner = card_.inset(t.m.padding, t.dp(4));
  for (int i = 0; i < kRows; ++i) {
    rows_[i] = Rect(inner.x, inner.y + i * rowH, inner.w, rowH);
    Rect parts[2];
    row(rows_[i], {px(t.dp(84)), flex()}, t.m.gapM, parts);
    labels_[i] = parts[0];
  }
  hint_ = Rect(area.x, card_.bottom() + t.m.gapS, area.w, area.bottom() - card_.bottom() - t.m.gapS);
  enabled_.bounds = rows_[0];
  const int bh = rowH - t.dp(4) > t.dp(28) ? rowH - t.dp(4) : t.dp(28);
  newPin_.bounds = Rect(rows_[2].right() - t.dp(84), rows_[2].y + (rowH - bh) / 2, t.dp(84), bh);
  newPin_.label = "Change";
  remove_.bounds = Rect(rows_[3].right() - t.dp(84), rows_[3].y + (rowH - bh) / 2, t.dp(84), bh);
  remove_.label = "Remove";
  Rect parts[2];
  row(rows_[4], {px(t.dp(84)), flex()}, t.m.gapM, parts);
  wash_.bounds = parts[1].inset(0, (rowH - t.m.control) / 2 > 0 ? (rowH - t.m.control) / 2 : 0);
}

void SettingsApp::RemoteScreen::onShow(AppContext &ctx) {
  wash_.minimum = 0;
  wash_.maximum = 90;
  refresh(ctx);
}

void SettingsApp::RemoteScreen::refresh(AppContext &ctx) {
  info_ = ctx.remote().status();
  if (!ctx.remote().pin(pin_)) pin_[0] = '\0';
  enabled_.on = info_.enabled;
  wash_.value = info_.veil;
  if (info_.running) {
    snprintf(host_, sizeof(host_), "%s.local", info_.host);
    snprintf(ip_, sizeof(ip_), "or %s", info_.ip);
  } else {
    snprintf(host_, sizeof(host_), "%s", info_.enabled ? "Waiting for Wi-Fi\xe2\x80\xa6" : "Off");
    ip_[0] = '\0';
  }
  snprintf(wallpaper_, sizeof(wallpaper_), "%s", !info_.wallpaperSupported ? "No room here" : info_.wallpaper ? "Set" : "None");
  remove_.enabled = info_.wallpaper;
  if (isVisible()) ctx.invalidate(ctx.contentRect());
}

void SettingsApp::RemoteScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  paintGlass(c, t, card_, t.m.radiusCard);
  static const char *const kLabels[kRows] = {"Enabled", "Address", "PIN", "Wallpaper", "Wash"};
  for (int i = 0; i < kRows; ++i) {
    c.drawTextIn(*t.font.body, kLabels[i], labels_[i], p.text);
    if (i < kRows - 1) paintHairline(c, t, rows_[i].x, rows_[i].bottom() - 1, rows_[i].w);
  }
  enabled_.draw(c, t);
  // The address: the name, and the IP below it.
  const Rect addr(rows_[1].x + t.dp(84) + t.m.gapM, rows_[1].y, rows_[1].w - t.dp(84) - t.m.gapM, rows_[1].h);
  if (ip_[0]) {
    Rect lines[2];
    column(center(addr, addr.w, t.font.body->lineHeight + t.font.caption->lineHeight),
           {px(t.font.body->lineHeight), px(t.font.caption->lineHeight)}, 0, lines);
    c.drawTextIn(*t.font.body, host_, lines[0], p.accent);
    c.drawTextIn(*t.font.caption, ip_, lines[1], p.textMuted);
  } else {
    c.drawTextIn(*t.font.body, host_, addr, p.textMuted);
  }
  const Rect pinArea(rows_[2].x + t.dp(84) + t.m.gapM, rows_[2].y, newPin_.bounds.x - rows_[2].x - t.dp(84) - 2 * t.m.gapM, rows_[2].h);
  c.drawTextIn(*t.font.title, info_.enabled ? pin_ : "\xe2\x80\x94", pinArea, info_.lockedOut ? p.danger : p.text);
  newPin_.draw(c, t);
  c.drawTextIn(*t.font.body, wallpaper_, Rect(rows_[3].x + t.dp(84) + t.m.gapM, rows_[3].y, remove_.bounds.x - rows_[3].x - t.dp(84) - 2 * t.m.gapM, rows_[3].h), p.textMuted);
  remove_.draw(c, t);
  wash_.draw(c, t);
  c.drawWrapped(*t.font.caption, "On a phone or PC on this Wi-Fi, open the address, enter the PIN and pick a photo.",
                hint_.inset(t.dp(4), 0), p.textMuted, 3, Align::Center);
}

bool SettingsApp::RemoteScreen::onEvent(const Event &e, AppContext &ctx) {
  if (enabled_.handle(e, ctx)) {
    ctx.remote().setEnabled(enabled_.on);
    return true;
  }
  if (newPin_.handle(e, ctx)) {
    ctx.remote().newPin();
    ctx.toast("PIN changed");
    return true;
  }
  if (remove_.handle(e, ctx)) {
    ctx.remote().removeWallpaper();
    return true;
  }
  if (wash_.handle(e, ctx, ctx.theme())) {
    ctx.remote().setVeil((uint8_t)wash_.value);
    return true;
  }
  return wash_.dragging;
}

// ------------------------------------------------------------------ what's new

void SettingsApp::WhatsNewScreen::layout(const Rect &content, const Theme &t) {
  card_ = content.inset(t.m.margin, t.m.gapS);
  list_.bounds = card_.inset(t.dp(2), t.dp(4));
  list_.rowHeight = t.dp(26);
  list_.buttonWidth = t.dp(32);
}

void SettingsApp::WhatsNewScreen::onShow(AppContext &) {
  int entries = 0;
  const ChangelogEntry *log = changelog(entries);
  int rows = 0;
  for (int i = 0; i < entries; ++i) {
    ++rows;  // the version
    for (const char *line : log[i].lines)
      if (line) ++rows;
  }
  list_.count = rows;
  list_.offset = 0;
}

// Row k is either a version heading or one of its lines.
void SettingsApp::WhatsNewScreen::drawRow(Canvas &c, const Theme &t, int index, const Rect &row, bool, void *) {
  int entries = 0;
  const ChangelogEntry *log = changelog(entries);
  int k = index;
  for (int i = 0; i < entries; ++i) {
    if (k == 0) {
      char heading[24];
      snprintf(heading, sizeof(heading), "StellaOS %s", log[i].version);
      c.drawTextIn(*t.font.title, heading, row.inset(t.m.padding, 0), i == 0 ? t.color.accent : t.color.text);
      return;
    }
    --k;
    for (const char *line : log[i].lines) {
      if (!line) continue;
      if (k == 0) {
        char text[64];
        snprintf(text, sizeof(text), "\xe2\x80\xa2 %s", line);
        c.drawTextIn(*t.font.body, text, row.inset(t.m.padding + t.dp(4), 0), t.color.textMuted);
        return;
      }
      --k;
    }
  }
}

void SettingsApp::WhatsNewScreen::draw(Canvas &c, const Theme &t) {
  paintGlass(c, t, card_, t.m.radiusCard);
  list_.draw(c, t, drawRow, this);
}

bool SettingsApp::WhatsNewScreen::onEvent(const Event &e, AppContext &ctx) {
  list_.handle(e, ctx);
  return true;
}

// ------------------------------------------------------------------ about

void SettingsApp::AboutScreen::layout(const Rect &content, const Theme &t) {
  Rect r[2];
  column(content.inset(t.m.margin, t.m.gapS), {flex(), px(t.m.control)}, t.m.gapS, r);
  card_ = r[0];
  Rect b[2];
  row(r[1], {flex(), flex()}, t.m.gapM, b);
  whatsNew_.bounds = b[0];
  whatsNew_.label = "What's new";
  update_.bounds = b[1];
  update_.label = "Software update";
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
  snprintf(next[1], 40, "StellaOS %s", s.version);
  snprintf(next[2], 40, "%d x %d %s", s.width, s.height, s.density == Density::Regular ? "regular" : "compact");
  snprintf(next[3], 40, "%lud %02luh %02lum", (unsigned long)(up / 86400), (unsigned long)(up / 3600 % 24),
           (unsigned long)(up / 60 % 60));
  snprintf(next[4], 40, "%lu KB free, %lu KB block", (unsigned long)(s.memory.freeHeap / 1024),
           (unsigned long)(s.memory.largestFreeBlock / 1024));
  snprintf(next[5], 40, "%lu KB", (unsigned long)(s.memory.minimumFreeHeap / 1024));
  snprintf(next[6], 40, "%s", resetName(s.resetCause));
  if (s.resetNote[0]) snprintf(next[6], 40, "%s \xc2\xb7 %.24s", resetName(s.resetCause), s.resetNote);
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
  whatsNew_.draw(c, t);
  update_.draw(c, t);
}

bool SettingsApp::AboutScreen::onEvent(const Event &e, AppContext &ctx) {
  if (update_.handle(e, ctx)) {
    ctx.push(app_.softwareUpdate_);
    return true;
  }
  if (whatsNew_.handle(e, ctx)) {
    ctx.push(app_.whatsNew_);
    return true;
  }
  list_.handle(e, ctx);
  return true;
}

// ------------------------------------------------------------------ software update

void SettingsApp::UpdateScreen::layout(const Rect &content, const Theme &t) {
  const int lh = t.font.body->lineHeight;
  const int bar = t.dp(6);
  const int toggleH = t.dp(32);
  Rect r[3];
  column(content.inset(t.m.margin, t.m.gapS),
         {px(2 * lh + toggleH + bar + t.dp(9) + 2 * t.m.padding), flex(), px(t.m.control)}, t.m.gapM, r);
  card_ = r[0];
  Rect inner[4];
  column(r[0].inset(t.m.padding, t.m.padding), {px(lh), px(lh), px(toggleH), px(bar)}, t.dp(3), inner);
  rows_[0] = inner[0];
  rows_[1] = inner[1];
  autoRow_ = inner[2];
  auto_.bounds = autoRow_;
  bar_ = inner[3];
  status_ = r[1];
  action_.bounds = layout::center(r[2], t.dp(200), t.m.control);
}

void SettingsApp::UpdateScreen::onShow(AppContext &ctx) { refresh(ctx); }

void SettingsApp::UpdateScreen::refresh(AppContext &ctx) {
  st_ = ctx.update().status();
  auto_.on = ctx.settings().autoUpdate;
  const NetworkStatus net = ctx.network().status();
  snprintf(installed_, sizeof(installed_), "StellaOS %s", ctx.status().version);
  if (st_.version[0] && (st_.state == UpdateState::Available || st_.state == UpdateState::Downloading ||
                         st_.state == UpdateState::Ready || st_.state == UpdateState::UpToDate))
    snprintf(offered_, sizeof(offered_), "%s \xc2\xb7 %lu.%lu MB", st_.version, (unsigned long)(st_.size / 1048576),
             (unsigned long)(st_.size % 1048576 * 10 / 1048576));
  else
    snprintf(offered_, sizeof(offered_), "\xe2\x80\x94");
  const char *label = "Check for updates";
  bool enabled = true;
  char text[sizeof(statusText_)];
  switch (st_.state) {
    case UpdateState::Unavailable:
      snprintf(text, sizeof(text), "%.60s", st_.error[0] ? st_.error : "Updates are not set up");
      enabled = false;
      break;
    case UpdateState::Idle:
      snprintf(text, sizeof(text), net.connected ? "Looks for a newer version of Stella." : "Connect to Wi-Fi to check for updates.");
      enabled = net.connected;
      break;
    case UpdateState::Checking:
      snprintf(text, sizeof(text), "Looking for updates\xe2\x80\xa6");
      label = "Checking";
      enabled = false;
      break;
    case UpdateState::UpToDate:
      snprintf(text, sizeof(text), "You have the latest version.");
      label = "Check again";
      break;
    case UpdateState::Available:
      snprintf(text, sizeof(text), "Version %.12s is ready. It is checked before it is installed; the device restarts to use it.",
               st_.version);
      label = "Install";
      break;
    case UpdateState::Downloading:
      snprintf(text, sizeof(text), "Downloading and checking\xe2\x80\xa6 %u%%", (unsigned)st_.percent);
      label = "Installing";
      enabled = false;
      break;
    case UpdateState::Ready:
      snprintf(text, sizeof(text), "Installed. Restart to run it; if it doesn't run well, the previous version returns.");
      label = "Restart now";
      break;
    default:
      snprintf(text, sizeof(text), "%.70s", st_.error[0] ? st_.error : "Something went wrong");
      label = "Try again";
      enabled = net.connected;
      break;
  }
  action_.label = label;
  action_.enabled = enabled;
  action_.style = st_.state == UpdateState::Available || st_.state == UpdateState::Ready ? ButtonStyle::Accent
                                                                                          : ButtonStyle::Glass;
  memcpy(statusText_, text, sizeof(text));
  if (isVisible()) ctx.invalidate(ctx.contentRect());
}

void SettingsApp::UpdateScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  paintGlass(c, t, card_, t.m.radiusCard);
  const char *labels[2] = {"Installed", "Available"};
  const char *values[2] = {installed_, offered_};
  for (int i = 0; i < 2; ++i) {
    Rect parts[2];
    row(rows_[i], {px(t.dp(80)), flex()}, t.m.gapS, parts);
    c.drawTextIn(*t.font.caption, labels[i], parts[0], p.textMuted);
    c.drawTextIn(*t.font.body, values[i], parts[1], p.text, Align::Right);
  }
  // Progress (only while it moves).
  c.drawTextIn(*t.font.body, "Check daily", autoRow_, p.text);
  auto_.draw(c, t);
  c.fillRoundRect(bar_, bar_.h / 2, p.line, (uint8_t)(p.lineAlpha * 2));
  if (st_.state == UpdateState::Downloading || st_.state == UpdateState::Ready) {
    const int w = bar_.w * (st_.state == UpdateState::Ready ? 100 : st_.percent) / 100;
    if (w > 0) c.fillRoundRect(Rect(bar_.x, bar_.y, w > bar_.h ? w : bar_.h, bar_.h), bar_.h / 2, p.accent);
  }
  c.drawWrapped(*t.font.body, statusText_, status_.inset(t.dp(4), t.dp(2)),
                st_.state == UpdateState::Failed ? p.danger : p.textMuted, 4, Align::Center);
  action_.draw(c, t);
}

bool SettingsApp::UpdateScreen::onEvent(const Event &e, AppContext &ctx) {
  if (auto_.handle(e, ctx)) {
    SystemSettings s = ctx.settings();
    s.autoUpdate = auto_.on;
    ctx.applySettings(s);
    return true;
  }
  if (!action_.handle(e, ctx)) return false;
  switch (st_.state) {
    case UpdateState::Idle:
    case UpdateState::UpToDate:
    case UpdateState::Failed:
      ctx.update().check();
      break;
    case UpdateState::Available:
      ctx.update().install();
      break;
    case UpdateState::Ready:
      ctx.toast("Restarting\xe2\x80\xa6");
      ctx.restartDevice();
      break;
    default:
      break;
  }
  return true;
}

}  // namespace apps
}  // namespace stella
