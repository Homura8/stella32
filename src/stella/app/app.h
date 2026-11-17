#pragma once
// The application model: what an app is, and everything it may ask of the OS.
//
//  * An App owns one or more Screens. Navigation is a per-app screen stack
//    (push for detail views, pop to go back). The shell supplies the status
//    bar, back navigation, toasts and the sky backdrop.
//  * All app code runs on the single UI thread. Background work belongs in
//    service Jobs; results reach apps through Topics (onTopic()).
//  * Apps are statically allocated objects registered with the runtime at
//    boot. They never touch hardware: the AppContext is their only door.
#include <stddef.h>
#include <stdint.h>

#include "../gfx/canvas.h"
#include "../hal/hal.h"
#include "../input/event.h"
#include "../net/http_service.h"
#include "../net/network_status.h"
#include "../service/job_scheduler.h"
#include "../service/topic.h"
#include "../ui/icons.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"
#include "system_settings.h"

namespace stella {

class AppContext;

constexpr uint32_t kNoTick = 0xFFFFFFFFu;

class Screen {
 public:
  virtual ~Screen() = default;
  // Status bar title; nullptr uses the app name.
  virtual const char *title() const { return nullptr; }
  // Compute every rectangle this screen uses from `content` and the theme.
  // Called before the first draw and whenever the theme or display changes.
  // This is the one place to edit when changing the screen's layout.
  virtual void layout(const Rect &content, const Theme &theme) = 0;
  // Paint the content area. The sky backdrop is already painted underneath;
  // draw only what this screen adds. Must be deterministic (no state changes).
  virtual void draw(Canvas &canvas, const Theme &theme) = 0;
  // Input and system events. Return true if handled.
  virtual bool onEvent(const Event &event, AppContext &ctx) {
    (void)event;
    (void)ctx;
    return false;
  }
  virtual void onShow(AppContext &ctx) { (void)ctx; }  // became visible
  virtual void onHide(AppContext &ctx) { (void)ctx; }  // covered or left
  // Periodic work while visible: return ms until the next tick, or kNoTick.
  virtual uint32_t onTick(AppContext &ctx) {
    (void)ctx;
    return kNoTick;
  }
  // True between onShow and onHide (maintained by the runtime).
  bool isVisible() const { return visible_; }
  void setVisible(bool visible) { visible_ = visible; }  // runtime only

