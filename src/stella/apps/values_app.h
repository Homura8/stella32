#pragma once
// Values: live numbers pushed from a script on the home network (the remote
// page's `POST /api/push`, see net/values.h): a PC's CPU and temperatures, a Home
// Assistant sensor, the state of a CI run. Up to eight tiles, each with its name
// and value; a tile that has not been refreshed for ten minutes dims and says how
// long ago it last arrived. Nothing is kept across a restart.
#include "../app/app.h"

namespace stella {
namespace apps {

class ValuesApp : public App {
 public:
  ValuesApp() : screen_(*this) {}
  const AppInfo &info() const override;
  Screen &root() override { return screen_; }
  void onTopic(uint16_t topicId, AppContext &ctx) override;
  static constexpr uint32_t kStaleMs = 10u * 60u * 1000u;
  // For tests and screenshots.
  Rect tile(int i) const { return screen_.tileBounds(i); }
  int tileCount() const { return screen_.count(); }

 private:
  class ValuesScreen : public Screen {
   public:
    explicit ValuesScreen(ValuesApp &app) : app_(app) {}
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;
    void refresh(AppContext &ctx);
    Rect tileBounds(int i) const { return tiles_[i]; }
    int count() const { return values_.count; }

   private:
    void arrange();
    ValuesApp &app_;
    Rect content_, tiles_[kMaxPushedValues], message_;
    Theme theme_{};
    PushedValues values_;
    char ageText_[kMaxPushedValues][14] = {};
    bool stale_[kMaxPushedValues] = {};
    char hint_[160] = {0};
  };

  ValuesScreen screen_;
};

}  // namespace apps
}  // namespace stella
