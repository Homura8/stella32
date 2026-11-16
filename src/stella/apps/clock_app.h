#pragma once
// Bedside clock: large time, date and a moon / sky-phase chip.
#include "../app/app.h"

namespace stella {
namespace apps {

class ClockApp : public App {
 public:
  const AppInfo &info() const override;
  Screen &root() override { return screen_; }

 private:
  class ClockScreen : public Screen {
   public:
    void layout(const Rect &content, const Theme &theme) override;
    void draw(Canvas &c, const Theme &t) override;
    void onShow(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;

   private:
    void refresh(AppContext &ctx);
    Rect time_, date_, chip_;
    char timeText_[12] = {0}, suffix_[4] = {0}, dateText_[48] = {0}, chipText_[48] = {0};
    bool timeKnown_ = false;
  };

  ClockScreen screen_;
};

}  // namespace apps
}  // namespace stella
