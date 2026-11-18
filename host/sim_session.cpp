#include "sim_session.h"

#include <stdio.h>
#include <string.h>

#include <string>

namespace stella {
namespace host {

namespace {

// An Open-Meteo response captured in shape (values chosen for the fixture).
std::string weatherFixture(bool day) {
  char json[2048];
  snprintf(json, sizeof(json),
           "{\"latitude\":-37.8,\"longitude\":144.96,\"timezone\":\"Australia/Sydney\","
           "\"current_units\":{\"temperature_2m\":\"\u00b0C\"},"
           "\"current\":{\"time\":1794828600,\"interval\":900,\"temperature_2m\":17.4,"
           "\"apparent_temperature\":15.9,\"relative_humidity_2m\":71,\"weather_code\":2,\"is_day\":%d,"
           "\"wind_speed_10m\":13.7},"
           "\"daily\":{\"time\":[1794747600,1794834000,1794920400,1795006800],\"weather_code\":[3,61,1,95],"
           "\"temperature_2m_max\":[21.3,18.9,24.6,26.1],\"temperature_2m_min\":[12.4,13.1,11.8,15.2],"
           "\"precipitation_probability_max\":[20,80,5,null]},"
           // Twelve hours from 22:00: dry, then rain building towards the small hours.
           "\"hourly\":{\"time\":[1794826800,1794830400,1794834000,1794837600,1794841200,1794844800,"
           "1794848400,1794852000,1794855600,1794859200,1794862800,1794866400],"
           "\"temperature_2m\":[17.4,16.9,16.2,15.8,15.1,14.9,14.6,14.2,14.0,14.5,16.0,17.9],"
           "\"precipitation_probability\":[10,10,20,35,60,80,70,40,20,10,null,5],"
           "\"weather_code\":[2,2,3,3,61,63,61,3,3,2,1,1]}}",
           day ? 1 : 0);
  return json;
}

}  // namespace

// A small calendar for the simulator and the tests: a stand-up and a lecture that repeat, a
// couple of one-off events, a multi-day trip and a birthday, around Mon 16 Nov 2026 (AEDT).
const char *const kCalendarFixture =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Stella//Simulator//EN\r\n"
    "BEGIN:VTIMEZONE\r\nTZID:Australia/Melbourne\r\n"
    "BEGIN:DAYLIGHT\r\nTZOFFSETFROM:+1000\r\nTZOFFSETTO:+1100\r\nDTSTART:19701004T020000\r\n"
    "RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=1SU\r\nEND:DAYLIGHT\r\n"
    "BEGIN:STANDARD\r\nTZOFFSETFROM:+1100\r\nTZOFFSETTO:+1000\r\nDTSTART:19700405T030000\r\n"
    "RRULE:FREQ=YEARLY;BYMONTH=4;BYDAY=1SU\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n"
    "BEGIN:VEVENT\r\nUID:standup\r\nSUMMARY:Team stand-up\r\n"
    "DTSTART;TZID=Australia/Melbourne:20261102T093000\r\nDURATION:PT15M\r\n"
    "RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:lecture\r\nSUMMARY:Chemistry lecture\r\nLOCATION:Room 3.12\r\n"
    "DTSTART;TZID=Australia/Melbourne:20260810T110000\r\nDTEND;TZID=Australia/Melbourne:20260810T130000\r\n"
    "RRULE:FREQ=WEEKLY;BYDAY=MO\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:dentist\r\nSUMMARY:Dentist\r\n"
    "DTSTART;TZID=Australia/Melbourne:20261117T083000\r\nDTEND;TZID=Australia/Melbourne:20261117T091500\r\n"
    "END:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:study\r\nSUMMARY:Study group\r\nLOCATION:Library, level 2\r\n"
    "DTSTART;TZID=Australia/Melbourne:20261117T150000\r\nDTEND;TZID=Australia/Melbourne:20261117T170000\r\n"
    "END:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:trip\r\nSUMMARY:Canberra trip\r\n"
    "DTSTART;VALUE=DATE:20261119\r\nDTEND;VALUE=DATE:20261123\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:movie\r\nSUMMARY:Movie night\r\nLOCATION:Cinema Nova\r\n"
    "DTSTART;TZID=Australia/Melbourne:20261120T193000\r\nDTEND;TZID=Australia/Melbourne:20261120T213000\r\n"
    "END:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:birthday\r\nSUMMARY:Mum's birthday\r\n"
    "DTSTART;VALUE=DATE:20101128\r\nRRULE:FREQ=YEARLY\r\nEND:VEVENT\r\n"
    "END:VCALENDAR\r\n";

SimSession::SimSession(int width, int height, int64_t epoch, Appearance appearance, bool threaded, bool online)
    : dev(width, height) {
  config.serviceThread = threaded;
  config.updateManifestUrl = "https://updates.example/stella/manifest.json";
  // The board's usual LED wiring; ledMode stays 1 (alerts and progress).
  config.defaults.ledPin[0] = 4;
  config.defaults.ledPin[1] = 16;
  config.defaults.ledPin[2] = 17;
  config.defaults.ledInverted = true;
  config.defaults.autoUpdate = false;  // the daily check is tested on its own; other tests count requests
  if (online) {
    config.wifiSsid = "Starlight";
    config.wifiPassword = "moonbeam";
  }
  dev.http.routes.push_back({"api.open-meteo.com", weatherFixture(epoch != kEveningEpoch), 200});
  dev.http.routes.push_back({"calendar.example", kCalendarFixture, 200});
  if (epoch) dev.platform.setEpoch(epoch);
  config.renderBuffer = dev.buffer.data();
  config.renderBufferPixels = dev.buffer.size();
  config.defaults.appearance = appearance;
  strcpy(config.defaults.timezone, kMelbourneTz);
  config.defaults.dimAfterSec = 0;  // screenshots stay at full brightness
  os.begin(dev.board(), config);
  os.install(launcher);
  os.install(clock);
  os.install(weather);
  os.install(alarms);
  os.install(timer);
  os.install(stopwatch);
  os.install(settings);
  os.install(touchTest);
  os.install(values);
  os.install(calendar);
  os.start();
  run(online ? 4000 : 100);  // join Wi-Fi and fetch the forecast
}

void SimSession::run(uint32_t ms) {
  uint32_t left = ms;
  for (;;) {
    uint32_t wait = os.step();
    if (!config.serviceThread) {
      const uint32_t service = os.serviceStep();
      if (service < wait) wait = service;
    }
    if (left == 0) break;
    if (wait == 0) wait = 1;
    if (wait > left) wait = left;
    dev.platform.advance(wait);
    left -= wait;
  }
}

void SimSession::tap(int x, int y) {
  dev.touch.down(x, y);
  run(80);
  dev.touch.up();
  run(150);
}

void SimSession::longPress(int x, int y) {
  dev.touch.down(x, y);
  run(800);
  dev.touch.up();
  run(150);
}

void SimSession::drag(int x0, int y0, int x1, int y1) {
  dev.touch.down(x0, y0);
  run(40);
  for (int i = 1; i <= 8; ++i) {
    dev.touch.down(x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8);
    run(20);
  }
  dev.touch.up();
  run(150);
}

Point SimSession::settingsRow(int i) const {
  const Theme &t = os.theme();
  const Rect c = os.contentRect();
  // Settings' list shrinks its rows when all of them do not fit (see MainScreen::layout).
  constexpr int kRows = 11;
  const int areaH = c.h - 2 * t.m.gapS;
  int rowH = kRows * t.m.row <= areaH ? t.m.row : areaH / kRows;
  if (rowH < t.dp(32)) rowH = t.dp(32);
  return Point{(int16_t)c.centerX(), (int16_t)(c.y + t.m.gapS + i * rowH + rowH / 2)};
}

Point SimSession::backButton() const { return Point{(int16_t)(os.theme().m.margin + 6), (int16_t)(os.theme().m.statusBar / 2)}; }

}  // namespace host
}  // namespace stella
