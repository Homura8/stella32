#pragma once
// Timer: a countdown with presets, +1 minute, pause and reset. It runs on the
// monotonic clock (no Wi-Fi needed) and rings through a system alert when it
// finishes, whichever app is in front.
#include "../app/app.h"

namespace stella {
namespace apps {

class TimerApp : public App {
 public:
  static constexpr uint16_t kDoneTag = 1;
  static constexpr uint32_t kMaxMs = 3u * 60u * 60u * 1000u;  // 3 hours
  static constexpr uint32_t kRingMs = 2u * 60u * 1000u;

  TimerApp();
  const AppInfo &info() const override;
  Screen &root() override { return screen_; }
  uint32_t onWake(AppContext &ctx) override;
  void onAlert(uint16_t tag, int action, AppContext &ctx) override;

  bool running() const { return running_; }
  uint32_t remaining(uint32_t nowMs) const;
  uint32_t duration() const { return durationMs_; }

 private:
  class TimerScreen : public Screen {
   public:
    explicit TimerScreen(TimerApp &app) : app_(app) {}
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;
    void refresh(AppContext &ctx);

   private:
    TimerApp &app_;
    Rect digits_, progress_, caption_;
    Segmented presets_;
    Button reset_, plus_, start_;
    char digitsText_[12] = {0}, captionText_[40] = {0};
    int progressW_ = -1;
  };

  void setDuration(uint32_t ms, AppContext &ctx);
  void start(AppContext &ctx);
  void pause(AppContext &ctx);
  void reset(AppContext &ctx);
  void addMinute(AppContext &ctx);

  uint32_t durationMs_ = 5u * 60u * 1000u;
  uint32_t remainingMs_ = 5u * 60u * 1000u;  // while not running
  uint32_t endAt_ = 0;                        // while running (millis)
  bool running_ = false;
  TimerScreen screen_;
};

}  // namespace apps
}  // namespace stella
