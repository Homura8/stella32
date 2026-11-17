#pragma once
// The Stella runtime: the UI-thread "kernel" that owns the display, input,
// theme, navigation, shell chrome, power management, settings persistence,
// background service thread and supervision.
//
// Typical board sketch:
//   static stella::Runtime os;
//   os.begin(board, config);
//   os.install(launcher); os.install(clockApp); ...
//   os.start();
//   for (;;) os.run();          // or call step() from loop()
#include <atomic>
#include <stddef.h>
#include <stdint.h>

#include "../app/app.h"
#include "../gfx/renderer.h"
#include "../input/gesture.h"
#include "../net/http_service.h"
#include "../net/wifi_manager.h"
#include "../service/job_scheduler.h"
#include "../service/resource_lease.h"
#include "../service/supervisor.h"
#include "../service/topic.h"
#include "event_queue.h"

#ifndef STELLA_VERSION
#define STELLA_VERSION "0.3.1"
#endif

namespace stella {

struct RuntimeConfig {
  Color *renderBuffer = nullptr;   // chunk buffer (e.g. width * 20 pixels)
  size_t renderBufferPixels = 0;
  SystemSettings defaults;         // used when nothing is stored yet
  uint32_t activePollMs = 15;      // touch polling while a finger is down
  uint32_t idlePollMs = 30;        // touch polling otherwise (no touch IRQ)
  uint32_t sleepPollMs = 60;       // touch polling with the backlight off
  uint32_t uiStallLimitMs = 3u * 60u * 1000u;
  uint32_t serviceStallLimitMs = 10u * 60u * 1000u;
  bool supervise = true;           // restart on a stalled thread
  // TLS handshakes (mbedTLS) run on this stack; 16 KiB is what Celestia
  // Nightstand's network task used successfully on the CYD.
  uint32_t serviceStackBytes = 16384;
  int8_t serviceCore = 0;
  // false: no service thread; the host calls Runtime::serviceStep() itself
  // (deterministic simulation). Firmware always uses the thread.
  bool serviceThread = true;
  // Used once when no Wi-Fi network has been saved yet (e.g. from secrets.h).
  const char *wifiSsid = nullptr;
  const char *wifiPassword = nullptr;
  Color *renderBuffer2 = nullptr;  // optional second chunk buffer for async displays
};

enum class PowerState : uint8_t { Active, Dimmed, Off };

class Runtime : public AppContext, public TopicNotifier, public ServiceHost {
 public:
  static constexpr size_t kMaxApps = 16;
  static constexpr size_t kMaxDepth = 6;
  static constexpr size_t kMaxTopics = 16;
  static constexpr size_t kMaxAlerts = 4;

  Runtime();
  // Stops the service thread (host builds and tests; firmware never destroys it).
  ~Runtime() override;

  bool begin(const Board &board, const RuntimeConfig &config);
  bool install(App &app);                          // before start()
  bool addJob(Job &job, uint32_t firstDelayMs = 0) override;  // before start()
  bool attachTopic(TopicBase &topic) override;                // before start()
  HttpService &http() override { return http_; }
  const SystemSettings &systemSettings() const override { return settings_; }
  RecordStatus loadRecordFor(const char *appId, const char *key, uint16_t version, void *data,
                             size_t size) override {
    return loadRecord(board_.storage, appId, key, version, data, size);
  }
  bool start();

  // One UI iteration; returns how long the caller may sleep (ms).
  uint32_t step();
  // step() then sleep until woken or the returned delay elapses.
  void run();
  // One pass of the background scheduler (only with serviceThread = false).
  uint32_t serviceStep();

  // Thread-safe, for ports.
  Topic<NetworkStatus> &networkTopic() { return network_; }
  SharedResource &networkResource() { return networkResource_; }
  JobScheduler &jobs() { return jobs_; }
  Supervisor &supervisor() { return supervisor_; }
  void wake() { events_.wake(); }

  PowerState power() const { return power_; }
  App *foreground() const;
  Screen *topScreen() const;
  const Renderer::Stats &lastFrame() const { return lastStats_; }
  bool renderPending() const { return !dirty_.empty(); }
  // The alert on screen (tests and diagnostics).
  bool alertShowing() const { return alertCount_ > 0; }
  const char *alertTitle() const { return alertCount_ ? alerts_[0].title : ""; }
  Rect alertButtonRect(int which) const { return which == 0 ? alertButtons_[0].bounds : alertButtons_[1].bounds; }

