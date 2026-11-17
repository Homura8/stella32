// Request safety: the simulated servers are as strict as the real ones, so a
// request that is cut off, misspelt or out of range fails here, in a test,
// instead of as "server error 400" on a device.
#include <string.h>

#include <string>

#include "sim_session.h"
#include "stella/apps/weather_app.h"
#include "test.h"

using namespace stella;
using namespace stella::host;
using namespace stella::apps;

namespace {

const char *kGood =
    "https://api.open-meteo.com/v1/forecast?latitude=-37.81&longitude=144.96"
    "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,is_day,wind_speed_10m"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
    "&hourly=temperature_2m,precipitation_probability,weather_code&forecast_hours=12"
    "&timezone=auto&forecast_days=4&timeformat=unixtime";

std::string rejection(const std::string &url) { return SimHttp::validateUrl(url.c_str()); }

}  // namespace

TEST(strict_server_accepts_the_real_request_and_every_place) {
  CHECK(rejection(kGood).empty());
  int count = 0;
  weatherPlaces(count);
  for (int place = 0; place < count; ++place) {
    char url[WeatherService::kUrlBytes];
    CHECK(WeatherService::buildUrl(url, sizeof(url), (uint8_t)place) > 0);
    CHECK(rejection(url).empty());
  }
}

TEST(strict_server_refuses_what_a_real_server_would) {
  // The 0.9.0 bug: the address cut off inside "timeformat=unixtime".
  const std::string good(kGood);
  CHECK(!rejection(good.substr(0, 383)).empty());
  CHECK(!rejection(good.substr(0, good.size() - 4)).empty());   // "...=unix"
  CHECK(!rejection(good.substr(0, good.size() - 14)).empty());  // "...&timeformat" / "=" missing
  CHECK(!rejection("http://api.open-meteo.com/v1/forecast?latitude=1&longitude=2").empty());
  CHECK(!rejection("https://api.open-meteo.com/v1/forecast?latitude=1&longitude=2&timezone=auto x").empty());
  CHECK(!rejection("https://api.open-meteo.com/v1/forecast?latitude=&longitude=2").empty());
  CHECK(!rejection("https://api.open-meteo.com/v1/forecast?latitude=1&longitude=2&bogus=1").empty());
  CHECK(!rejection("https://api.open-meteo.com/v1/forecast?latitude=1").empty());  // no longitude
  CHECK(!rejection("https://api.open-meteo.com/v1/forecast?latitude=91&longitude=2").empty());
  CHECK(!rejection("https://api.open-meteo.com/v1/forecast?latitude=1&longitude=2&hourly=temperature_2m,rain_chance").empty());
  CHECK(!rejection("https://api.open-meteo.com/v1/forecast?latitude=1&longitude=2&forecast_hours=0").empty());
  CHECK(!rejection("https://api.open-meteo.com/v1/forecast?latitude=1&longitude=2&forecast_days=17").empty());
  CHECK(!rejection("https://api.open-meteo.com/v1/forecast?latitude=1&longitude=2&timeformat=unix").empty());
  CHECK(!rejection("https:///v1/forecast").empty());
}

TEST(a_cut_off_request_is_answered_400_by_the_fake_network) {
  SimSession s(320, 240);
  const std::string good(kGood);
  s.dev.http.routes.insert(s.dev.http.routes.begin(), {"open-meteo", "{}", 200});
  struct Sink : HttpSink {
    bool onData(const uint8_t *, size_t) override { return true; }
  } sink;
  CHECK_EQ(s.dev.http.get(good.c_str(), 1000, 1000, sink), 200);
  CHECK_EQ(s.dev.http.get(good.substr(0, 383).c_str(), 1000, 1000, sink), 400);
  CHECK(!s.dev.http.lastRejection.empty());
}

// An update address that does not fit is refused, not silently shortened into
// a different (and wrong) address.
TEST(update_address_that_is_too_long_is_refused_not_truncated) {
  std::string longUrl = "https://updates.example/stella/";
  while (longUrl.size() < 170) longUrl += "very-long-path/";
  longUrl += "manifest.json";
  SimDevice dev(320, 240);
  dev.platform.setEpoch(kEveningEpoch);
  Runtime os;
  RuntimeConfig cfg;
  apps::LauncherApp launcher;
  cfg.renderBuffer = dev.buffer.data();
  cfg.renderBufferPixels = dev.buffer.size();
  cfg.serviceThread = false;
  cfg.updateManifestUrl = longUrl.c_str();
  os.begin(dev.board(), cfg);
  os.install(launcher);
  os.start();
  for (int i = 0; i < 20; ++i) {
    os.step();
    os.serviceStep();
    dev.platform.advance(50);
  }
  CHECK(os.update().status().state == UpdateState::Unavailable);
  CHECK(strstr(os.update().status().error, "too long") != nullptr);
  CHECK_EQ(dev.http.requests, 0);
}

// Whatever the clock says, an hour label stays a valid, short label.
TEST(weather_hour_labels_stay_in_range) {
  for (long epoch : {0L, 1L, 86399L, 1700000000L, 4102444800L}) {
    SimSession s(320, 240, epoch > 100 ? epoch : kEveningEpoch);
    s.os.launch("weather");
    s.run(200);
    CHECK(!s.os.renderPending());
  }
}
