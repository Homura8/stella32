#pragma once
// Home screen: the time, greeting, date and a paged grid of app tiles. Which
// apps show, and in what order, is the person's choice (Settings > Home screen,
// or a long press on the time): it is saved as the "layout" record of this app.
#include "../app/app.h"
#include "../core/containers.h"

namespace stella {
namespace apps {

// What is saved: the app ids in the order the person chose, and whether each
// shows. Apps that are not in it (new ones) are added at the end, shown.
struct HomeLayoutRecord {
  static constexpr uint16_t kVersion = 1;
  static constexpr int kMax = 16;
  struct Item {
    char id[16];
    uint8_t visible;
    uint8_t reserved[3];
  };
  uint8_t count = 0;
  uint8_t reserved[3] = {};
  Item items[kMax] = {};
};

class LauncherApp : public App {
 public:
  LauncherApp();
  const AppInfo &info() const override;
  Screen &root() override { return home_; }
  void onStart(AppContext &ctx) override;
  void onRoute(const char *route, AppContext &ctx) override;  // "customise"
  void onTopic(uint16_t topicId, AppContext &ctx) override;
  // Where things are on the "Home screen" editor (for tests and screenshots).
  Rect editRow(int i) const { return custom_.rowRect(i); }
  Rect editUp(int i) const { return custom_.upRect(i); }
  Rect editDown(int i) const { return custom_.downRect(i); }
  Rect editToggle(int i) const { return custom_.toggleRect(i); }
  const HomeLayoutRecord &layout() const { return layout_; }
  // Where things are on the Home screen (for tests and screenshots).
  Rect tileBounds(int slot) const { return home_.tileRect(slot); }
  Rect timeBounds() const { return home_.timeRect(); }
  Rect pagerPrev() const { return home_.prevRect(); }
  Rect pagerNext() const { return home_.nextRect(); }
  bool headerFits() const { return home_.wordsFit(); }
  Rect noteBounds() const { return home_.noteRect(); }  // the invitation under the apps (tests, screenshots)
  Rect greetingRect() const { return home_.greetingRect(); }
  Rect dateRect() const { return home_.dateRect(); }

 private:
  class HomeScreen : public Screen {
   public:
    explicit HomeScreen(LauncherApp &app) : app_(app) {}
    void layout(const Rect &content, const Theme &theme) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;
    void refreshText(AppContext &ctx);
    Rect tileRect(int slot) const { return tile(slot); }
    Rect timeRect() const { return time_; }
    Rect prevRect() const { return prev_; }
    Rect nextRect() const { return next_; }
    Rect greetingRect() const { return greeting_; }
    Rect dateRect() const { return date_; }
    Rect noteRect() const { return note_; }
    bool wordsFit() const { return chooseWords(theme_).fits; }  // the header text is not cut off

   private:
    struct Words {
      const char *greeting = "", *date = "";
      const Font *greetingFont = nullptr, *dateFont = nullptr;
      bool fits = true;
    };
    Words chooseWords(const Theme &t) const;
    int perPage() const { return columns_ * rows_; }
    int pageCount() const;
    Rect tile(int slot) const;
    int tileAt(int x, int y) const;  // app index under a point on the current page, or -1
    void arrange();  // place tiles and the note for the current app count
    bool showPage(int page, AppContext &ctx);

    LauncherApp &app_;
    FixedVector<const AppInfo *, 16> apps_;
    Theme theme_{};
    Rect area_, header_, time_, greeting_, date_, grid_, dots_, note_;
    Rect pager_, prev_, next_;  // page buttons (resistive panels make swiping hard)
    bool pagerBelow_ = false;   // in the row of page dots, when the header is too narrow for them
    int columns_ = 3, rows_ = 2, gap_ = 8, page_ = 0, pressed_ = -1;
    char timeText_[16] = {0}, greetingText_[32] = {0}, greetingShort_[16] = {0}, dateShort_[24] = {0}, dateText_[48] = {0}, noteTitle_[32] = {0}, noteText_[48] = {0};
    bool setupNote_ = false, notePressed_ = false;  // the note invites something: Wi-Fi setup or choosing a time zone
    bool noteZone_ = false;                          // ... and this is the time zone one
    bool twelveHour_ = false;                       // the time has an AM/PM
    bool prevPressed_ = false, nextPressed_ = false;
  };

  // Settings > Home screen: every app with a switch to show or hide it and
  // arrows to move it. Settings itself always stays (or there would be no way back).
  class CustomiseScreen : public Screen {
   public:
    explicit CustomiseScreen(LauncherApp &app) : app_(app) {}
    const char *title() const override { return "Home screen"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    Rect rowRect(int i) const;
    Rect upRect(int i) const;
    Rect downRect(int i) const;
    Rect toggleRect(int i) const;

   private:
    static void drawItem(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed, void *self);
    Rect controlsOf(const Rect &row, int which) const;  // 0 up, 1 down, 2 toggle
    void refreshInfos(AppContext &ctx);
    LauncherApp &app_;
    Rect card_;
    ListView list_;
    Theme theme_{};
    FixedVector<const AppInfo *, 16> infos_;  // the app behind each row (name, icon, colour)
  };

  // The saved order merged with the apps that exist: every app once, in the
  // person's order, new ones last.
  void syncLayout(AppContext &ctx);
  void save(AppContext &ctx);
  const AppInfo *find(AppContext &ctx, const char *id) const;

  HomeLayoutRecord layout_;
  HomeScreen home_;
  CustomiseScreen custom_;
};

}  // namespace apps
}  // namespace stella
