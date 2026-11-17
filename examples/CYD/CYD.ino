// Stella/32 firmware for the ESP32-2432S028R "Cheap Yellow Display".
//
// Build (from the repository root):
//   arduino-cli compile --profile cyd examples/CYD
// Arduino IDE: install this repository as a library, open File > Examples >
// Stella32 > CYD, select "ESP32 Dev Module" with the "Huge APP" partition.
//
// Wi-Fi: join from Settings > Wi-Fi on the device. Optionally, copy
// secrets.example.h to secrets.h to pre-load a network on first boot.
//
// Faster rendering: uncomment to use DMA transfers (verify on your board).
// #define STELLA_CYD_DMA 1
#include <Stella.h>
#include <stella/boards/cyd/cyd_board.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef STELLA_WIFI_SSID
#define STELLA_WIFI_SSID ""
#define STELLA_WIFI_PASSWORD ""
#endif
#ifndef STELLA_DEFAULT_TIMEZONE
#define STELLA_DEFAULT_TIMEZONE "AEST-10AEDT,M10.1.0,M4.1.0/3"  // Melbourne; change in Settings > Time
#endif

namespace {

stella::cyd::CydBoard board;
stella::Runtime os;
stella::apps::LauncherApp launcher;
stella::apps::ClockApp clockApp;
stella::apps::SettingsApp settingsApp;
stella::apps::TouchTestApp touchTest;
stella::apps::WeatherApp weather;
stella::apps::AlarmsApp alarms;
stella::apps::TimerApp timer;

}  // namespace

void setup() {
  Serial.begin(115200);
  board.begin();

  stella::RuntimeConfig config;
  config.renderBuffer = board.renderBuffer();
  config.renderBufferPixels = board.renderBufferPixels();
  config.renderBuffer2 = board.secondRenderBuffer();
  config.wifiSsid = STELLA_WIFI_SSID;  // first boot only; Settings > Wi-Fi replaces it
  config.wifiPassword = STELLA_WIFI_PASSWORD;
  strncpy(config.defaults.timezone, STELLA_DEFAULT_TIMEZONE, sizeof(config.defaults.timezone) - 1);
  // Panel calibration defaults for this board (Settings > Display > Calibrate
  // changes them on the device). The CYD's TN panel washes pastels out and
  // lifts dark tones, so it starts richer and with some contrast.
  config.defaults.colorTone = stella::ColorTone::Rich;
  config.defaults.contrast = 40;
  if (!os.begin(board.board(), config)) {
    Serial.println("Stella failed to start");
    return;
  }
  os.install(launcher);
  os.install(clockApp);
  os.install(weather);
  os.install(alarms);
  os.install(timer);
  os.install(settingsApp);
  os.install(touchTest);
  os.start();
}

void loop() { os.run(); }