  // AppContext.
  const Theme &theme() const override { return theme_; }
  Rect screenRect() const override { return screen_; }
  Rect contentRect() const override { return content_; }
  // Invalidations from a background app's callbacks are ignored: only the
  // foreground app may repaint the screen.
  void invalidate(const Rect &area) override {
    if (callbackApp_ && callbackApp_ != foreground()) return;
    dirty_.add(area);
  }
  void invalidateAll() override { dirty_.addAll(); }
  uint32_t millis() const override;
  LocalTime localTime() const override;
  void formatTime(const LocalTime &t, char *out, size_t capacity) const override;
  void requestTick() override {
    if (callbackApp_ && callbackApp_ != foreground()) return;
    tickArmed_ = topScreen() != nullptr;
    nextTickAt_ = millis();
  }
  bool push(Screen &screen) override;
  void pop() override;
  void goHome() override;
  bool launch(const char *appId, const char *route = nullptr) override;
  size_t appCount() const override { return appCount_; }
  const AppInfo *appAt(size_t index) const override;
  void toast(const char *text) override;
  const SystemSettings &settings() const override { return settings_; }
  void applySettings(const SystemSettings &settings) override;
  SystemStatus status() const override;
  NetworkControl &network() override { return wifi_; }
  bool showAlert(const Alert &alert) override;
  void dismissAlert(uint16_t tag) override;
  void scheduleWake(uint32_t delayMs) override;
  void requestJob(const Job &job) override { jobs_.requestRun(job); }
  RecordStatus loadAppRecord(const char *key, uint16_t version, void *data, size_t size) override;
  bool saveAppRecord(const char *key, uint16_t version, const void *data, size_t size) override;

  // TopicNotifier.
  void topicChanged(uint16_t topicId) override;

 private:
  struct AppSlot {
    App *app = nullptr;
    Screen *stack[kMaxDepth] = {};
    uint8_t depth = 0;
    bool started = false;
    bool wakeArmed = false;  // onWake() due at wakeAt
    uint32_t wakeAt = 0;
  };

  struct PendingAlert {
    char title[32];
    char message[96];
    char primary[20];
    char secondary[20];
    IconFn icon;
    uint16_t tag;
    int8_t owner;  // app index
    bool sound;
    uint32_t timeoutMs;
  };

  static void drawFrame(Canvas &canvas, void *self);
  static void serviceMain(void *self);
  static uint32_t clockThunk(void *self);

  void draw(Canvas &c);
  void drawStatusBar(Canvas &c);
  void drawToast(Canvas &c);
  void drawAlert(Canvas &c);
  void layoutAlert();
  void beginAlert(uint32_t now);
  void closeAlert(int action);
  void updateAlert(uint32_t now);
  void runWakes(uint32_t now);
  int callerIndex() const;
  Rect statusBarRect() const { return Rect(screen_.x, screen_.y, screen_.w, theme_.m.statusBar); }
  Rect backButtonRect() const;
  Rect toastRect() const;
  bool canGoBack() const;

  void updateTheme(bool force);
  void layoutTop();
  void showTop();
  void hideTop();
  void dispatchPointer(const Event &e);
  void handleTopicEvent(uint16_t id);
  void pollTouch(uint32_t now);
  void updatePower(uint32_t now);
  void applyBrightness();
  void superviseThreads(uint32_t now);
  AppSlot *slot(int index) { return index >= 0 && (size_t)index < appCount_ ? &apps_[index] : nullptr; }
  const AppSlot *slot(int index) const { return index >= 0 && (size_t)index < appCount_ ? &apps_[index] : nullptr; }

  Board board_;
  RuntimeConfig config_;
  Renderer renderer_{nullptr, 0};
  DirtyRegion dirty_;
  Theme theme_;
  Rect screen_, content_;
  GestureRecognizer gestures_;
  EventQueue events_;
  JobScheduler jobs_;
  SharedResource networkResource_;
  Supervisor supervisor_;
  Topic<NetworkStatus> network_{topics::kNetwork};
  Topic<WifiScan> scan_{topics::kWifiScan};
  WifiManager wifi_{network_, scan_};
  HttpService http_;
  TopicBase *topics_[kMaxTopics] = {};
  size_t topicCount_ = 0;

  AppSlot apps_[kMaxApps];
  size_t appCount_ = 0;
  int foreground_ = -1, home_ = -1;
  App *callbackApp_ = nullptr;  // app whose code is currently running (storage namespace)

  SystemSettings settings_;
  bool settingsDirty_ = false;
  uint32_t settingsSaveAt_ = 0;

  PowerState power_ = PowerState::Active;
  uint32_t lastInteraction_ = 0;
  uint8_t appliedBrightness_ = 0;
  bool brightnessKnown_ = false;

  bool pointerToShell_ = false;
  bool backPressed_ = false;
  uint32_t nextTickAt_ = 0;
  bool tickArmed_ = false;
  char toastText_[64] = {0};
  uint32_t toastUntil_ = 0;
  bool toastVisible_ = false;
  PendingAlert alerts_[kMaxAlerts] = {};
  size_t alertCount_ = 0;
  uint32_t alertShownAt_ = 0, chimeAt_ = 0;
  uint8_t chimeStep_ = 0;
  Rect alertCard_, alertHeader_, alertMessage_;
  Button alertButtons_[2];
  int lastMinute_ = -1;
  bool started_ = false, serviceRunning_ = false;
  int uiWatch_ = -1, serviceWatch_ = -1;
  std::atomic<bool> restartRequested_{false};
  std::atomic<bool> stopping_{false}, serviceExited_{false};
  std::atomic<uint32_t> uiStackFree_{0}, serviceStackFree_{0};
  osal::Signal serviceWake_;
  uint32_t frames_ = 0, lastFrameMs_ = 0, supervisorRestarts_ = 0;
  uint32_t nextHealthLog_ = 0;
  Renderer::Stats lastStats_;
};

}  // namespace stella
