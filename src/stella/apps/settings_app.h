#pragma once
// System settings: display, appearance, time and device information.
#include "../app/app.h"
#include "../app/text_entry.h"

namespace stella {
namespace apps {

// One labelled row of a ChoicesScreen.
struct ChoiceRow {
  const char *label;
  const char *const *choices;
  uint8_t count;
};

class SettingsApp : public App {
 public:
  SettingsApp();
  const AppInfo &info() const override;
  Screen &root() override { return main_; }
  void onTopic(uint16_t topicId, AppContext &ctx) override;
  void onRoute(const char *route, AppContext &ctx) override;
  // Where the choices of the Units and Idle face screens are (tests).
  Rect unitsChoice(int row, int i) const { return units_.choice(row, i); }
  Rect idleChoice(int row, int i) const { return idle_.choice(row, i); }
  Rect calendarRow(int i) const { return calendar_.rowBounds(i); }

 private:
  class MainScreen : public Screen {
   public:
    explicit MainScreen(SettingsApp &app) : app_(app) {}
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;

   private:
    static constexpr int kRows = 11;
    static void drawRow(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed, void *self);
    SettingsApp &app_;
    Rect card_;
    ListView list_;
    char wifi_[40] = {0}, appearance_[16] = {0}, time_[40] = {0}, sound_[16] = {0}, about_[24] = {0}, remoteDetail_[16] = {0}, unitsDetail_[8] = {0}, idleDetail_[16] = {0}, calendarDetail_[12] = {0};
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
    Button forget_, scan_, phone_;
    ListView list_;
    NetworkStatus net_;
    WifiScan networks_;
    char headline_[48] = {0}, detail_[48] = {0};
    char joining_[33] = {0};
  };

