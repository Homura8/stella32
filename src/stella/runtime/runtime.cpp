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

void applyTimezone(const char *posix) {
  setenv("TZ", posix, 1);
  tzset();
}

}  // namespace

Runtime::Runtime() {
  topics_[topicCount_++] = &network_;
  topics_[topicCount_++] = &scan_;
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
  applyTimezone(settings_.timezone);

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

  wifi_.configure(board.wifi, board.storage, &jobs_, config.wifiSsid, config.wifiPassword);
  if (wifi_.available()) jobs_.add(wifi_, millis());
  http_.configure(board.http, &networkResource_);
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
    invalidate(statusBarRect());
    updateTheme(false);
  }

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
  LocalTime out;
  int64_t epoch = 0;
  if (!board_.platform || !board_.platform->wallClock(epoch)) return out;
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
  const SkyPhase phase = lt.valid ? phaseForHour(lt.hour) : SkyPhase::Night;
  const bool dark = resolveDark(settings_.appearance, phase);
  const Density density = densityFor(screen_.w, screen_.h);
  if (!force && phase == theme_.phase && dark == theme_.color.dark && density == theme_.density) return;
  theme_ = makeTheme(density, dark, phase);
  content_ = Rect(screen_.x, screen_.y + theme_.m.statusBar, screen_.w, screen_.h - theme_.m.statusBar);
  if (board_.display) applyBrightness();
  GestureConfig g;
  g.slop = theme_.m.touchSlop;
  gestures_.configure(g);
  layoutTop();
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
  if (Screen *s = topScreen()) s->layout(content_, theme_);
}

void Runtime::showTop() {
  Screen *s = topScreen();
  if (!s) return;
  s->layout(content_, theme_);
  s->setVisible(true);
  callbackApp_ = foreground();
  s->onShow(*this);
  callbackApp_ = nullptr;
  tickArmed_ = true;
  nextTickAt_ = millis();
  invalidateAll();
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
    lastInteraction_ = now;
    if (power_ != PowerState::Active) {
      // The waking touch only wakes; it never reaches an app.
      power_ = PowerState::Active;
      applyBrightness();
      if (!gestures_.active()) gestures_.consumeCurrent();
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
  if (e.type == EventType::Press) pointerToShell_ = statusBarRect().contains(e.x, e.y);
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
  if (id == topics::kNetwork) invalidate(statusBarRect());
  for (size_t i = 0; i < appCount_; ++i) {
    if (!apps_[i].started) continue;
    callbackApp_ = apps_[i].app;
    apps_[i].app->onTopic(id, *this);
  }
  callbackApp_ = nullptr;
}

// ------------------------------------------------------------------ power

void Runtime::updatePower(uint32_t now) {
  if (gestures_.active()) return;
  const uint32_t idle = elapsedMs(now, lastInteraction_);
  PowerState next = PowerState::Active;
  if (settings_.sleepAfterSec && idle >= settings_.sleepAfterSec * 1000u) next = PowerState::Off;
  else if (settings_.dimAfterSec && idle >= settings_.dimAfterSec * 1000u) next = PowerState::Dimmed;
  if (next == power_ || (next == PowerState::Active)) return;  // only touch wakes
  if (power_ == PowerState::Off && next == PowerState::Dimmed) return;
  power_ = next;
  applyBrightness();
}

void Runtime::applyBrightness() {
  uint8_t level = settings_.brightness;
  // Bedside comfort: the night sky also caps the backlight.
  if (settings_.nightDim && theme_.phase == SkyPhase::Night) level = nightBrightness(level);
  if (power_ == PowerState::Dimmed) level = level / 4 < 12 ? 12 : level / 4;
  if (power_ == PowerState::Off) level = 0;
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

void Runtime::applySettings(const SystemSettings &s) {
  const bool timezoneChanged = strcmp(s.timezone, settings_.timezone) != 0;
  settings_ = s;
  settings_.sanitize();
  if (timezoneChanged) applyTimezone(settings_.timezone);
  settingsDirty_ = true;
  settingsSaveAt_ = millis() + kSettingsDebounceMs;
  applyBrightness();
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
  s.width = screen_.w;
  s.height = screen_.h;
  s.density = theme_.density;
  network_.read(s.network);
  s.supervisorRestarts = supervisorRestarts_;
  s.framesRendered = frames_;
  s.lastFrameMs = lastFrameMs_;
  s.uiStackFree = uiStackFree_.load();
  s.serviceStackFree = serviceStackFree_.load();
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
  paintSky(c, theme_, screen_);
  if (c.visible(statusBarRect())) drawStatusBar(c);
  Screen *s = topScreen();
  if (s && c.visible(content_)) {
    const Rect previous = c.pushClip(content_);
    s->draw(c, theme_);
    c.restoreClip(previous);
  }
  if (toastVisible_) drawToast(c);
}

void Runtime::drawStatusBar(Canvas &c) {
  const Theme &t = theme_;
  const Palette &p = t.color;
  const Rect bar = statusBarRect().inset(t.m.margin, 0);

  char clock[12];
  formatTime(localTime(), clock, sizeof(clock));
  const NetworkStatus net = network_.get();
  const int iconSize = t.dp(14);
  const int rightW = Canvas::textWidth(*t.font.caption, clock) + (net.present ? iconSize + t.dp(6) : 0);
  Rect parts[2];
  layout::row(bar, {layout::flex(), layout::px(rightW)}, t.m.gapM, parts);

  // Left: back affordance + title, or the Stella brand on the home screen.
  Rect left = parts[0];
  const bool back = canGoBack();
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
  const char *title = back ? (s && s->title() ? s->title() : app ? app->info().name : "") : "Stella";
  c.drawTextIn(*t.font.title, title, left, back ? p.text : p.accent);

  // Right: network indicator + clock.
  Rect right = parts[1];
  if (net.present) {
    // Settled: muted. Joining: accent. Problem: soft danger pink. Unset: faint.
    const Color wifiColor = net.state == WifiState::Connected    ? p.textMuted
                            : net.state == WifiState::Connecting ? p.accent
                            : net.state == WifiState::Disconnected ? p.danger
                                                                   : blend(p.glass, p.textMuted, 120);
    icons::wifi(c, layout::leading(right, iconSize), wifiColor);
    right = Rect(right.x + iconSize + t.dp(6), right.y, right.w - iconSize - t.dp(6), right.h);
  }
  c.drawTextIn(*t.font.caption, clock, right, p.textMuted, Align::Right);
}

void Runtime::drawToast(Canvas &c) {
  const Rect r = toastRect();
  if (!c.visible(r.inset(-glassOutset(theme_)))) return;
  paintGlass(c, theme_, r, r.h / 2);
  c.drawTextIn(*theme_.font.body, toastText_, r.inset(theme_.m.padding, 0), theme_.color.text, Align::Center);
}

}  // namespace stella
