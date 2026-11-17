// End-to-end flows driven only by touch: Wi-Fi onboarding with the on-screen
// keyboard, weather loading and failure, deep links, background repaint rules.
#include <string.h>

#include "sim_session.h"
#include "stella/app/text_entry.h"
#include "stella/ui/keyboard.h"
#include "test.h"

using namespace stella;
using namespace stella::host;
using stella::apps::WeatherService;
using stella::apps::WeatherSnapshot;

namespace {

void tapRect(SimSession &s, const Rect &r) { s.tap(r.centerX(), r.centerY()); }

const char *topTitle(SimSession &s) {
  Screen *top = s.os.topScreen();
  return top && top->title() ? top->title() : "";
}

// Types `text` on the password screen's keyboard by touch.
void typeOn(SimSession &s, const char *text) {
  auto *entry = static_cast<TextEntryScreen *>(s.os.topScreen());
  for (const char *p = text; *p; ++p) {
    const Rect key = entry->keyboard().keyFor(*p);
    CHECK(!key.empty());
    tapRect(s, key);
  }
}

struct Sink : Invalidator {
  void invalidate(const Rect &) override {}
};

Event at(EventType type, const Rect &r) {
  Event e;
  e.type = type;
  e.x = e.startX = (int16_t)r.centerX();
  e.y = e.startY = (int16_t)r.centerY();
  return e;
}

}  // namespace

TEST(keyboard_modes_shift_and_actions) {
  Sink sink;
  Keyboard kb;
  kb.bounds = Rect(0, 100, 300, 140);
  CHECK(!kb.keyFor('q').empty() && kb.keyFor('Q').empty() && kb.keyFor('1').empty());
  // One-shot shift: the next letter is upper case, then back to lower.
  kb.handle(at(EventType::Tap, kb.shiftKey()), sink);
  CHECK(kb.mode == Keyboard::Mode::Upper);
  KeyInput k = kb.handle(at(EventType::Tap, kb.keyFor('S')), sink);
  CHECK(k.kind == KeyInput::Char && k.ch == 'S' && kb.mode == Keyboard::Mode::Lower);
  // 123 -> symbols -> #+= -> more symbols -> abc.
  kb.handle(at(EventType::Tap, kb.modeKey()), sink);
  CHECK(kb.mode == Keyboard::Mode::Symbols && !kb.keyFor('7').empty());
  kb.handle(at(EventType::Tap, kb.shiftKey()), sink);
  CHECK(kb.mode == Keyboard::Mode::More && !kb.keyFor('{').empty());
  kb.handle(at(EventType::Tap, kb.modeKey()), sink);
  CHECK(kb.mode == Keyboard::Mode::Lower);
  // Space, done gating, and long-press backspace = clear.
  CHECK(kb.handle(at(EventType::Tap, kb.keyFor(' ')), sink).ch == ' ');
  kb.doneEnabled = false;
  CHECK(kb.handle(at(EventType::Tap, kb.actionKey()), sink).kind == KeyInput::None);
  kb.doneEnabled = true;
  CHECK(kb.handle(at(EventType::Tap, kb.actionKey()), sink).kind == KeyInput::Done);
  Rect del = kb.shiftKey();
  del.x = (int16_t)(kb.bounds.right() - del.w);  // backspace mirrors shift on the right
  CHECK(kb.handle(at(EventType::LongPress, del), sink).kind == KeyInput::Clear);
}

TEST(wifi_onboarding_by_touch) {
  SimSession s(320, 240, kEveningEpoch, Appearance::Auto, false, /*online=*/false);
  CHECK(s.os.network().status().state == WifiState::NotConfigured);
  // The home note invites setup and deep-links into Settings > Wi-Fi.
  const Rect c = s.os.contentRect();
  s.tap(c.centerX(), c.bottom() - 20);
  CHECK(strcmp(s.os.foreground()->info().id, "settings") == 0 && strcmp(topTitle(s), "Wi-Fi") == 0);
  s.run(500);  // the screen started a scan
  CHECK(s.os.network().scan().count == 3);
  // First row: "Starlight" (strongest). Rows are 32 dp below the header.
  const Theme &t = s.os.theme();
  const int listTop = c.y + t.m.gapS + (t.m.row + t.dp(4)) + t.m.gapS + (t.m.control - t.dp(8)) + t.m.gapS + t.dp(4);
  s.tap(c.centerX(), listTop + t.dp(16));
  CHECK(strcmp(topTitle(s), "Starlight") == 0);
  typeOn(s, "moonbeam");
  auto *entry = static_cast<TextEntryScreen *>(s.os.topScreen());
  CHECK(strcmp(entry->text(), "moonbeam") == 0);
  tapRect(s, entry->keyboard().actionKey());
  CHECK(strcmp(topTitle(s), "Wi-Fi") == 0);  // back on the Wi-Fi screen
  CHECK(entry->text()[0] == '\0');             // the typed secret was wiped
  s.run(2000);
  const NetworkStatus net = s.os.network().status();
  CHECK(net.connected && strcmp(net.ssid, "Starlight") == 0);
  WifiCredentials saved;
  CHECK(loadRecord(&s.dev.storage, "wifi", "creds", WifiCredentials::kVersion, &saved, sizeof(saved)) ==
        RecordStatus::Ok);
  CHECK(strcmp(saved.password, "moonbeam") == 0);
}

