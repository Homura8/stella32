#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../core/deadline.h"
#include "../core/text.h"
#include "../core/log.h"
#include "../ui/layout.h"
#include "../ui/materials.h"

namespace stella {
namespace {

constexpr const char *kTag = "runtime";
constexpr uint32_t kToastMs = 2200;
constexpr uint32_t kSettingsDebounceMs = 1500;

// Alert chime: a soft rising C-E-G arpeggio, then a rest (Hz, ms).
struct ChimeNote {
  uint16_t hz, ms;
};
constexpr ChimeNote kChime[] = {{1047, 150}, {0, 70}, {1319, 150}, {0, 70}, {1568, 240}, {0, 1100}};
constexpr uint8_t kChimeNotes = sizeof(kChime) / sizeof(kChime[0]);
// The short chime Settings plays so a volume change can be heard.
constexpr ChimeNote kPreview[] = {{1319, 110}, {1568, 170}, {0, 0}};
constexpr uint8_t kPreviewNotes = sizeof(kPreview) / sizeof(kPreview[0]);

void applyTimezone(const char *posix) {
  setenv("TZ", posix, 1);
  tzset();
}

}  // namespace

Runtime::Runtime() {
  topics_[topicCount_++] = &network_;
  topics_[topicCount_++] = &scan_;
  topics_[topicCount_++] = &updateStatus_;
  topics_[topicCount_++] = &remoteInfo_;
}

Runtime::~Runtime() {
  if (!serviceRunning_) return;
  stopping_.store(true);
  serviceWake_.notify();
  while (!serviceExited_.load()) osal::sleepMs(1);
}

bool Runtime::begin(const Board &board, const RuntimeConfig &config) {
  if (!board.display || !board.platform || !config.renderBuffer ||
      config.renderBufferPixels < (size_t)board.display->width()) {
    LOGE(kTag, "begin: display, platform and a render buffer of at least one row are required");
    return false;
  }
  board_ = board;
  config_ = config;
  renderer_ = Renderer(config.renderBuffer, config.renderBufferPixels, config.renderBuffer2);
  screen_ = Rect(0, 0, board.display->width(), board.display->height());
  dirty_.setBounds(screen_);
  for (size_t i = 0; i < topicCount_; ++i) topics_[i]->attach(this);

  settings_ = config.defaults;
  settings_.sanitize();
  bool migrated = false;
  const RecordStatus loaded = loadSettings(board.storage, settings_, &migrated);
  if (migrated) {
    LOGI(kTag, "settings migrated to v%u", (unsigned)SystemSettings::kVersion);
    saveSettings(board.storage, settings_);
  } else if (loaded != RecordStatus::Ok && loaded != RecordStatus::Missing) {
    LOGW(kTag, "settings %s; using defaults", recordStatusName(loaded));
  }
  // After an update the person is shown what changed, once. A device with no
  // saved settings is new (nothing to compare with): it only learns the version.
  {
    char seen[16] = {0};
    const RecordStatus seenStatus = loadRecord(board.storage, "sys", "seen", 1, seen, sizeof(seen));
    seen[sizeof(seen) - 1] = '\0';
    const bool hadSettings = loaded == RecordStatus::Ok;
    const bool newer = seenStatus != RecordStatus::Ok || UpdateService::compareVersions(STELLA_VERSION, seen) != 0;
    whatsNewPending_ = hadSettings && newer && UpdateService::compareVersions(STELLA_VERSION, seen) != -1;
    if (!hadSettings && newer) {
      char now[16] = STELLA_VERSION;
      saveRecord(board.storage, "sys", "seen", 1, now, sizeof(now));
    }
  }
  applyTimezone(settings_.timezone);
  applyPanelTrim(true);
  applyVolume();
  applyLed(true);
  appliedCal_ = LightCalibration{settings_.lightDark, settings_.lightSelf, settings_.lightBright};
  ambient_.configure(appliedCal_);

  size_t retainedSize = 0;
  void *retained = board.platform->retainedMemory(retainedSize);
  if (retained && retainedSize >= sizeof(RestartRecord)) {
    RestartRecord &record = *static_cast<RestartRecord *>(retained);
    if (restart_record::takePending(record)) {
      LOGW(kTag, "previous restart by supervisor: thread=%s stalled=%lus (total %lu)", record.thread,
           (unsigned long)(record.stalledMs / 1000), (unsigned long)record.restarts);
    }
    supervisorRestarts_ = record.restarts;
  }
  {
    // An update that was running when the device last reset: say so (About).
    uint8_t stage = 0, percent = 0;
    if (update_crumb::take(board.platform, stage, percent)) {
      update_crumb::describe(stage, percent, resetNote_, sizeof(resetNote_));
      LOGW(kTag, "the last run reset during %s", resetNote_);
    }
  }

  wifi_.configure(board.wifi, board.storage, &jobs_, config.wifiSsid, config.wifiPassword, board.setup);
  if (wifi_.available()) jobs_.add(wifi_, millis());
  http_.configure(board.http, &networkResource_);
  updater_.configure(&http_, board.firmware, board.platform, &jobs_, config.updateManifestUrl, STELLA_VERSION);
  updater_.setAutoCheck(settings_.autoUpdate);
  // The first daily check waits a couple of minutes, so boot, Wi-Fi and the
  // first weather fetch get the network (and its TLS lease) first.
  if (updater_.enabled()) jobs_.add(updater_, millis(), 2u * 60u * 1000u);
  calendarSource_.configure(board.storage, &jobs_);
  remote_.configure(board.remote, board.wallpaper, board.wifi, board.storage, &jobs_, &network_, &calendarSource_);
  jobs_.add(remote_, millis(), 500);
  reloadWallpaper();
  // A freshly updated image runs on probation: it is confirmed after a healthy
  // minute, and otherwise the bootloader returns to the previous one.
  updateUnconfirmed_ = board.platform->updatePending();
  confirmAtMs_ = millis() + 60000;
  if (updateUnconfirmed_) LOGI(kTag, "running a fresh update on probation; confirming after 60 s of healthy running");
  jobs_.setWakeSignal(&serviceWake_);

  lastInteraction_ = millis();
  updateTheme(true);
  applyBrightness();
  LOGI(kTag, "Stella %s on %s, %dx%d %s", STELLA_VERSION, board.platform->boardName(), screen_.w, screen_.h,
       theme_.density == Density::Regular ? "regular" : "compact");
  return true;
}

bool Runtime::install(App &app) {
  const AppInfo &info = app.info();
  if (started_ || appCount_ >= kMaxApps || !validStorageName(info.id)) {
    LOGE(kTag, "cannot install app '%s'", info.id ? info.id : "?");
    return false;
  }
  for (size_t i = 0; i < appCount_; ++i) {
    if (strcmp(apps_[i].app->info().id, info.id) == 0) {
      LOGE(kTag, "duplicate app id '%s'", info.id);
      return false;
    }
  }
  apps_[appCount_].app = &app;
  if ((info.flags & kAppHome) && home_ < 0) home_ = (int)appCount_;
  ++appCount_;
  app.onInstall(*this);
  return true;
}

bool Runtime::wallClock(int64_t &epochSeconds) {
  return board_.platform && board_.platform->wallClock(epochSeconds);
}

bool Runtime::addJob(Job &job, uint32_t firstDelayMs) {
  if (started_) return false;
  return jobs_.add(job, millis(), firstDelayMs);
}

bool Runtime::attachTopic(TopicBase &topic) {
  if (started_ || topicCount_ >= kMaxTopics) return false;
  topic.attach(this);
  topics_[topicCount_++] = &topic;
  return true;
}

bool Runtime::start() {
  if (started_ || appCount_ == 0) return false;
  if (home_ < 0) home_ = 0;
  const uint32_t now = millis();
  uiWatch_ = supervisor_.watch("ui", config_.uiStallLimitMs, now);
  // The service thread runs background jobs and also supervises the UI loop
  // (a thread cannot notice its own stall), so it starts whenever either is needed.
  if (config_.serviceThread && (jobs_.size() > 0 || config_.supervise)) {
    serviceWatch_ = supervisor_.watch("svc", config_.serviceStallLimitMs, now);
    osal::ThreadSpec spec;
    spec.name = "stella-svc";
    spec.stackBytes = config_.serviceStackBytes;
    spec.core = config_.serviceCore;
    serviceRunning_ = osal::startThread(spec, serviceMain, this);
    if (!serviceRunning_) {
      supervisor_.retire(serviceWatch_);
      LOGE(kTag, "service thread could not start; background jobs disabled");
    }
  }
  started_ = true;
  foreground_ = -1;
  launch(apps_[home_].app->info().id);
  for (size_t i = 0; i < appCount_; ++i) {
    apps_[i].wakeArmed = true;  // every app's first onWake() runs on the first step
    apps_[i].wakeAt = now;
  }
  return true;
}

// ------------------------------------------------------------------ main loop

uint32_t Runtime::step() {
  if (!started_) return config_.idlePollMs;
  uint32_t now = millis();
  supervisor_.beat(uiWatch_, now);
  superviseThreads(now);

  Event e;
  while (events_.pop(e)) {
    if (e.type == EventType::Topic) handleTopicEvent(e.id);
  }

  pollTouch(now);

  if (toastVisible_ && reached(now, toastUntil_)) {
    toastVisible_ = false;
    invalidate(toastRect().inset(-glassOutset(theme_)));
  }

  const LocalTime lt = localTime();
  const int minuteKey = lt.valid ? lt.hour * 60 + lt.minute : -1;
  if (minuteKey != lastMinute_) {
    lastMinute_ = minuteKey;
    if (!topFullscreen()) invalidate(statusBarRect());
    updateTheme(false);
  }

  runWakes(now);
  updateAlert(millis());
  updatePreview(millis());
  updateLight(millis());
  updateLed(millis());
  maybeShowWhatsNew(now);

  if (tickArmed_ && reached(now, nextTickAt_)) {
    if (Screen *s = topScreen()) {
      callbackApp_ = foreground();
      const uint32_t next = s->onTick(*this);
      callbackApp_ = nullptr;
      if (next == kNoTick) tickArmed_ = false;
      else nextTickAt_ = millis() + next;
    }
  }

  updatePower(now);

  if (updateUnconfirmed_ && frames_ > 0 && reached(now, confirmAtMs_)) {
    updateUnconfirmed_ = false;
    board_.platform->confirmUpdate();
    LOGI(kTag, "update confirmed");
  }
  if (restartPending_ && reached(now, restartAtMs_)) {
    LOGI(kTag, "restarting on request");
    board_.platform->restart();
    restartPending_ = false;  // a simulated board returns
  }

  uiStackFree_.store(board_.platform->stackHeadroom());
  if (reached(now, nextHealthLog_)) {
    // One line a minute for long-running (overnight) validation on hardware.
    nextHealthLog_ = now + 60000;
    const MemoryStats m = board_.platform->memory();
    const NetworkStatus net = network_.get();
    LOGI(kTag, "health up=%lus heap=%lu min=%lu block=%lu stack ui=%lu svc=%lu frame=%lums wifi=%d drops=%lu",
         (unsigned long)(now / 1000), (unsigned long)m.freeHeap, (unsigned long)m.minimumFreeHeap,
         (unsigned long)m.largestFreeBlock, (unsigned long)uiStackFree_.load(),
         (unsigned long)serviceStackFree_.load(), (unsigned long)lastFrameMs_, (int)net.state,
         (unsigned long)events_.dropped());
  }

  if (settingsDirty_ && reached(now, settingsSaveAt_)) {
    settingsDirty_ = false;
    if (board_.storage && !saveSettings(board_.storage, settings_)) LOGW(kTag, "settings could not be saved");
  }

  if (!dirty_.empty() && power_ != PowerState::Off) {
    const uint32_t t0 = millis();
    lastStats_ = renderer_.render(*board_.display, dirty_, drawFrame, this);
    lastFrameMs_ = elapsedMs(millis(), t0);
    ++frames_;
  }

  now = millis();
  uint32_t wait = gestures_.active() ? config_.activePollMs
                  : power_ == PowerState::Off ? config_.sleepPollMs
                                              : config_.idlePollMs;
  if (tickArmed_) wait = minWait(wait, remainingMs(now, nextTickAt_));
  if (toastVisible_) wait = minWait(wait, remainingMs(now, toastUntil_));
  for (size_t i = 0; i < appCount_; ++i)
    if (apps_[i].wakeArmed) wait = minWait(wait, remainingMs(now, apps_[i].wakeAt));
  if (alertCount_) {
    if (alerts_[0].sound && board_.sound) wait = minWait(wait, remainingMs(now, chimeAt_));
    if (alerts_[0].timeoutMs) wait = minWait(wait, remainingMs(now, alertShownAt_ + alerts_[0].timeoutMs));
  }
  if (previewStep_) wait = minWait(wait, remainingMs(now, previewAt_));
  if (ledAnimating_ || ledProbing_) wait = minWait(wait, 33);  // a breathing LED needs a frame every so often
  if (board_.light) wait = minWait(wait, remainingMs(now, nextLightAt_));
  return wait;
}

uint32_t Runtime::serviceStep() {
  if (serviceRunning_) return JobScheduler::kIdleWaitMs;
  return jobs_.runDue(clockThunk, this);
}

void Runtime::run() {
  const uint32_t wait = step();
  if (wait > 0) events_.wait(wait);
}

uint32_t Runtime::millis() const { return board_.platform ? board_.platform->millis() : 0; }

LocalTime Runtime::localTime() const {
  int64_t epoch = 0;
  if (!board_.platform || !board_.platform->wallClock(epoch)) return LocalTime{};
  return localTimeAt(epoch);
}

LocalTime Runtime::localTimeAt(int64_t epoch) const {
  LocalTime out;
  const time_t t = (time_t)epoch;
  struct tm tmv;
  if (!localtime_r(&t, &tmv)) return out;
  out.valid = true;
  out.epoch = epoch;
  out.year = tmv.tm_year + 1900;
  out.month = tmv.tm_mon + 1;
  out.day = tmv.tm_mday;
  out.hour = tmv.tm_hour;
  out.minute = tmv.tm_min;
  out.second = tmv.tm_sec;
  out.weekday = tmv.tm_wday;
  out.yearDay = tmv.tm_yday;
  return out;
}

void Runtime::formatTime(const LocalTime &t, char *out, size_t capacity) const {
  if (!t.valid) {
    snprintf(out, capacity, "--:--");
  } else if (settings_.clock24h) {
    snprintf(out, capacity, "%02d:%02d", t.hour, t.minute);
  } else {
    const int h = t.hour % 12 == 0 ? 12 : t.hour % 12;
    snprintf(out, capacity, "%d:%02d", h, t.minute);
  }
}

// ------------------------------------------------------------------ theme

void Runtime::updateTheme(bool force) {
  const LocalTime lt = localTime();
  // Unknown time: assume night, the calmest palette for a bedside device.
  timePhase_ = SkyPhase::Night;
  if (lt.valid) {
    // The real sun when the Weather place has told us about today; else the hours.
    const bool sunToday = sunRise_ > 0 && sunSet_ > sunRise_ && lt.epoch > sunRise_ - 18 * 3600 && lt.epoch < sunRise_ + 30 * 3600;
    if (sunPolar_ == 1) timePhase_ = SkyPhase::Day;
    else if (sunPolar_ == 2) timePhase_ = SkyPhase::Night;
    else if (sunToday) timePhase_ = phaseForSun(lt.epoch, sunRise_, sunSet_);
    else timePhase_ = phaseForHour(lt.hour);
  }
  // The sky can be kept at one phase (Settings > Appearance).
  const SkyPhase phase = settings_.skyPin ? (SkyPhase)(settings_.skyPin - 1) : timePhase_;
  const bool dark = resolveDark(settings_.appearance, phase);
  const Density density = densityFor(screen_.w, screen_.h);
  const Look look = settings_.look(dark);
  const bool sameLook = look.tone == theme_.look.tone && look.contrast == theme_.look.contrast &&
                        look.whites == theme_.look.whites && look.warmth == theme_.look.warmth &&
                        look.dither == theme_.look.dither;
  if (!force && phase == theme_.phase && dark == theme_.color.dark && density == theme_.density && sameLook) {
    if (board_.display) applyBrightness();  // night dimming follows the clock, not the pinned sky
    return;
  }
  theme_ = makeTheme(density, dark, phase, look);
  content_ = Rect(screen_.x, screen_.y + theme_.m.statusBar, screen_.w, screen_.h - theme_.m.statusBar);
  if (board_.display) applyBrightness();
  GestureConfig g;
  g.slop = theme_.m.touchSlop;
  gestures_.configure(g);
  layoutTop();
  if (alertCount_) layoutAlert();
  invalidateAll();
}

// ------------------------------------------------------------------ navigation

App *Runtime::foreground() const {
  const AppSlot *s = slot(foreground_);
  return s ? s->app : nullptr;
}

Screen *Runtime::topScreen() const {
  const AppSlot *s = slot(foreground_);
  return s && s->depth ? s->stack[s->depth - 1] : nullptr;
}

const AppInfo *Runtime::appAt(size_t index) const { return index < appCount_ ? &apps_[index].app->info() : nullptr; }

void Runtime::layoutTop() {
  if (Screen *s = topScreen()) s->layout(areaFor(s), theme_);
}

void Runtime::showTop() {
  Screen *s = topScreen();
  if (!s) return;
  s->layout(areaFor(s), theme_);
  s->setVisible(true);
  callbackApp_ = foreground();
  s->onShow(*this);
  callbackApp_ = nullptr;
  tickArmed_ = true;
  nextTickAt_ = millis();
  invalidateAll();
  if (board_.display) applyBrightness();  // a fullscreen screen runs the backlight low
}

void Runtime::hideTop() {
  if (Screen *s = topScreen()) {
    callbackApp_ = foreground();
    s->onHide(*this);
    callbackApp_ = nullptr;
    s->setVisible(false);
  }
  tickArmed_ = false;
}

bool Runtime::push(Screen &screen) {
  AppSlot *s = slot(foreground_);
  if (!s) return false;
  if (s->depth >= kMaxDepth) {
    LOGE(kTag, "screen stack full in '%s'", s->app->info().id);
    return false;
  }
  hideTop();
  s->stack[s->depth++] = &screen;
  showTop();
  return true;
}

void Runtime::pop() {
  AppSlot *s = slot(foreground_);
  if (!s) return;
  if (s->depth > 1) {
    hideTop();
    --s->depth;
    showTop();
  } else if (foreground_ != home_) {
    goHome();
  }
}

void Runtime::goHome() {
  if (home_ < 0) return;
  if (foreground_ == home_) {
    AppSlot *s = slot(home_);
    if (s->depth > 1) {
      hideTop();
      s->depth = 1;
      showTop();
    }
    return;
  }
  launch(apps_[home_].app->info().id);
}

bool Runtime::launch(const char *appId, const char *route) {
  int index = -1;
  for (size_t i = 0; i < appCount_; ++i) {
    if (strcmp(apps_[i].app->info().id, appId) == 0) index = (int)i;
  }
  if (index < 0) return false;
  if (index == foreground_) {
    if (route) {
      callbackApp_ = apps_[index].app;
      apps_[index].app->onRoute(route, *this);
      callbackApp_ = nullptr;
    }
    return true;
  }
  hideTop();
  foreground_ = index;
  AppSlot &s = apps_[index];
  if (!s.started) {
    callbackApp_ = s.app;
    s.app->onStart(*this);
    callbackApp_ = nullptr;
    s.started = true;
  }
  if (s.depth == 0) s.stack[s.depth++] = &s.app->root();
  showTop();
  if (route) {
    callbackApp_ = s.app;
    s.app->onRoute(route, *this);
    callbackApp_ = nullptr;
  }
  return true;
}

bool Runtime::canGoBack() const {
  const AppSlot *s = slot(foreground_);
  return s && (foreground_ != home_ || s->depth > 1);
}

// ------------------------------------------------------------------ input

void Runtime::pollTouch(uint32_t now) {
  if (!board_.touch) return;
  TouchSample sample;
  if (!board_.touch->sample(sample)) return;
  if (sample.pressed) {
    // Calibration correction (Touch test > Calibrate), clamped to the screen.
    const SystemSettings &cal = settings_;
    int x = sample.x * cal.touchScaleX / 1024 + cal.touchOffsetX;
    int y = sample.y * cal.touchScaleY / 1024 + cal.touchOffsetY;
    sample.x = (int16_t)(x < 0 ? 0 : x >= screen_.w ? screen_.w - 1 : x);
    sample.y = (int16_t)(y < 0 ? 0 : y >= screen_.h ? screen_.h - 1 : y);
    lastInteraction_ = now;
    if (power_ != PowerState::Active) {
      // A dark screen: the waking touch only wakes and never reaches an app.
      // A dimmed screen is still readable, so the touch brightens it and acts.
      if (power_ == PowerState::Off && !gestures_.active()) gestures_.consumeCurrent();
      power_ = PowerState::Active;
      applyBrightness();
    }
  }
  Event out[2];
  const int n = gestures_.update(sample, now, out);
  for (int i = 0; i < n; ++i) dispatchPointer(out[i]);
}

Rect Runtime::backButtonRect() const {
  const int s = theme_.m.statusBar;
  return Rect(screen_.x + theme_.m.margin / 2, screen_.y, s, s);
}

void Runtime::dispatchPointer(const Event &e) {
  if (alertCount_) {
    // The alert is modal: only its buttons respond.
    const int buttons = alerts_[0].secondary[0] ? 2 : 1;
    for (int i = 0; i < buttons; ++i) {
      if (alertButtons_[i].handle(e, *this)) {
        closeAlert(i == 0 ? kAlertPrimary : kAlertSecondary);
        return;
      }
    }
    return;
  }
  if (e.type == EventType::Press) pointerToShell_ = !topFullscreen() && statusBarRect().contains(e.x, e.y);
  if (pointerToShell_) {
    if (e.type == EventType::LongPress) {
      goHome();
      return;
    }
    // The back target is generous: the left third of the status bar.
    const Rect hit(screen_.x, screen_.y, screen_.w / 3, theme_.m.statusBar);
    if (canGoBack() && trackPress(hit, e, backPressed_, *this, statusBarRect())) pop();
    return;
  }
  if (Screen *s = topScreen()) {
    callbackApp_ = foreground();
    s->onEvent(e, *this);
    callbackApp_ = nullptr;
  }
}

void Runtime::topicChanged(uint16_t topicId) {
  Event e;
  e.type = EventType::Topic;
  e.id = topicId;
  events_.push(e);
}

void Runtime::handleTopicEvent(uint16_t id) {
  for (size_t i = 0; i < topicCount_; ++i) {
    if (topics_[i]->id() == id) topics_[i]->acknowledge();
  }
  if (id == topics::kNetwork && !topFullscreen()) invalidate(statusBarRect());
  if (id == topics::kUpdate) noteUpdateForLed();
  if (id == topics::kRemote) reloadWallpaper();
  if (id == topics::kNetwork) noteNetworkForLed();
  for (size_t i = 0; i < appCount_; ++i) {
    if (!apps_[i].started) continue;
    callbackApp_ = apps_[i].app;
    apps_[i].app->onTopic(id, *this);
  }
  callbackApp_ = nullptr;
}

// ------------------------------------------------------------------ power

void Runtime::updatePower(uint32_t now) {
  if (gestures_.active() || alertCount_) return;  // an alert keeps the screen awake
  const uint32_t idle = elapsedMs(now, lastInteraction_);
  PowerState next = PowerState::Active;
  if (settings_.sleepAfterSec && idle >= settings_.sleepAfterSec * 1000u) next = PowerState::Off;
  else if (settings_.dimAfterSec && idle >= settings_.dimAfterSec * 1000u) next = PowerState::Dimmed;
  if (next == power_ || (next == PowerState::Active)) return;  // only touch wakes
  if (power_ == PowerState::Off && next == PowerState::Dimmed) return;
  power_ = next;
  applyBrightness();
  if (next == PowerState::Dimmed) maybeIdleFace();
}

// After the idle dim, at night, an app that offers an idle face (the Clock's
// bedside mode) takes over, but only from Home or from that app itself, so
// nothing the user is in the middle of is replaced.
void Runtime::maybeIdleFace() {
  if (!settings_.bedside) return;
  if (settings_.bedside == 1 && timePhase_ != SkyPhase::Night) return;
  if (!localTime().valid || topFullscreen() || alertCount_) return;
  for (size_t i = 0; i < appCount_; ++i) {
    if (!(apps_[i].app->info().flags & kAppIdleFace)) continue;
    if (foreground_ != home_ && foreground_ != (int)i) return;
    launch(apps_[i].app->info().id, settings_.idleFace == 1 ? "dashboard" : "bedside");
    return;
  }
}

void Runtime::applyBrightness() {
  uint8_t level = settings_.brightness;
  // In a dark room (once calibrated): a fraction of what the person chose.
  if (settings_.autoBrightness && settings_.lightCalibrated() && board_.light) {
    const int v = level * appliedScale_ / 255;
    level = (uint8_t)(v < 16 ? 16 : v);
  }
  // Bedside comfort: the night sky also caps the backlight.
  if (settings_.nightDim && timePhase_ == SkyPhase::Night) level = nightBrightness(level);
  if (power_ == PowerState::Dimmed) level = level / 2 < 16 ? 16 : level / 2;
  if (topFullscreen()) level = level / 3 < 16 ? 16 : level / 3;  // idle face: dim
  level = backlightDuty(level);  // perceived level -> PWM duty
  if (power_ == PowerState::Off) level = 0;
  if (holdDuty_ >= 0) level = (uint8_t)holdDuty_;  // calibration: an exact backlight
  if (brightnessKnown_ && level == appliedBrightness_) return;
  board_.display->setBacklight(level);
  appliedBrightness_ = level;
  brightnessKnown_ = true;
}

// ------------------------------------------------------------------ system services

void Runtime::toast(const char *text) {
  if (toastVisible_) invalidate(toastRect().inset(-glassOutset(theme_)));
  copyText(toastText_, sizeof(toastText_), text);
  toastVisible_ = true;
  toastUntil_ = millis() + kToastMs;
  invalidate(toastRect().inset(-glassOutset(theme_)));
}

Rect Runtime::toastRect() const {
  const Theme &t = theme_;
  const int w0 = Canvas::textWidth(*t.font.body, toastText_) + 2 * t.m.padding + t.dp(8);
  const int maxW = screen_.w - 2 * t.m.margin;
  const int w = w0 < maxW ? w0 : maxW, h = t.m.control;
  return Rect(screen_.centerX() - w / 2, screen_.bottom() - t.m.margin - h, w, h);
}

void Runtime::applyPanelTrim(bool force) {
  Display *d = board_.display;
  if (!d || !d->supportsPanelTrim()) return;
  if (!force && settings_.panelVcom == appliedTrim_.vcom && settings_.panelRefreshHz == appliedTrim_.refreshHz) return;
  appliedTrim_.vcom = settings_.panelVcom;
  appliedTrim_.refreshHz = settings_.panelRefreshHz;
  d->waitIdle();  // no pixel transfer may be in flight
  d->setPanelTrim(appliedTrim_);
}

void Runtime::applySettings(const SystemSettings &s) {
  const bool timezoneChanged = strcmp(s.timezone, settings_.timezone) != 0;
  const bool autoUpdateWasOn = settings_.autoUpdate;
  const bool unitsWereF = settings_.fahrenheit;
  settings_ = s;
  settings_.sanitize();
  // Texts that apps offer to others (the weather on the bedside face) are
  // written in the chosen units, so every app gets to refresh them now.
  if (settings_.fahrenheit != unitsWereF) {
    for (size_t i = 0; i < appCount_; ++i) {
      apps_[i].wakeArmed = true;
      apps_[i].wakeAt = millis();
    }
  }
  // Turning the daily check on looks right away (quietly) instead of tomorrow.
  if (settings_.autoUpdate && !autoUpdateWasOn) {
    updater_.setAutoCheck(true);
    jobs_.requestRun(updater_);
  }
  if (timezoneChanged) applyTimezone(settings_.timezone);
  applyPanelTrim(false);
  settingsDirty_ = true;
  settingsSaveAt_ = millis() + kSettingsDebounceMs;
  applyLed(false);
  const LightCalibration cal{settings_.lightDark, settings_.lightSelf, settings_.lightBright};
  if (cal.dark != appliedCal_.dark || cal.self != appliedCal_.self || cal.bright != appliedCal_.bright) {
    appliedCal_ = cal;
    ambient_.configure(cal);
    lightWarm_ = false;
  }
  if (!settings_.autoBrightness) appliedScale_ = 255;
  applyBrightness();
  applyVolume();
  updater_.setAutoCheck(settings_.autoUpdate);
  updateTheme(false);
  invalidate(statusBarRect());
}

SystemStatus Runtime::status() const {
  SystemStatus s;
  s.board = board_.platform->boardName();
  s.version = STELLA_VERSION;
  s.uptimeMs = millis();
  s.memory = board_.platform->memory();
  s.resetCause = board_.platform->resetCause();
  copyText(s.resetNote, sizeof(s.resetNote), resetNote_);
  s.hasLed = board_.led != nullptr;
  s.hasLight = board_.light != nullptr;
  s.hasSound = board_.sound != nullptr;
  s.night = timePhase_ == SkyPhase::Night;
  s.ambient = settings_.autoBrightness && settings_.lightCalibrated() && board_.light && lightWarm_
                  ? (int8_t)(ambient_.level() * 100.f + 0.5f)
                  : (int8_t)-1;
  s.width = screen_.w;
  s.height = screen_.h;
  s.density = theme_.density;
  network_.read(s.network);
  s.supervisorRestarts = supervisorRestarts_;
  s.framesRendered = frames_;
  s.lastFrameMs = lastFrameMs_;
  s.uiStackFree = uiStackFree_.load();
  s.serviceStackFree = serviceStackFree_.load();
  s.panelTrim = board_.display && board_.display->supportsPanelTrim();
  s.sunrise = sunRise_;
  s.sunset = sunSet_;
  s.sunPolar = sunPolar_;
  for (size_t i = 0; i < appCount_; ++i) {
    const int64_t t = apps_[i].nextAlarm;
    if (t > 0 && (s.nextAlarm == 0 || t < s.nextAlarm)) s.nextAlarm = t;
    if (s.glance.empty() && !apps_[i].glance.empty()) s.glance = apps_[i].glance;
  }
  return s;
}

RecordStatus Runtime::loadAppRecord(const char *key, uint16_t version, void *data, size_t size) {
  App *app = callbackApp_ ? callbackApp_ : foreground();
  if (!app) return RecordStatus::Unavailable;
  return loadRecord(board_.storage, app->info().id, key, version, data, size);
}

bool Runtime::saveAppRecord(const char *key, uint16_t version, const void *data, size_t size) {
  App *app = callbackApp_ ? callbackApp_ : foreground();
  if (!app) return false;
  return saveRecord(board_.storage, app->info().id, key, version, data, size);
}

// ------------------------------------------------------------------ supervision

void Runtime::superviseThreads(uint32_t now) {
  if (!config_.supervise) return;
  uint32_t age = 0;
  const int stalled = supervisor_.firstStalled(now, age);
  if (stalled < 0 || restartRequested_.exchange(true)) return;
  size_t size = 0;
  void *retained = board_.platform->retainedMemory(size);
  if (retained && size >= sizeof(RestartRecord)) {
    restart_record::note(*static_cast<RestartRecord *>(retained), supervisor_.name(stalled), age);
  }
  LOGE(kTag, "thread '%s' made no progress for %lus; restarting", supervisor_.name(stalled),
       (unsigned long)(age / 1000));
  board_.platform->restart();
}

uint32_t Runtime::clockThunk(void *self) { return static_cast<Runtime *>(self)->millis(); }

void Runtime::serviceMain(void *self) {
  Runtime &rt = *static_cast<Runtime *>(self);
  while (!rt.stopping_.load()) {
    rt.supervisor_.beat(rt.serviceWatch_, rt.millis());
    const uint32_t wait = rt.jobs_.runDue(clockThunk, self);
    rt.serviceStackFree_.store(rt.board_.platform->stackHeadroom());
    const uint32_t now = rt.millis();
    rt.supervisor_.beat(rt.serviceWatch_, now);
    rt.superviseThreads(now);
    rt.serviceWake_.wait(wait < 10 ? 10 : wait);
  }
  rt.serviceExited_.store(true);
}

// ------------------------------------------------------------------ drawing

void Runtime::drawFrame(Canvas &canvas, void *self) { static_cast<Runtime *>(self)->draw(canvas); }

void Runtime::draw(Canvas &c) {
  Screen *s = topScreen();
  if (s && s->fullscreen()) {
    const Rect previous = c.pushClip(screen_);
    s->draw(c, theme_);
    c.restoreClip(previous);
    if (toastVisible_) drawToast(c);
    if (alertCount_) drawAlert(c);
    return;
  }
  if (wallpaperOn_) paintWallpaper(c);
  else paintSky(c, theme_, screen_);
  if (c.visible(statusBarRect())) drawStatusBar(c);
  if (s && c.visible(content_)) {
    const Rect previous = c.pushClip(content_);
    s->draw(c, theme_);
    c.restoreClip(previous);
  }
  if (toastVisible_) drawToast(c);
  if (alertCount_) drawAlert(c);
}

void Runtime::drawStatusBar(Canvas &c) {
  const Theme &t = theme_;
  const Palette &p = t.color;
  const Rect bar = statusBarRect().inset(t.m.margin, 0);

  // Home shows the time large in its own header, so its status bar has none.
  const bool back = canGoBack();
  char clock[12] = "";
  if (back) formatTime(localTime(), clock, sizeof(clock));
  const NetworkStatus net = network_.get();
  const int iconSize = t.dp(14);
  const int rightW = (clock[0] ? Canvas::textWidth(*t.font.caption, clock) + t.dp(6) : 0) + (net.present ? iconSize : 0);
  Rect parts[2];
  layout::row(bar, {layout::flex(), layout::px(rightW)}, t.m.gapM, parts);

  // Left: back affordance + title, or the Stella brand on the home screen.
  Rect left = parts[0];
  if (back) {
    const Rect box = layout::leading(left, iconSize);
    if (backPressed_) c.fillCircle(box.centerX(), box.centerY(), iconSize / 2 + t.dp(4), p.accent, 46);
    icons::back(c, box, p.accent);
  } else {
    icons::sparkle(c, layout::leading(left, iconSize), p.accent);
  }
  left = Rect(left.x + iconSize + t.dp(6), left.y, left.w - iconSize - t.dp(6), left.h);
  const Screen *s = topScreen();
  const App *app = foreground();
  if (back) {
    const char *title = s && s->title() ? s->title() : app ? app->info().name : "";
    c.drawTextIn(*t.font.title, title, left, p.text);
  }

  // Right: network indicator + clock.
  Rect right = parts[1];
  if (net.present) {
    // Settled: muted. Joining: accent. Problem: soft danger pink. Unset: faint.
    const Color wifiColor = net.state == WifiState::Connected    ? p.textMuted
                            : net.state == WifiState::Connecting ? p.accent
                            : net.state == WifiState::Disconnected ? p.danger
                                                                   : blend(p.glass, p.textMuted, 120);
    icons::wifi(c, layout::trailing(Rect(right.x, right.y, right.w - (clock[0] ? Canvas::textWidth(*t.font.caption, clock) + t.dp(6) : 0), right.h), iconSize), wifiColor);
    right = Rect(right.right() - (clock[0] ? Canvas::textWidth(*t.font.caption, clock) : 0), right.y, clock[0] ? Canvas::textWidth(*t.font.caption, clock) : 0, right.h);
  }
  if (clock[0]) c.drawTextIn(*t.font.caption, clock, right, p.textMuted, Align::Right);
}

void Runtime::drawToast(Canvas &c) {
  const Rect r = toastRect();
  if (!c.visible(r.inset(-glassOutset(theme_)))) return;
  paintGlass(c, theme_, r, r.h / 2);
  c.drawTextIn(*theme_.font.body, toastText_, r.inset(theme_.m.padding, 0), theme_.color.text, Align::Center);
}

}  // namespace stella

