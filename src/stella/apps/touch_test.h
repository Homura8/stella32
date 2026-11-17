#pragma once
// Touch test and calibration. Test: draws the trail of recent touch points
// and corner targets, so a panel's calibration can be checked by eye (hold to
// clear; leaving clears too). Calibrate: tap four crosses; the fitted
// correction (scale and offset per axis) is saved in the system settings and
// applied by the runtime to every touch.
#include "../app/app.h"
#include "../core/containers.h"

namespace stella {
namespace apps {

// Least-squares fit of target = a * measured + b over n points, as the
// settings' touch correction (scale in 1/1024, offset in pixels). False when
// the points are degenerate or the result is implausible (> 20% scale or
// > 60 px offset): a mis-tap, not a calibration.
bool fitTouchAxis(const int *measured, const int *target, int n, int16_t &scale1024, int16_t &offset);

class TouchTestApp : public App {
 public:
  const AppInfo &info() const override;
  Screen &root() override { return screen_; }

 private:
  class TouchScreen : public Screen {
   public:
    const char *title() const override { return calibrating() ? "Calibrate touch" : "Touch test"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    void onHide(AppContext &ctx) override;

   private:
    static constexpr int kTrail = 48;
    bool calibrating() const { return step_ >= 0; }
    Point target(int i) const;  // calibration cross i (0..3), screen pixels
    void clear();
    void startCalibration(AppContext &ctx);
    void finishCalibration(AppContext &ctx);
    void cancelCalibration(AppContext &ctx);
    Theme theme_{};
    Rect pad_, readout_;
    Button calibrate_;
    Point trail_[kTrail];
    int head_ = 0, count_ = 0;
    Point last_{-1, -1};
    char text_[48] = {0};
    // Calibration: step 0..3 waits for cross `step_`; -1 when not calibrating.
    int step_ = -1;
    Point pressAt_{-1, -1};
    int measuredX_[4] = {}, measuredY_[4] = {};
    SystemSettings saved_;  // restored if calibration is abandoned
  };

  TouchScreen screen_;
};

}  // namespace apps
}  // namespace stella
