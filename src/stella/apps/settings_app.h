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

  // One glass card of labelled rows: brightness, night dimming, idle
  // timeouts, and the way into calibration.
  class DisplayScreen : public Screen {
   public:
    explicit DisplayScreen(SettingsApp &app) : app_(app) {}
    const char *title() const override { return "Display"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;

   private:
    static constexpr int kRows = 6;
    SettingsApp &app_;
    Rect card_, rows_[kRows], labels_[kRows];
    Slider brightness_;
    Toggle nightDim_;
    Segmented dim_, off_, bedside_;
    ListRow calibrate_;
    char calibrateDetail_[24] = {0};
    bool night_ = false;  // the night phase (by the clock) is on now
  };

  // Settings > Display > Calibrate: tune the panel until it matches a good
  // reference screen. Tone: contrast (per mode), white level, warmth.
  // Colour: palette tone, sky dithering. Panel: LCD VCOM and frame rate.
  // Each tab's Reset returns its values to the board's defaults.
  class CalibrateScreen : public Screen {
   public:
    const char *title() const override { return "Calibrate"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;

   private:
    void arrange();
    void load(const SystemSettings &s);
    int rowCount() const { return tabs_.selected == 0 ? 3 : 2; }
    Theme theme_{};
    Rect content_, strip_, note_, rows_[3], labels_[3];
    Segmented tabs_, tone_, refresh_;
    Slider contrast_, whites_, warmth_, vcom_;
    Toggle dither_;
    Button reset_;
    bool panelTrim_ = false, dark_ = false;
  };

  class AppearanceScreen : public Screen {
   public:
    const char *title() const override { return "Appearance"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;

   private:
    Rect note_, skies_, skyNote_;
    Segmented mode_;
    uint8_t skyPin_ = 0;
    int pressedSky_ = -1;
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
  CalibrateScreen calibrate_;
  AppearanceScreen appearance_;
  TimeScreen time_;
  AboutScreen about_;
};

}  // namespace apps
}  // namespace stella