  // Wi-Fi setup from a phone: the device opens a protected access point
  // ("Stella-Setup") and a web page; this screen shows the name, the
  // temporary password and the address, and how it is going. It closes itself
  // when the phone's join succeeds.
  class SetupScreen : public Screen {
   public:
    explicit SetupScreen(SettingsApp &app) : app_(app) {}
    const char *title() const override { return "Phone setup"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    void onHide(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;
    void refresh(AppContext &ctx);

   private:
    SettingsApp &app_;
    Rect card_, labels_[3], values_[3], status_;
    Button cancel_;
    SetupInfo info_;
    NetworkStatus net_;
    char statusText_[72] = {0};
    bool sawSetup_ = false;
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
    Segmented dim_, off_;
    ListRow calibrate_, touch_;  // screen colours; touch test and calibration
    char calibrateDetail_[24] = {0};
    bool night_ = false;  // the night phase (by the clock) is on now
  };

  // Settings > Sound & light: the alert volume (letting go of the slider plays a
  // short chime at the new level), the status LED (mode, brightness, finding
  // which pins it is on) and ambient-light dimming (on/off, calibration).
  class SoundScreen : public Screen {
   public:
    const char *title() const override { return "Sound & light"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;

   private:
    static constexpr int kRows = 6;
    explicit SoundScreen(SettingsApp &app) : app_(app) {}
    friend class SettingsApp;
    SettingsApp &app_;
    Rect card_, rows_[kRows], labels_[kRows];
    Slider volume_, ledLevel_;
    Segmented ledMode_;
    Toggle autoBrightness_;
    ListRow findLed_, lightSensor_;
    char ledDetail_[24] = {0}, lightDetail_[24] = {0};
    bool changed_ = false;
  };

  // Settings > Sound & light > Find my LED: boards of the same name wire the
  // back LED differently, and it cannot be sensed, so the person is asked. It
  // lights each candidate pin in turn (both polarities if needed) and asks
  // which colour appeared; the answers become the wiring.
  class LedFindScreen : public Screen {
   public:
    const char *title() const override { return "Find my LED"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    void onHide(AppContext &ctx) override;

   private:
    enum class Phase : uint8_t { Intro, Asking, Found, NotFound };
    void probe(AppContext &ctx);
    void answer(int colour, AppContext &ctx);  // 0 red, 1 green, 2 blue, -1 none
    void setPhase(Phase p, AppContext &ctx);
    Rect body_, buttonRow_;
    int buttonGap_ = 0;
    Button buttons_[4];
    int buttonCount_ = 0;
    Phase phase_ = Phase::Intro;
    uint8_t pins_[8] = {};
    int pinCount_ = 0, index_ = 0;
    bool activeLow_ = true, polarityKnown_ = false, triedBoth_ = false;
    LedWiring found_;
    char text_[160] = {0};
  };

  // Settings > Sound & light > Light sensor: measures the sensor in its case
  // and room, with the screen's own light separated out (see ambient.h).
  class LightCalibrateScreen : public Screen {
   public:
    const char *title() const override { return "Light sensor"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    void onHide(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;

   private:
    enum class Phase : uint8_t { Intro, MeasuringDark, AskBright, MeasuringBright, Done, Failed };
    static constexpr int kDarkSteps = 4;
    void setPhase(Phase p, AppContext &ctx);
    void finish(AppContext &ctx);
    static constexpr int kBrightDuty = 128;
    Rect body_, bar_, buttonRow_;
    int buttonGap_ = 0, progress_ = 0;
    Button buttons_[2];
    int buttonCount_ = 0;
    Phase phase_ = Phase::Intro;
    int step_ = 0;           // which backlight step is being measured
    uint32_t stepStart_ = 0, nextSample_ = 0;
    uint32_t sum_ = 0;
    int samples_ = 0;
    float duty_[kDarkSteps] = {}, reading_[kDarkSteps] = {};
    float brightRaw_ = 0;
    char text_[160] = {0};
  };

  // Settings > Display > Calibrate: tune the panel until it matches a good
  // reference screen. Tone: contrast (per mode), white level, warmth.
  // Colour: palette tone, sky dithering. Panel: LCD VCOM and frame rate.
  // Each tab's Reset returns its values to the board's defaults.
  class CalibrateScreen : public Screen {
   public:
    const char *title() const override { return "Colours"; }
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

  // Settings > About > Software update: check, install, restart. The update
  // is signed and verified by the service (net/firmware_update.h); a new
  // image that doesn't run healthily is replaced by the previous one.
  class UpdateScreen : public Screen {
   public:
    const char *title() const override { return "Software update"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    void refresh(AppContext &ctx);

   private:
    Rect card_, rows_[2], bar_, status_, autoRow_;
    Toggle auto_;
    Button action_;
    UpdateStatus st_;
    char installed_[32] = {0}, offered_[40] = {0}, statusText_[128] = {0};
  };

  // A glass card of labelled segmented choices: Settings > Units and Settings >
  // Idle face. What each row means is data (the labels) and two small functions
  // (read a row from the settings, write it back).
  class ChoicesScreen : public Screen {
   public:
    using Row = ChoiceRow;
    using Get = int (*)(const SystemSettings &s, int row);
    using Set = void (*)(SystemSettings &s, int row, int value);
    void init(const char *title, const Row *rows, int count, Get get, Set set, const char *note = nullptr) {
      note_text_ = note;
      title_ = title;
      rows_ = rows;
      count_ = count;
      get_ = get;
      set_ = set;
    }
    const char *title() const override { return title_; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    Rect choice(int row, int i) const { return seg_[row].segment(i); }  // tests

   private:
    static constexpr int kMax = 3;
    const char *title_ = "";
    const char *note_text_ = nullptr;  // a few words under the card saying what it does
    Rect note_;
    const Row *rows_ = nullptr;
    int count_ = 0;
    Get get_ = nullptr;
    Set set_ = nullptr;
    Rect card_, rowRects_[kMax], labels_[kMax];
    Segmented seg_[kMax];
  };

  // Settings > Calendar: whether an address is set (and its host), a refresh and a
  // remove. The address itself is private and is entered on the remote page.
  class CalendarScreen : public Screen {
   public:
    explicit CalendarScreen(SettingsApp &app) : app_(app) {}
    const char *title() const override { return "Calendar"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    Rect rowBounds(int i) const { return rows_[i]; }  // tests

   private:
    static constexpr int kRows = 3;
    SettingsApp &app_;
    Rect card_, rows_[kRows], hint_;
    ListRow address_, fetch_, remove_;
    char host_[48] = {0};
    bool has_ = false;
  };

  // Settings > Web page: the web page on the home network (phone or PC, by
  // name or IP), its PIN, and the wallpaper it uploads. The PIN is shown only here.
  class RemoteScreen : public Screen {
   public:
    const char *title() const override { return "Web page"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    void refresh(AppContext &ctx);

   private:
    static constexpr int kRows = 5;
    Rect card_, rows_[kRows], labels_[kRows], hint_;
    Toggle enabled_;
    Button newPin_, remove_;
    Slider wash_;
    RemoteInfo info_;
    char pin_[8] = {0}, host_[48] = {0}, ip_[40] = {0}, wallpaper_[24] = {0};
  };

  // Settings > About > What's new (and once by itself after an update): the
  // changelog as a scrolling list, newest version first.
  class WhatsNewScreen : public Screen {
   public:
    const char *title() const override { return "What's new"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;

   private:
    static void drawRow(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed, void *self);
    Rect card_;
    ListView list_;
  };

  class AboutScreen : public Screen {
   public:
    explicit AboutScreen(SettingsApp &app) : app_(app) {}
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
    SettingsApp &app_;
    Rect card_;
    ListView list_;
    Button update_, whatsNew_;
    char values_[kRows][40] = {};
  };

  MainScreen main_;
  WifiScreen wifi_;
  SetupScreen setup_;
  TextEntryScreen password_;
  DisplayScreen display_;
  CalibrateScreen calibrate_;
  AppearanceScreen appearance_;
  TimeScreen time_;
  ChoicesScreen units_, idle_;
  SoundScreen sound_{*this};
  LedFindScreen ledFind_;
  LightCalibrateScreen light_;
  UpdateScreen softwareUpdate_;
  RemoteScreen remote_;
  CalendarScreen calendar_{*this};
  WhatsNewScreen whatsNew_;
  AboutScreen about_;
};

}  // namespace apps
}  // namespace stella