TEST(password_shorter_than_wpa_minimum_cannot_be_submitted) {
  SimSession s(320, 240, kEveningEpoch, Appearance::Auto, false, false);
  s.os.launch("settings", "wifi");
  s.run(500);
  const Rect c = s.os.contentRect();
  const Theme &t = s.os.theme();
  const int listTop = c.y + t.m.gapS + (t.m.row + t.dp(4)) + t.m.gapS + (t.m.control - t.dp(8)) + t.m.gapS + t.dp(4);
  s.tap(c.centerX(), listTop + t.dp(16));
  typeOn(s, "short");
  auto *entry = static_cast<TextEntryScreen *>(s.os.topScreen());
  tapRect(s, entry->keyboard().actionKey());
  CHECK(strcmp(topTitle(s), "Starlight") == 0);  // still on the password screen
  CHECK(s.dev.wifi.connects == 0);
}

TEST(weather_loads_fails_soft_and_follows_place) {
  SimSession s(320, 240);
  CHECK_EQ(s.dev.http.requests, 1);  // fetched once Wi-Fi came up
  CHECK(strstr(s.dev.http.lastUrl.c_str(), "latitude=-37.81") != nullptr);  // Melbourne from the time zone
  WeatherSnapshot w = s.weather.snapshot();
  CHECK(w.report.valid && w.report.temperatureC == 17 && w.report.dayCount == 4 && w.report.days[1].code == 61);
  CHECK(w.report.days[3].rainChance == 0);  // JSON null is tolerated
  CHECK(w.status.freshness(s.os.millis(), WeatherService::kFreshForMs) == Freshness::Fresh);

  // A failed refresh keeps the last good report and records why.
  s.os.launch("weather");
  s.run(200);
  s.dev.http.failWith = 503;
  const Rect c = s.os.contentRect();
  s.longPress(c.x + 60, c.y + 50);  // long-press the hero to refresh
  s.run(500);
  CHECK_EQ(s.dev.http.requests, 2);
  w = s.weather.snapshot();
  CHECK(w.report.valid && w.report.temperatureC == 17 && !w.status.lastOk && strstr(w.status.error, "503"));

  // Choosing a place refetches with its coordinates and persists the choice.
  s.dev.http.failWith = 0;
  s.os.launch("weather", "places");
  CHECK(strcmp(topTitle(s), "Location") == 0);
  const Theme &t = s.os.theme();
  const int listTop = c.y + t.m.gapS + t.dp(4);
  s.tap(c.centerX(), listTop + 5 * t.dp(30) + t.dp(15));  // row 5: Tokyo
  s.run(500);
  CHECK(strstr(s.dev.http.lastUrl.c_str(), "latitude=35.68") != nullptr);
  uint8_t place = 0;
  CHECK(loadRecord(&s.dev.storage, "weather", "place", 1, &place, sizeof(place)) == RecordStatus::Ok && place == 5);
}

TEST(background_apps_do_not_repaint) {
  SimSession s(320, 240);
  s.os.launch("clock");
  s.run(300);
  s.dev.display.resetCounters();
  // Wi-Fi drops and returns: Settings and Weather receive topics in the
  // background, but only the status bar may change.
  s.dev.wifi.dropLink();
  s.run(8000);
  const uint64_t limit = (uint64_t)320 * s.os.theme().m.statusBar * 4;
  CHECK(s.dev.display.pixelsWritten() <= limit);
}

// ------------------------------------------------------------------ the next hours

using namespace stella::apps;

namespace {
WeatherReport hoursReport(const int chances[], int n, int64_t start) {
  WeatherReport w;
  w.valid = true;
  w.hourCount = (uint8_t)n;
  for (int i = 0; i < n; ++i) {
    w.hours[i].time = start + i * 3600;
    w.hours[i].rainChance = (uint8_t)chances[i];
  }
  return w;
}
}  // namespace

