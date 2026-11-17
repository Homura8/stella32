#pragma once
// System settings: display, appearance, time and device information.
#include "../app/app.h"

namespace stella {
namespace apps {

class SettingsApp : public App {
 public:
  SettingsApp();
  const AppInfo &info() const override;
  Screen &root() override { return main_; }
  void onTopic(uint16_t topicId, AppContext &ctx) override;

 private:
  class MainScreen : public Screen {
   public:
    explicit MainScreen(SettingsApp &app) : app_(app) {}
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;

   private:
    SettingsApp &app_;
    Rect card_;
    ListRow rows_[4];
    char appearance_[16] = {0}, time_[40] = {0};
  };

  class DisplayScreen : public Screen {
   public:
    const char *title() const override { return "Display"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;

   private:
    Rect card_, brightnessLabel_, dimLabel_, offLabel_;
    Slider brightness_;
    Segmented dim_, off_;
  };

  class AppearanceScreen : public Screen {
   public:
    const char *title() const override { return "Appearance"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;

   private:
    Rect note_, skies_;
    Segmented mode_;
  };

  class TimeScreen : public Screen {
   public:
    const char *title() const override { return "Time"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;

   private:
    static void drawZone(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed, void *self);
    Rect toggleCard_, zoneLabel_, zoneCard_;
    Toggle clock24_;
    ListView zones_;
    int selected_ = 0;
  };

  class AboutScreen : public Screen {
   public:
    const char *title() const override { return "About"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;

   private:
    static constexpr int kRows = 8;
    static void drawRow(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed, void *self);
    void refresh(AppContext &ctx);
    Rect card_;
    ListView list_;
    char values_[kRows][40] = {};
  };

  MainScreen main_;
  DisplayScreen display_;
  AppearanceScreen appearance_;
  TimeScreen time_;
  AboutScreen about_;
};

}  // namespace apps
}  // namespace stella
