#pragma once
// System settings: display, appearance, time and device information.
#include "../app/app.h"
#include "../app/text_entry.h"

namespace stella {
namespace apps {

class SettingsApp : public App {
 public:
  SettingsApp();
  const AppInfo &info() const override;
  Screen &root() override { return main_; }
  void onTopic(uint16_t topicId, AppContext &ctx) override;
  void onRoute(const char *route, AppContext &ctx) override;

 private:
  class MainScreen : public Screen {
   public:
    explicit MainScreen(SettingsApp &app) : app_(app) {}
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;

   private:
    static constexpr int kRows = 5;
    SettingsApp &app_;
    Rect card_;
    ListRow rows_[kRows];
    char wifi_[40] = {0}, appearance_[16] = {0}, time_[40] = {0};
  };

  class WifiScreen : public Screen {
   public:
    explicit WifiScreen(SettingsApp &app) : app_(app) {}
    const char *title() const override { return "Wi-Fi"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    void refresh(AppContext &ctx);

   private:
    static void drawNetwork(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed, void *self);
    static void submitPassword(const char *text, void *self, AppContext &ctx);
    SettingsApp &app_;
    Rect statusCard_, statusText_, statusIcon_, header_, listCard_;
    Button forget_, scan_;
    ListView list_;
    NetworkStatus net_;
    WifiScan networks_;
    char headline_[48] = {0}, detail_[48] = {0};
    char joining_[33] = {0};
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
    Rect note_, nightCard_, skies_;
    Segmented mode_;
    Toggle nightDim_;
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
    static constexpr int kRows = 10;
    static void drawRow(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed, void *self);
    void refresh(AppContext &ctx);
    Rect card_;
    ListView list_;
    char values_[kRows][40] = {};
  };

  MainScreen main_;
  WifiScreen wifi_;
  TextEntryScreen password_;
  DisplayScreen display_;
  AppearanceScreen appearance_;
  TimeScreen time_;
  AboutScreen about_;
};

}  // namespace apps
}  // namespace stella