 private:
  bool visible_ = false;
};

// A system alert: a modal glass card over everything, which wakes the display
// and (with `sound`) plays the alert chime until answered. The owning app is
// told the outcome in App::onAlert(). Strings are copied when shown.
constexpr int kAlertPrimary = 0;
constexpr int kAlertSecondary = 1;
constexpr int kAlertTimedOut = -1;

struct Alert {
  const char *title = "";           // short, e.g. "7:00" or "Time's up"
  const char *message = "";         // one or two lines
  IconFn icon = nullptr;
  const char *primary = "OK";       // accent button
  const char *secondary = nullptr;  // optional glass button
  uint16_t tag = 0;                 // app-defined; identifies the alert to its app
  bool sound = false;
  uint32_t timeoutMs = 0;           // 0 = until answered
};

enum AppFlags : uint8_t {
  kAppHome = 1 << 0,    // the launcher; shown at boot and by "home"
  kAppHidden = 1 << 1,  // not listed in the launcher
};

struct AppInfo {
  const char *id;    // stable, <= 15 chars (also its storage namespace)
  const char *name;  // shown in the launcher and status bar
  IconFn icon;
  uint8_t tint;      // launcher bubble colour: Palette::tints index (0..3)
  uint8_t flags;
};

// Lets an app register background work while it is installed (before the
// system starts). Jobs run on the service thread and publish Topics; they must
// not touch the UI or AppContext.
class ServiceHost {
 public:
  virtual ~ServiceHost() = default;
  virtual bool addJob(Job &job, uint32_t firstDelayMs = 0) = 0;
  virtual bool attachTopic(TopicBase &topic) = 0;
  virtual HttpService &http() = 0;
  // Read-only access to persisted state while installing (e.g. a saved place).
  virtual const SystemSettings &systemSettings() const = 0;
  virtual RecordStatus loadRecordFor(const char *appId, const char *key, uint16_t version, void *data,
                                     size_t size) = 0;
};

class App {
 public:
  virtual ~App() = default;
  virtual const AppInfo &info() const = 0;
  virtual Screen &root() = 0;
  virtual void onInstall(ServiceHost &host) { (void)host; }  // register jobs/topics
  virtual void onStart(AppContext &ctx) { (void)ctx; }  // once, before the first show
  // Deep link: launched with a route (e.g. "wifi"); push the matching screen.
  virtual void onRoute(const char *route, AppContext &ctx) {
    (void)route;
    (void)ctx;
  }
  virtual void onTopic(uint16_t topicId, AppContext &ctx) {
    (void)topicId;
    (void)ctx;
  }
  // Background timer for every installed app, foreground or not (alarms,
  // timers). First called right after the system starts, possibly before
  // onStart(); return ms until the next call, or kNoTick. Keep it short: it
  // runs on the UI thread. Repaints from a background app are ignored.
  virtual uint32_t onWake(AppContext &ctx) {
    (void)ctx;
    return kNoTick;
  }
  // The user answered one of this app's alerts (kAlertPrimary/Secondary) or
  // it timed out (kAlertTimedOut). Not called for dismissAlert().
  virtual void onAlert(uint16_t tag, int action, AppContext &ctx) {
    (void)tag;
    (void)action;
    (void)ctx;
  }
};

struct LocalTime {
  bool valid = false;  // false until the wall clock is known
  int64_t epoch = 0;
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, weekday = 0, yearDay = 0;
};

struct SystemStatus {
  const char *board = "";
  const char *version = "";
  uint32_t uptimeMs = 0;
  MemoryStats memory;
  ResetCause resetCause = ResetCause::Unknown;
  int16_t width = 0, height = 0;
  Density density = Density::Compact;
  NetworkStatus network;
  uint32_t supervisorRestarts = 0;
  uint32_t framesRendered = 0;
  uint32_t lastFrameMs = 0;  // render time of the most recent frame
  uint32_t uiStackFree = 0, serviceStackFree = 0;  // lowest free stack seen, bytes
};

class AppContext : public Invalidator {
 public:
  // Display and theme.
  virtual const Theme &theme() const = 0;
  virtual Rect screenRect() const = 0;
  virtual Rect contentRect() const = 0;
  virtual void invalidateAll() = 0;

  // Time.
  virtual uint32_t millis() const = 0;
  virtual LocalTime localTime() const = 0;
  virtual void formatTime(const LocalTime &t, char *out, size_t capacity) const = 0;  // honours 12/24 h
  // Run the top screen's onTick() again soon (after it returned kNoTick).
  // Ignored for a background app.
  virtual void requestTick() = 0;

  // Navigation.
  virtual bool push(Screen &screen) = 0;
  virtual void pop() = 0;
  virtual void goHome() = 0;
  virtual bool launch(const char *appId, const char *route = nullptr) = 0;
  virtual size_t appCount() const = 0;
  virtual const AppInfo *appAt(size_t index) const = 0;

  // System.
  virtual void toast(const char *text) = 0;
  virtual const SystemSettings &settings() const = 0;
  // Apply new settings (theme, brightness, timezone...) and persist them soon.
  virtual void applySettings(const SystemSettings &settings) = 0;
  virtual SystemStatus status() const = 0;
  virtual NetworkControl &network() = 0;
  // Show a system alert owned by the calling app; one with the same tag is
  // replaced. Alerts queue (a few) while another is showing. False if full.
  virtual bool showAlert(const Alert &alert) = 0;
  // Withdraw the calling app's alert with this tag (shown or queued) without
  // an onAlert() callback.
  virtual void dismissAlert(uint16_t tag) = 0;
  // Run the calling app's onWake() after `delayMs` (sooner than it asked).
  // Each onWake() return value then sets the next wake, so an app with
  // several deadlines returns the nearest one.
  virtual void scheduleWake(uint32_t delayMs) = 0;
  // Ask the service thread to run a background job now (e.g. "refresh").
  virtual void requestJob(const Job &job) = 0;
  // Per-app persistent records, namespaced by AppInfo::id.
  virtual RecordStatus loadAppRecord(const char *key, uint16_t version, void *data, size_t size) = 0;
  virtual bool saveAppRecord(const char *key, uint16_t version, const void *data, size_t size) = 0;
};

}  // namespace stella
