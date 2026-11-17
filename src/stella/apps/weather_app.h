#pragma once
// Weather: current conditions and a short forecast from Open-Meteo (no API
// key). The fetching job runs on the service thread and publishes a
// WeatherSnapshot topic; the screens only read it. Last-good data stays on
// screen with an explicit freshness line when the network or service fails.
#include <atomic>

#include "../app/app.h"
#include "../service/data_status.h"

namespace stella {
namespace apps {

struct WeatherPlace {
  const char *name;
  float latitude, longitude;
  const char *timezone;  // POSIX zone this place is the default for
};
const WeatherPlace *weatherPlaces(int &count);

struct WeatherDay {
  int64_t date = 0;  // local midnight, epoch seconds
  int16_t maxC = 0, minC = 0;
  int16_t code = -1;
  uint8_t rainChance = 0;
};

struct WeatherReport {
  static constexpr int kDays = 4;
  bool valid = false;
  int16_t temperatureC = 0, feelsLikeC = 0;
  int16_t code = -1;
  bool day = true;
  uint8_t humidity = 0;
  uint16_t windKmh = 0;
  int64_t observed = 0;
  uint8_t dayCount = 0;
  uint8_t place = 0;
  WeatherDay days[kDays];
};

struct WeatherSnapshot {
  WeatherReport report;
  DataStatus status;
  uint8_t place = 0;  // place currently being fetched
};

// WMO weather code -> words and icon.
const char *weatherLabel(int code);
IconFn weatherIcon(int code, bool day);

namespace topics {
constexpr uint16_t kWeather = stella::topics::kFirstUser + 0;
}

class WeatherService : public Job, public JsonHandler {
 public:
  static constexpr uint32_t kFreshForMs = 45u * 60u * 1000u;

  explicit WeatherService(Topic<WeatherSnapshot> &topic) : topic_(topic) {}
  void attach(HttpService &http) { http_ = &http; }
  void setPlace(uint8_t place) { place_.store(place); }

  const char *name() const override { return "weather"; }
  JobPolicy policy() const override;
  JobResult run(uint32_t nowMs) override;
  void onValue(const JsonPath &path, const JsonValue &value) override;

 private:
  Topic<WeatherSnapshot> &topic_;
  HttpService *http_ = nullptr;
  std::atomic<uint8_t> place_{0};
  WeatherReport parsing_;
  bool seenTemperature_ = false, seenCode_ = false;
};

class WeatherApp : public App {
 public:
  WeatherApp();
  const AppInfo &info() const override;
  Screen &root() override { return main_; }
  void onInstall(ServiceHost &host) override;
  void onStart(AppContext &ctx) override;
  void onTopic(uint16_t topicId, AppContext &ctx) override;
  void onRoute(const char *route, AppContext &ctx) override;  // "places"
  uint32_t onWake(AppContext &ctx) override;
  WeatherSnapshot snapshot() const { return topic_.get(); }

 private:
  class MainScreen : public Screen {
   public:
    explicit MainScreen(WeatherApp &app) : app_(app) {}
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;
    void refresh(AppContext &ctx);  // re-read the snapshot and invalidate

   private:
    void describeFreshness(AppContext &ctx, char *out, size_t capacity) const;
    WeatherApp &app_;
    Rect hero_, icon_, temp_, condition_, place_, chips_[3], days_[WeatherReport::kDays], status_;
    int dayCards_ = 3;
    WeatherSnapshot snap_;
    char statusText_[48] = {0};
    char dayNames_[WeatherReport::kDays][8] = {};
    bool placePressed_ = false;
  };

  class PlaceScreen : public Screen {
   public:
    explicit PlaceScreen(WeatherApp &app) : app_(app) {}
    const char *title() const override { return "Location"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;

   private:
    static void drawPlace(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed, void *self);
    WeatherApp &app_;
    Rect card_;
    ListView list_;
  };

  void choosePlace(uint8_t place, AppContext &ctx);
  bool publishGlance(AppContext &ctx);  // "14° Overcast" for idle faces (the bedside clock); false if none

  Topic<WeatherSnapshot> topic_{topics::kWeather};
  WeatherService service_{topic_};
  MainScreen main_;
  PlaceScreen places_;
  uint8_t place_ = 0;
};

}  // namespace apps
}  // namespace stella
