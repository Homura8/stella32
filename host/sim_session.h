#pragma once
// A complete simulated Stella device with the built-in apps installed, plus
// helpers to drive it with taps and time. Used by tests and the simulator.
#include <stdint.h>

#include "host_board.h"
#include "stella/apps/apps.h"
#include "stella/runtime/runtime.h"

namespace stella {
namespace host {

// Fixed instants for deterministic screenshots (Australia/Melbourne).
constexpr int64_t kEveningEpoch = 1794828600;  // Mon 16 Nov 2026 22:30 AEDT
constexpr int64_t kMorningEpoch = 1794784500;  // Mon 16 Nov 2026 10:15 AEDT
constexpr const char *kMelbourneTz = "AEST-10AEDT,M10.1.0,M4.1.0/3";

class SimSession {
 public:
  // threaded = false runs background jobs synchronously inside run(), so
  // network-dependent screens are deterministic.
  // online = true pre-provisions the simulated "Starlight" network, so Wi-Fi
  // connects and the weather fixture loads during construction.
  SimSession(int width, int height, int64_t epoch = kEveningEpoch, Appearance appearance = Appearance::Auto,
             bool threaded = false, bool online = true);

  // Run the UI loop for `ms` of simulated time, honouring its sleep requests.
  void run(uint32_t ms);
  void tap(int x, int y);
  void longPress(int x, int y);
  void drag(int x0, int y0, int x1, int y1);
  // Centre of settings row `i` on the Settings main screen
  // (0 Wi-Fi, 1 Display, 2 Appearance, 3 Time, 4 About).
  Point settingsRow(int i) const;
  Point backButton() const;

  SimDevice dev;
  Runtime os;
  apps::LauncherApp launcher;
  apps::ClockApp clock;
  apps::SettingsApp settings;
  apps::TouchTestApp touchTest;
  apps::WeatherApp weather;
  apps::AlarmsApp alarms;
  apps::TimerApp timer;
  apps::StopwatchApp stopwatch;
  RuntimeConfig config;
};

}  // namespace host
}  // namespace stella
