#pragma once
// Home screen: greeting, date and a paged grid of app tiles.
#include "../app/app.h"
#include "../core/containers.h"

namespace stella {
namespace apps {

class LauncherApp : public App {
 public:
  const AppInfo &info() const override;
  Screen &root() override { return home_; }
  void onTopic(uint16_t topicId, AppContext &ctx) override;
  // Where things are on the Home screen (for tests and screenshots).
  Rect tileBounds(int slot) const { return home_.tileRect(slot); }
  Rect pagerPrev() const { return home_.prevRect(); }
  Rect pagerNext() const { return home_.nextRect(); }

 private:
  class HomeScreen : public Screen {
   public:
    void layout(const Rect &content, const Theme &theme) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;
    void refreshText(AppContext &ctx);
    Rect tileRect(int slot) const { return tile(slot); }
    Rect prevRect() const { return prev_; }
    Rect nextRect() const { return next_; }

   private:
    int perPage() const { return columns_ * rows_; }
    int pageCount() const;
    Rect tile(int slot) const;
    int tileAt(int x, int y) const;  // app index under a point on the current page, or -1
    void arrange();  // place tiles and the note for the current app count
    bool showPage(int page, AppContext &ctx);

    FixedVector<const AppInfo *, 16> apps_;
    Theme theme_{};
    Rect area_, header_, time_, greeting_, date_, grid_, dots_, note_;
    Rect pager_, prev_, next_;  // page buttons (resistive panels make swiping hard)
    int columns_ = 3, rows_ = 2, gap_ = 8, page_ = 0, pressed_ = -1;
    char timeText_[16] = {0}, greetingText_[32] = {0}, dateText_[48] = {0}, noteTitle_[32] = {0}, noteText_[48] = {0};
    bool setupNote_ = false, notePressed_ = false;  // note invites Wi-Fi setup
    bool prevPressed_ = false, nextPressed_ = false;
  };

  HomeScreen home_;
};

}  // namespace apps
}  // namespace stella
