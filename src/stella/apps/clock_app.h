#pragma once
// Bedside clock: large time, date and a moon / sky-phase chip, plus the
// bedside mode: a dim fullscreen clock on a flat dark background with the
// date and the next alarm. It starts from the Bedside button, or by itself
// after the idle dim at night (Settings > Display > Bedside); a tap leaves it.
#include "../app/app.h"

namespace stella {
namespace apps {

class ClockApp : public App {
 public:
  ClockApp();
  const AppInfo &info() const override;
  Screen &root() override { return screen_; }
  void onRoute(const char *route, AppContext &ctx) override;  // "bedside"

 private:
  class ClockScreen : public Screen {
   public:
    explicit ClockScreen(ClockApp &app) : app_(app) {}
    void layout(const Rect &content, const Theme &theme) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;

   private:
    void refresh(AppContext &ctx);
    ClockApp &app_;
    Rect time_, date_, chip_;
    Button bedside_;
    char timeText_[12] = {0}, suffix_[4] = {0}, dateText_[48] = {0}, chipText_[48] = {0};
    bool timeKnown_ = false;
  };

  // The idle face: covers the whole display (no sky, no status bar), dim.
  class BedsideScreen : public Screen {
   public:
    bool fullscreen() const override { return true; }
    void layout(const Rect &area, const Theme &theme) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;

   private:
    void refresh(AppContext &ctx);
    Rect area_, time_, date_, alarm_;
    char timeText_[12] = {0}, suffix_[4] = {0}, dateText_[48] = {0}, alarmText_[48] = {0};
    bool timeKnown_ = false, hasAlarm_ = false;
  };

  ClockScreen screen_;
  BedsideScreen bedsideScreen_;
};

}  // namespace apps
}  // namespace stella