namespace stella {

// ------------------------------------------------------------------ background wake-ups

int Runtime::callerIndex() const {
  const App *app = callbackApp_ ? callbackApp_ : foreground();
  for (size_t i = 0; i < appCount_; ++i)
    if (apps_[i].app == app) return (int)i;
  return -1;
}

void Runtime::setNextAlarm(int64_t epochSeconds) {
  const int index = callerIndex();
  if (index >= 0) apps_[index].nextAlarm = epochSeconds;
}

void Runtime::setGlance(IconFn icon, const char *text) {
  const int index = callerIndex();
  if (index < 0) return;
  Glance &g = apps_[index].glance;
  g.icon = icon;
  copyText(g.text, sizeof(g.text), text ? text : "");
  if (g.empty()) g.icon = nullptr;
}

void Runtime::scheduleWake(uint32_t delayMs) {
  const int index = callerIndex();
  if (index < 0) return;
  AppSlot &s = apps_[index];
  const uint32_t at = millis() + delayMs;
  if (!s.wakeArmed || remainingMs(millis(), s.wakeAt) > delayMs) s.wakeAt = at;
  s.wakeArmed = true;
}

void Runtime::runWakes(uint32_t now) {
  for (size_t i = 0; i < appCount_; ++i) {
    AppSlot &s = apps_[i];
    if (!s.wakeArmed || !reached(now, s.wakeAt)) continue;
    s.wakeArmed = false;  // scheduleWake() inside onWake() may re-arm it
    App *previous = callbackApp_;
    callbackApp_ = s.app;
    const uint32_t next = s.app->onWake(*this);
    callbackApp_ = previous;
    if (next == kNoTick) continue;
    const uint32_t t = millis();
    if (!s.wakeArmed || remainingMs(t, s.wakeAt) > next) s.wakeAt = t + next;
    s.wakeArmed = true;
  }
}

// ------------------------------------------------------------------ alerts

bool Runtime::showAlert(const Alert &alert) {
  const int owner = callerIndex();
  if (owner < 0) return false;
  size_t index = alertCount_;
  for (size_t i = 0; i < alertCount_; ++i)
    if (alerts_[i].owner == owner && alerts_[i].tag == alert.tag) index = i;
  if (index == alertCount_) {
    if (alertCount_ >= kMaxAlerts) {
      LOGW(kTag, "alert queue full; '%s' dropped", alert.title ? alert.title : "");
      return false;
    }
    ++alertCount_;
  }
  PendingAlert &a = alerts_[index];
  copyText(a.title, sizeof(a.title), alert.title ? alert.title : "");
  copyText(a.message, sizeof(a.message), alert.message ? alert.message : "");
  copyText(a.primary, sizeof(a.primary), alert.primary ? alert.primary : "OK");
  copyText(a.secondary, sizeof(a.secondary), alert.secondary ? alert.secondary : "");
  a.icon = alert.icon;
  a.tag = alert.tag;
  a.owner = (int8_t)owner;
  a.sound = alert.sound;
  a.timeoutMs = alert.timeoutMs;
  if (index == 0) beginAlert(millis());  // new, or the one on screen changed
  return true;
}

void Runtime::dismissAlert(uint16_t tag) {
  const int owner = callerIndex();
  for (size_t i = 0; i < alertCount_; ++i) {
    if (alerts_[i].owner != owner || alerts_[i].tag != tag) continue;
    for (size_t j = i + 1; j < alertCount_; ++j) alerts_[j - 1] = alerts_[j];
    --alertCount_;
    if (i == 0) {
      if (board_.sound) board_.sound->tone(0);
      invalidateAll();
      if (alertCount_) beginAlert(millis());
    }
    return;
  }
}

void Runtime::beginAlert(uint32_t now) {
  alertShownAt_ = now;
  chimeStep_ = 0;
  chimeAt_ = now;
  if (board_.sound && !alerts_[0].sound) board_.sound->tone(0);
  // A finger already on the glass belongs to whatever was under it: cancel
  // that gesture so the alert cannot be answered by accident.
  if (gestures_.active()) {
    gestures_.consumeCurrent();
    Event cancel;
    cancel.type = EventType::Release;
    cancel.dragged = true;
    cancel.x = cancel.y = cancel.startX = cancel.startY = -1;
    if (Screen *s = topScreen()) {
      App *previous = callbackApp_;
      callbackApp_ = foreground();
      s->onEvent(cancel, *this);
      callbackApp_ = previous;
    }
    backPressed_ = false;
  }
  alertButtons_[0].pressed = alertButtons_[1].pressed = false;
  layoutAlert();
  lastInteraction_ = now;
  if (power_ != PowerState::Active) {
    power_ = PowerState::Active;
    applyBrightness();
  }
  invalidateAll();
  LOGI(kTag, "alert '%s' from %s", alerts_[0].title, apps_[alerts_[0].owner].app->info().id);
}

void Runtime::closeAlert(int action) {
  if (!alertCount_) return;
  const PendingAlert closed = alerts_[0];
  for (size_t j = 1; j < alertCount_; ++j) alerts_[j - 1] = alerts_[j];
  --alertCount_;
  if (board_.sound) board_.sound->tone(0);
  invalidateAll();
  lastInteraction_ = millis();
  if (alertCount_) beginAlert(millis());
  App *previous = callbackApp_;
  callbackApp_ = apps_[closed.owner].app;
  callbackApp_->onAlert(closed.tag, action, *this);
  callbackApp_ = previous;
}

void Runtime::updateAlert(uint32_t now) {
  if (!alertCount_) return;
  const PendingAlert &a = alerts_[0];
  if (a.timeoutMs && elapsedMs(now, alertShownAt_) >= a.timeoutMs) {
    closeAlert(kAlertTimedOut);
    return;
  }
  if (a.sound && board_.sound && reached(now, chimeAt_)) {
    const ChimeNote &note = kChime[chimeStep_];
    board_.sound->tone(note.hz);
    chimeAt_ = now + note.ms;
    chimeStep_ = (uint8_t)((chimeStep_ + 1) % kChimeNotes);
  }
}

void Runtime::setSun(int64_t sunrise, int64_t sunset, uint8_t polar) {
  if (sunrise == sunRise_ && sunset == sunSet_ && polar == sunPolar_) return;
  sunRise_ = sunrise;
  sunSet_ = sunset;
  sunPolar_ = polar > 2 ? 0 : polar;
  if (started_) updateTheme(false);  // the sky may change now
}

void Runtime::previewChime() {
  if (!board_.sound || settings_.volume == 0 || (alertCount_ && alerts_[0].sound)) return;
  previewStep_ = 1;
  previewAt_ = millis();
  updatePreview(previewAt_);
}

void Runtime::updatePreview(uint32_t now) {
  if (!previewStep_ || !reached(now, previewAt_)) return;
  if (alertCount_ && alerts_[0].sound) {  // a real alert took the speaker
    previewStep_ = 0;
    return;
  }
  const ChimeNote &note = kPreview[previewStep_ - 1];
  board_.sound->tone(note.hz);
  if (note.hz == 0) {
    previewStep_ = 0;
    return;
  }
  previewAt_ = now + note.ms;
  previewStep_ = (uint8_t)(previewStep_ + 1);
  if (previewStep_ > kPreviewNotes) previewStep_ = 0;
}

// ------------------------------------------------------------------ status LED and ambient light

// The wallpaper, when there is a valid one and the screen is the size it was made for.
void Runtime::reloadWallpaper() {
  const RemoteInfo info = remoteInfo_.get();
  wallpaperVeil_ = info.veil;
  bool on = info.wallpaper && board_.wallpaper && screen_.w == wallpaper::kWidth && screen_.h == wallpaper::kHeight;
  if (on) {
    uint8_t raw[wallpaper::kPaletteBytes];
    on = board_.wallpaper->read(wallpaper::kHeaderBytes, raw, sizeof(raw));
    for (int i = 0; on && i < wallpaper::kColors; ++i) wallpaperPalette_[i] = (uint16_t)(raw[2 * i] | (raw[2 * i + 1] << 8));
  }
  if (on != wallpaperOn_ || on) {
    wallpaperOn_ = on;
    if (started_) invalidateAll();
  }
}

// The picture, row by row, washed toward white (light mode) or black (dark mode)
// so the glass and its text stay readable: the same job the sky does.
void Runtime::paintWallpaper(Canvas &c) {
  const Rect area = c.clip().intersect(screen_);
  if (area.empty()) return;
  const Color wash = theme_.color.dark ? (Color)0x0000 : (Color)0xFFFF;
  const uint8_t alpha = (uint8_t)(wallpaperVeil_ * 255 / 100);
  const size_t base = wallpaper::kHeaderBytes + wallpaper::kPaletteBytes;
  for (int y = area.y; y < area.bottom(); ++y) {
    if (!board_.wallpaper->read(base + (size_t)y * wallpaper::kWidth + area.x, wallpaperRow_, (size_t)area.w)) {
      c.fillRect(Rect(area.x, y, area.w, 1), wash);
      continue;
    }
    for (int i = 0; i < area.w; ++i) {
      const Color color = wallpaperPalette_[wallpaperRow_[i]];
      c.at(area.x + i, y) = alpha ? blend(color, wash, alpha) : color;
    }
  }
}

// The changelog, once per version, when Home is in front and quiet (not in the
// middle of anything, not while an alert is up). It is marked seen as it opens.
void Runtime::maybeShowWhatsNew(uint32_t now) {
  if (!whatsNewPending_ || now < 6000 || frames_ == 0 || alertCount_ || power_ != PowerState::Active) return;
  if (foreground_ != home_ || gestures_.active() || topFullscreen()) return;
  AppSlot *home = slot(home_);
  if (!home || home->depth != 1) return;
  whatsNewPending_ = false;
  char version[16] = STELLA_VERSION;
  saveRecord(board_.storage, "sys", "seen", 1, version, sizeof(version));
  launch("settings", "whatsnew");
}

void Runtime::applyLed(bool force) {
  led_.configure(settings_.ledMode, settings_.ledLevel);
  if (!board_.led) return;
  LedWiring w;
  for (int i = 0; i < 3; ++i) w.pin[i] = settings_.ledPin[i];
  w.inverted = settings_.ledInverted;
  if (!force && memcmp(w.pin, ledWiring_.pin, sizeof(w.pin)) == 0 && w.inverted == ledWiring_.inverted) return;
  ledWiring_ = w;
  board_.led->configure(w);  // an unusable wiring releases the pins
  ledShownKnown_ = false;
}

void Runtime::updateLed(uint32_t now) {
  if (!board_.led) return;
  if (ledProbing_) {
    if (!reached(now, ledProbeUntil_)) return;
    ledProbe(kNoPin, true);  // forgotten: give the LED back
  }
  // The alert and the bedside glow follow the state of things, so they are set here.
  LedRequest alert;
  if (alertCount_) {
    alert.pattern = LedPattern::Breathe;
    alert.color = LedColor{255, 150, 40};  // warm amber
    alert.periodMs = 1600;
    alert.floor = 110;  // an alarm stays visible between breaths
  }
  led_.set(LedSource::Alert, alert);
  LedRequest glow;
  if (topFullscreen() && settings_.ledMode == 2) {
    glow.pattern = LedPattern::Breathe;
    glow.color = LedColor{24, 18, 44};  // a faint violet: this is a night light
    glow.periodMs = 7000;
    glow.floor = 90;
  }
  led_.set(LedSource::Glow, glow);
  bool animating = false;
  const LedColor c = led_.frame(now, animating);
  ledAnimating_ = animating;
  if (!ledShownKnown_ || c != ledShown_) {
    board_.led->set(c.r, c.g, c.b);
    ledShown_ = c;
    ledShownKnown_ = true;
  }
}

void Runtime::noteUpdateForLed() {
  const UpdateStatus st = updateStatus_.get();
  LedRequest r;
  switch (st.state) {
    case UpdateState::Downloading:
      r.pattern = LedPattern::Breathe;
      r.color = LedColor{70, 110, 255};  // blue while it works
      r.periodMs = 1400;
      r.floor = 30;
      break;
    case UpdateState::Ready:
      r.pattern = LedPattern::Breathe;
      r.color = LedColor{80, 220, 150};  // green: restart to use it
      r.periodMs = 3000;
      r.floor = 40;
      break;
    case UpdateState::Failed:
      if (lastUpdateState_ != UpdateState::Failed) {
        r.pattern = LedPattern::Steady;
        r.color = LedColor{255, 60, 80};
        r.untilMs = millis() + 1500;
      }
      break;
    default: break;
  }
  lastUpdateState_ = st.state;
  led_.set(LedSource::Update, r);
}

void Runtime::noteNetworkForLed() {
  const NetworkStatus net = network_.get();
  const App *fg = foreground();
  const bool inSettings = fg && strcmp(fg->info().id, "settings") == 0;
  if (net.setup != lastSetup_) {
    LedRequest r;
    if (net.setup) {
      r.pattern = LedPattern::Breathe;
      r.color = LedColor{170, 120, 255};  // the setup access point is open
      r.periodMs = 2400;
      r.floor = 30;
    }
    led_.set(LedSource::Wifi, r);
  } else if (inSettings && lastWifiState_ == WifiState::Connecting &&
             (net.state == WifiState::Connected || net.state == WifiState::Disconnected)) {
    // A join the person started in Settings: say how it went.
    LedRequest r;
    r.pattern = LedPattern::Steady;
    r.color = net.state == WifiState::Connected ? LedColor{80, 220, 150} : LedColor{255, 60, 80};
    r.untilMs = millis() + (net.state == WifiState::Connected ? 1200 : 1500);
    led_.set(LedSource::Wifi, r);
  }
  lastSetup_ = net.setup;
  lastWifiState_ = net.state;
}

void Runtime::flashLed(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
  LedRequest req;
  req.pattern = LedPattern::Steady;
  req.color = LedColor{r, g, b};
  req.untilMs = millis() + (ms ? ms : 1);
  led_.set(LedSource::Flash, req);
}

int Runtime::ledCandidates(uint8_t *out, int capacity) {
  return board_.led ? board_.led->candidatePins(out, capacity) : 0;
}

void Runtime::ledProbe(uint8_t pin, bool activeLow) {
  if (!board_.led) return;
  if (pin == kNoPin) {
    if (!ledProbing_) return;
    board_.led->probe(kNoPin, activeLow);
    ledProbing_ = false;
    applyLed(true);  // take the pins back with the saved wiring
    return;
  }
  ledProbing_ = true;
  ledProbeUntil_ = millis() + 10000;  // never leave an LED lit by mistake
  board_.led->probe(pin, activeLow);
}

uint16_t Runtime::lightReading() { return board_.light ? board_.light->read() : 0; }

void Runtime::holdBacklight(int duty) {
  holdDuty_ = duty < 0 ? -1 : duty > 255 ? 255 : duty;
  holdUntil_ = millis() + 30000;
  if (board_.display) applyBrightness();
}

// Four times a second: read the sensor, work out the room's light with the
// screen's own glow taken out, and move the brightness a little toward it.
void Runtime::updateLight(uint32_t now) {
  if (!board_.light) return;
  if (holdDuty_ >= 0 && reached(now, holdUntil_)) {
    holdDuty_ = -1;
    if (board_.display) applyBrightness();
  }
  if (!reached(now, nextLightAt_)) return;
  nextLightAt_ = now + 250;
  if (!settings_.autoBrightness || !settings_.lightCalibrated()) {
    if (appliedScale_ != 255) {
      appliedScale_ = 255;
      if (board_.display) applyBrightness();
    }
    return;
  }
  ambient_.sample(board_.light->read(), appliedBrightness_);
  if (holdDuty_ >= 0) return;  // a calibration is measuring: the screen is not ours to dim
  const uint8_t target = ambient_.scale();
  if (!lightWarm_) {
    lightWarm_ = true;
    appliedScale_ = target;  // the first reading: start where the room is
    if (board_.display) applyBrightness();
    return;
  }
  const int diff = (int)target - (int)appliedScale_;
  if (diff > -6 && diff < 6) return;  // a small change is not worth a visible step
  appliedScale_ = (uint8_t)((int)appliedScale_ + (diff > 0 ? (diff < 8 ? diff : 8) : (diff > -8 ? diff : -8)));
  if (board_.display) applyBrightness();
}

void Runtime::applyVolume() {
  if (board_.sound) board_.sound->setVolume(settings_.volume);
}

void Runtime::layoutAlert() {
  const Theme &t = theme_;
  const PendingAlert &a = alerts_[0];
  const int maxW = screen_.w - 2 * t.m.margin;
  const int w = t.dp(300) < maxW ? t.dp(300) : maxW;
  const int header = t.font.headline->lineHeight > t.dp(36) ? t.font.headline->lineHeight : t.dp(36);
  const int message = a.message[0] ? 2 * t.font.body->lineHeight : 0;
  const int h = 2 * t.m.padding + header + message + t.m.control + (message ? 2 : 1) * t.m.gapM;
  alertCard_ = layout::center(screen_, w, h);
  Rect parts[3];
  layout::column(alertCard_.inset(t.m.padding, t.m.padding), {layout::px(header), layout::px(message), layout::px(t.m.control)},
                 message ? t.m.gapM : 0, parts);
  if (!message) parts[2] = Rect(parts[2].x, parts[1].y + t.m.gapM, parts[2].w, parts[2].h);
  alertHeader_ = parts[0];
  alertMessage_ = parts[1];
  Button &primary = alertButtons_[0];
  Button &secondary = alertButtons_[1];
  primary.label = a.primary;
  primary.style = ButtonStyle::Accent;
  secondary.label = a.secondary;
  secondary.style = ButtonStyle::Glass;
  if (a.secondary[0]) {
    Rect b[2];
    layout::row(parts[2], {layout::flex(), layout::flex()}, t.m.gapM, b);
    secondary.bounds = b[0];
    primary.bounds = b[1];
  } else {
    primary.bounds = parts[2];
    secondary.bounds = Rect();
  }
}

void Runtime::drawAlert(Canvas &c) {
  const Theme &t = theme_;
  const Palette &p = t.color;
  const PendingAlert &a = alerts_[0];
  // A soft veil over everything, then the card.
  c.fillRect(screen_, p.shadow, p.dark ? 150 : 96);
  if (!c.visible(alertCard_.inset(-glassOutset(t)))) return;
  // Nearly opaque, so whatever is underneath never competes with the text.
  paintGlass(c, t, alertCard_, t.m.radiusCard);
  c.fillRoundRect(alertCard_.inset(1), t.m.radiusCard - 1, p.surface, 236);
  Rect title = alertHeader_;
  if (a.icon) {
    const int r = t.dp(18);
    paintBubble(c, t, title.x + r, title.centerY(), r, (uint8_t)(apps_[a.owner].app->info().tint & 3));
    a.icon(c, layout::center(Rect(title.x, title.y, 2 * r, title.h), t.dp(20), t.dp(20)), p.accent);
    title = Rect(title.x + 2 * r + t.m.gapM, title.y, title.w - 2 * r - t.m.gapM, title.h);
  }
  c.drawTextIn(*t.font.headline, a.title, title, p.text);
  if (a.message[0]) c.drawWrapped(*t.font.body, a.message, alertMessage_, p.textMuted, 2);
  alertButtons_[0].draw(c, t);
  if (a.secondary[0]) alertButtons_[1].draw(c, t);
}

}  // namespace stella