TEST(rain_outlook_finds_the_first_wet_hour_and_ignores_hours_that_are_over) {
  const int64_t base = 1794826800;  // 22:00
  const int wet[] = {10, 20, 35, 60, 80};
  WeatherReport w = hoursReport(wet, 5, base);
  RainOutlook r = rainOutlook(w, base + 600);  // 22:10
  CHECK(r.kind == RainOutlook::Likely && r.at == base + 3 * 3600 && !r.now && r.chance == 60);
  // The same hours later: the dry ones are behind us, and the wet hour is now.
  r = rainOutlook(w, base + 3 * 3600 + 120);
  CHECK(r.kind == RainOutlook::Likely && r.now && r.at == base + 3 * 3600);
  // Only "possible" chances (30-49 %).
  const int maybe[] = {5, 10, 40, 20};
  w = hoursReport(maybe, 4, base);
  r = rainOutlook(w, base);
  CHECK(r.kind == RainOutlook::Possible && r.at == base + 2 * 3600 && r.chance == 40);
  // Dry: reports the highest chance seen.
  const int dry[] = {5, 10, 25, 20};
  w = hoursReport(dry, 4, base);
  r = rainOutlook(w, base);
  CHECK(r.kind == RainOutlook::Dry && r.chance == 25);
  // No hours, or no report: unknown. A report entirely in the past is dry, never a stale warning.
  WeatherReport none;
  CHECK(rainOutlook(none, base).kind == RainOutlook::Unknown);
  w = hoursReport(wet, 5, base);
  CHECK(rainOutlook(w, base + 10 * 3600).kind == RainOutlook::Dry);
}

TEST(hourly_forecast_is_parsed_and_the_next_hours_screen_opens_from_its_chip) {
  SimSession s(320, 240);
  const WeatherReport &w = s.weather.snapshot().report;
  CHECK(w.valid && w.hourCount == WeatherReport::kHours);
  CHECK(w.hours[0].time == 1794826800 && w.hours[4].rainChance == 60 && w.hours[4].tempC == 15);
  CHECK(w.hours[10].rainChance == 0 && w.hours[11].rainChance == 5);  // a JSON null is no chance, and does not shift the rest
  // Regression: keys longer than the parser's old 23-character limit (the daily chance of rain was always 0).
  CHECK(w.days[0].rainChance == 20 && w.days[1].rainChance == 80 && w.days[2].rainChance == 5);
  s.os.launch("weather");
  s.run(200);
  const Rect chip = s.weather.hourlyChip();
  s.tap(chip.centerX(), chip.centerY());
  CHECK(s.os.topScreen()->title() && strcmp(s.os.topScreen()->title(), "Next hours") == 0);
  s.os.pop();
  CHECK(s.os.topScreen()->title() == nullptr);  // back on the main screen
}

TEST(a_forecast_with_no_hours_is_still_accepted) {
  SimSession s(320, 240);
  s.dev.http.routes.insert(s.dev.http.routes.begin(),
                           {"api.open-meteo.com",
                            "{\"current\":{\"time\":1794828600,\"temperature_2m\":17.4,\"apparent_temperature\":15.9,"
                            "\"relative_humidity_2m\":71,\"weather_code\":2,\"is_day\":1,\"wind_speed_10m\":13.7},"
                            "\"daily\":{\"time\":[1794747600,1794834000,1794920400],\"weather_code\":[3,61,1],"
                            "\"temperature_2m_max\":[21.3,18.9,24.6],\"temperature_2m_min\":[12.4,13.1,11.8],"
                            "\"precipitation_probability_max\":[20,80,5]}}",
                            200});
  s.os.launch("weather");
  s.run(200);
  const Rect c = s.os.contentRect();
  s.longPress(c.x + 60, c.y + 50);  // refresh now
  s.run(1000);
  const WeatherReport &w = s.weather.snapshot().report;
  CHECK(w.valid && w.hourCount == 0);
  const Rect chip = s.weather.hourlyChip();
  s.tap(chip.centerX(), chip.centerY());  // it still opens, and says there is nothing yet
  CHECK(s.os.topScreen()->title() && strcmp(s.os.topScreen()->title(), "Next hours") == 0);
}

TEST(the_next_hours_screen_renders_at_every_size_in_both_clock_styles) {
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    for (bool h24 : {true, false}) {
      SimSession s(sz[0], sz[1]);
      SystemSettings st = s.os.settings();
      st.clock24h = h24;
      s.os.applySettings(st);
      s.os.launch("weather");
      s.run(200);
      const Rect chip = s.weather.hourlyChip();
      s.tap(chip.centerX(), chip.centerY());
      CHECK(s.os.topScreen()->title() && strcmp(s.os.topScreen()->title(), "Next hours") == 0);
      const std::vector<Color> incremental = s.dev.display.framebuffer();
      s.os.invalidateAll();
      s.run(20);
      CHECK(incremental == s.dev.display.framebuffer());
    }
  }
}
