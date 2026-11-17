// Stella/32 firmware for the ESP32-2432S028R "Cheap Yellow Display".
//
// Build (from the repository root):
//   arduino-cli compile --profile cyd examples/CYD
// Arduino IDE: install this repository as a library, open File > Examples >
// Stella32 > CYD, select "ESP32 Dev Module" with the "Minimal SPIFFS (1.9MB APP
// with OTA)" partition scheme (the two-slot layout over-the-air updates need).
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
// Over-the-air updates (see docs/ARCHITECTURE.md, "Updates"). The default
// address is where the "Publish update" workflow puts every merge to main
// (GitHub Pages of this repository); set STELLA_UPDATE_URL in secrets.h to use
// another host. Optionally set STELLA_UPDATE_PUBLIC_KEY to require signed
// updates (tools/update_tool.py).
#ifndef STELLA_UPDATE_URL
#define STELLA_UPDATE_URL "https://homura8.github.io/stella32/manifest.json"
#endif
#ifndef STELLA_UPDATE_PUBLIC_KEY
#define STELLA_UPDATE_PUBLIC_KEY ""
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

// Stella confirms a freshly updated image itself, after it has run healthily
// for a minute (Runtime); until then the bootloader can still roll back. The
// Arduino core would otherwise confirm it at once.
extern "C" bool verifyRollbackLater() { return true; }

void setup() {
  Serial.begin(115200);
  board.begin();

  stella::RuntimeConfig config;
  config.renderBuffer = board.renderBuffer();
  config.renderBufferPixels = board.renderBufferPixels();
  config.renderBuffer2 = board.secondRenderBuffer();
  config.wifiSsid = STELLA_WIFI_SSID;  // first boot only; Settings > Wi-Fi replaces it
  config.wifiPassword = STELLA_WIFI_PASSWORD;
  board.setUpdateKey(STELLA_UPDATE_PUBLIC_KEY);
  config.updateManifestUrl = STELLA_UPDATE_URL[0] ? STELLA_UPDATE_URL : nullptr;
  strncpy(config.defaults.timezone, STELLA_DEFAULT_TIMEZONE, sizeof(config.defaults.timezone) - 1);
  // Panel calibration defaults for this board (Settings > Display > Calibrate
  // changes them on the device). The CYD's TN panel washes pastels out, lifts
  // dark tones and runs cold. Contrast was matched to a Mac by eye on a CYD
  // (light 86 %, dark 74 %); white level and warmth are starting points.
  // The panel's VCOM and frame rate stay at the TFT_eSPI ILI9341_2 values
  // most CYD projects use.
  config.defaults.colorTone = stella::ColorTone::Rich;
  config.defaults.contrastLight = 86;
  config.defaults.contrastDark = 74;
  config.defaults.whiteLevel = 70;
  config.defaults.warmth = 30;
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
