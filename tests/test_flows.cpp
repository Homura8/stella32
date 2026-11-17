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
