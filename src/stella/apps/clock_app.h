#pragma once
// Bedside clock: large time, date and a moon / sky-phase chip, plus the
// bedside mode: a dim fullscreen clock on a flat dark background with the
// date and the next alarm. It starts from the Bedside button, or by itself
// after the idle dim at night (Settings > Display > Bedside); a tap leaves it.
#include "../app/app.h"

namespace stella {
namespace apps {

// The bedside clock's animation frame (0..3) at a time, and the delay to the
// next one: a 16 s cycle, bit 1 = twinkle, bit 0 = the moon's blink.
int bedsideFrameAt(uint32_t ms);
uint32_t bedsideMsToNextFrame(uint32_t ms);

class ClockApp : public App {
 public:
  ClockApp();
  const AppInfo &info() const override;
  Screen &root() override { return screen_; }
  void onRoute(const char *route, AppContext &ctx) override;  // "bedside", "dashboard"
  Screen &bedsideFace() { return bedsideScreen_; }      // for tests
  Screen &dashboardFace() { return dashboardScreen_; }  // for tests

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

  // The idle face (after Celestia Nightstand's bedside mode): a black sky with
  // faint stars, a sleepy crescent moon and a drifting planet, a big lilac
  // clock, the weather and the next alarm. It covers the whole display (no
  // chrome, no sky gradient) and animates calmly: every ~4 s a few stars
  // twinkle and the moon bobs (and blinks once per 16 s); only those spots
  // are repainted.
  class BedsideScreen : public Screen {
   public:
    bool fullscreen() const override { return true; }
    void layout(const Rect &area, const Theme &theme) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;

    static constexpr int kStars = 10;

   private:
    void refresh(AppContext &ctx);
    void animate(AppContext &ctx, int frame);
    static int starLevel(int star, int frame);  // 0 faint, 1 soft
    Rect starBox(int star) const;
    void drawMoon(Canvas &c, const Theme &t) const;
    void drawSky(Canvas &c, const Theme &t) const;
    Rect area_, date_, clock_, divider_, glanceRow_, alarmRow_, sunRow_, footer_;
    const Font *clockFont_ = nullptr;
    Point stars_[kStars];
    Point moon_, bigSparkle_, smallSparkle_, planet_;
    int moonR_ = 12, starSize_ = 1, sparkleBig_ = 14, sparkleSmall_ = 10;
    int frame_ = 0;
    char timeText_[12] = {0}, suffix_[4] = {0}, dateText_[48] = {0}, alarmText_[48] = {0}, sunText_[32] = {0};
    Glance glance_;
    bool timeKnown_ = false, hasAlarm_ = false;
  };

  // The dashboard idle face (Settings > Idle face > Dashboard): the time, the
  // date and up to three quiet lines (the weather, the next alarm, the next
  // sunrise or sunset) on the same dim black as the bedside clock, but with no
  // animation at all, so it repaints once a minute and only where something
  // changed. A tap leaves it.
  class DashboardScreen : public Screen {
   public:
    bool fullscreen() const override { return true; }
    void layout(const Rect &area, const Theme &theme) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;

   private:
    static constexpr int kLines = 3;  // weather, alarm, sun
    void refresh(AppContext &ctx);
    Rect area_, date_, clock_, lines_[kLines], footer_;
    const Font *clockFont_ = nullptr;
    char timeText_[12] = {0}, suffix_[4] = {0}, dateText_[48] = {0};
    char lineText_[kLines][48] = {};
    IconFn weatherIcon_ = nullptr;
    bool timeKnown_ = false;
  };

  ClockScreen screen_;
  BedsideScreen bedsideScreen_;
  DashboardScreen dashboardScreen_;
};

}  // namespace apps
}  // namespace stella
