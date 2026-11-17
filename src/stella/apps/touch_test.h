#pragma once
// Touch calibration aid: draws the trail of recent touch points and corner
// targets, so a new panel's calibration can be checked by eye. Long-press clears.
#include "../app/app.h"
#include "../core/containers.h"

namespace stella {
namespace apps {

class TouchTestApp : public App {
 public:
  const AppInfo &info() const override;
  Screen &root() override { return screen_; }

 private:
  class TouchScreen : public Screen {
   public:
    const char *title() const override { return "Touch test"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;

   private:
    static constexpr int kTrail = 48;
    Rect pad_, readout_;
    Point trail_[kTrail];
    int head_ = 0, count_ = 0;
    Point last_{-1, -1};
    char text_[40] = "Touch anywhere \xc2\xb7 hold to clear";
  };

  TouchScreen screen_;
};

}  // namespace apps
}  // namespace stella
