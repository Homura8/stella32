// Stella/32 firmware for the ESP32-2432S028R "Cheap Yellow Display".
//
// Build (from the repository root):
//   arduino-cli compile --profile cyd examples/CYD
// Arduino IDE: install this repository as a library, open File > Examples >
// Stella32 > CYD, select "ESP32 Dev Module" with the "Huge APP" partition.
//
// Optional: copy secrets.example.h to secrets.h for Wi-Fi and network time.
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
stella::esp32::WifiJob wifi(os, STELLA_WIFI_SSID, STELLA_WIFI_PASSWORD);

}  // namespace

void setup() {
  Serial.begin(115200);
  board.begin();

  stella::RuntimeConfig config;
  config.renderBuffer = board.renderBuffer();
  config.renderBufferPixels = board.renderBufferPixels();
  strncpy(config.defaults.timezone, STELLA_DEFAULT_TIMEZONE, sizeof(config.defaults.timezone) - 1);
  if (!os.begin(board.board(), config)) {
    Serial.println("Stella failed to start");
    return;
  }
  os.install(launcher);
  os.install(clockApp);
  os.install(settingsApp);
  os.install(touchTest);
  if (wifi.configured()) os.addJob(wifi);
  os.start();
}

void loop() { os.run(); }
