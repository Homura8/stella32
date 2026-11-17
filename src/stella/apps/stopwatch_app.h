#pragma once
// Stopwatch: start, stop, resume, laps and reset. It runs on the monotonic
// clock (no Wi-Fi needed) and keeps counting while another app is in front;
// the screen shows tenths of a second while it is open and running.
//
// Laps are listed newest first with the lap time and the total; with three or
// more laps the quickest is accented and the slowest is marked.
#include "../app/app.h"

namespace stella {
namespace apps {

class StopwatchApp : public App {
 public:
  static constexpr int kMaxLaps = 30;
  static constexpr uint32_t kMaxMs = 100u * 60u * 60u * 1000u - 1u;  // 99:59:59.999, then it holds

  StopwatchApp();
  const AppInfo &info() const override;
  Screen &root() override { return screen_; }
  uint32_t onWake(AppContext &ctx) override;

  bool running() const { return running_; }
  uint32_t elapsed(uint32_t nowMs) const;
  int lapCount() const { return lapCount_; }
  // Total elapsed at lap `i` (0 = the first lap taken), and its own duration.
  uint32_t lapTotal(int i) const { return laps_[i]; }
  uint32_t lapTime(int i) const { return laps_[i] - (i ? laps_[i - 1] : 0); }

  // "MM:SS" or "H:MM:SS"; with `tenths`, "MM:SS.t" (below an hour).
  static void format(uint32_t ms, bool tenths, char *out, size_t capacity);

 private:
  class StopwatchScreen : public Screen {
   public:
    explicit StopwatchScreen(StopwatchApp &app) : app_(app) {}
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;
    void refresh(AppContext &ctx);

   private:
    static void drawLap(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed, void *self);
    StopwatchApp &app_;
    Rect digits_, mainRect_, tenthsRect_, card_, hint_;
    Button reset_, start_;
    ListView list_;
    char mainText_[12] = {0}, tenthsText_[4] = {0};
    int shownLaps_ = -1;
    int best_ = -1, worst_ = -1;  // lap indices (-1: not marked)
    bool hours_ = false;          // an hour or more: whole seconds, centred
    bool fresh_ = true;           // zero and no laps: the digits are muted
    bool shownRunning_ = false;   // what the empty-list hint says
  };

  void start(AppContext &ctx);
  void stop(AppContext &ctx);
  void lap(AppContext &ctx);
  void reset(AppContext &ctx);
  void fold(uint32_t nowMs);  // move the running segment into accumMs_

  bool running_ = false;
  uint32_t accumMs_ = 0;  // time counted before the current run
  uint32_t startAt_ = 0;  // millis() when the current run began
  int lapCount_ = 0;
  uint32_t laps_[kMaxLaps] = {};
  StopwatchScreen screen_;
};

}  // namespace apps
}  // namespace stella
