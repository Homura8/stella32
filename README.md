# Stella/32

A lightweight operating environment for ESP32 devices.

Stella gives small touch devices what an app needs, and nothing a PSRAM-less
ESP32 cannot afford: a multi-app runtime, a soft and clean UI toolkit in the
[Celestia Glow](https://github.com/Homura8/celestiaglow) style, a
dirty-rectangle renderer, gesture input, background jobs, supervision, power
management and versioned settings.

![Stella on a 320x240 panel](docs/screenshots/overview.png)

*Rendered on the host by the production renderer (`tools/screenshots.py`).*

## Status

| | |
| --- | --- |
| Version | 0.6.0, signed over-the-air updates |
| First board | ESP32-2432S028R "Cheap Yellow Display" (2.8" 320x240, XPT2046) |
| Host tests | 125 tests under ASan + UBSan, incl. touch-driven Wi-Fi onboarding and alarms; screenshots at 320x240 and 480x320 |
| Firmware build | Compiles with Arduino-ESP32 3.3.12: 1.35 MB flash (42%), 72 KB static RAM (21%) |
| On hardware | First runs on a CYD by the owner (0.3.0): boots, Wi-Fi, Weather; colour, brightness and paging feedback addressed in 0.3.1 (not yet re-checked on the device). |

Built-in apps: **Home** (launcher), **Clock** (with a starry, dim *bedside* mode
that starts by itself after the idle dim at night), **Weather** (Open-Meteo, no
API key), **Alarms** (weekday or one-off, snooze), **Timer**, **Settings**
(Wi-Fi, display, appearance, time, about) and **Touch test** (calibration aid).
Wi-Fi is set up on the device itself (scan, pick a network, type the password
on the on-screen keyboard) or from a phone (*Settings > Wi-Fi > Phone*: join
"Stella-Setup" and a web page takes the network and password). Alarms and timers ring through a system alert over
whatever app is open, with a soft chime on a speaker plugged into the CYD's
*SPEAK* connector.

## Quick start

### Host: tests and screenshots (no hardware)

Requires CMake, a C++17 compiler and Python 3 with Pillow.

```bash
cmake -S . -B build
cmake --build build -j
./build/stella_tests
python3 tools/screenshots.py
```

### Firmware: CYD

Requires [arduino-cli](https://docs.arduino.cc/arduino-cli/) 1.x. The pinned
profile installs Arduino-ESP32 3.3.12, TFT_eSPI 2.5.43 and
XPT2046_Touchscreen 1.4.0.

```bash
arduino-cli compile --profile cyd examples/CYD
```

```bash
arduino-cli upload --profile cyd -p /dev/ttyUSB0 examples/CYD
```

Join Wi-Fi on the device: the home screen offers *Set up Wi-Fi* on first
boot (or open *Settings > Wi-Fi*). The network is remembered across restarts;
time comes from SNTP and the weather from Open-Meteo. Optionally, copy
`examples/CYD/secrets.example.h` to `examples/CYD/secrets.h` (ignored by git)
to pre-load a network on first boot.

Faster rendering: uncomment `#define STELLA_CYD_DMA 1` in `examples/CYD/CYD.ino`
to draw the next chunk while the previous one is sent by DMA (uses 12.5 KiB
more RAM). It is off by default until verified on hardware.

In the Arduino IDE: add this repository as a library, open
*File > Examples > Stella32 > CYD*, choose *ESP32 Dev Module* with the
*Minimal SPIFFS (1.9MB APP with OTA)* partition scheme.

### Over-the-air updates

After one USB flash of the default build (it has two app slots), updating is:
merge to `main`, then on the device **Settings > About > Software update >
Check > Install > Restart**. GitHub builds and publishes the firmware on every
merge (one-time: repository *Settings > Pages > Source: GitHub Actions*; bump
`STELLA_VERSION` for a release). The download is checked (size, SHA-256) and the
new version is confirmed after a healthy minute; otherwise the bootloader
returns to the previous one. Your settings and Wi-Fi are kept. Signing updates
is optional extra security: see [Architecture > Updates](docs/ARCHITECTURE.md).
`--profile cyd-huge` builds the single-app layout (no updates).

## Repository guide

| Path | Contents |
| --- | --- |
| `src/stella/` | The OS, layered (see `docs/ARCHITECTURE.md`) |
| `src/stella/apps/` | Built-in apps |
| `src/stella/boards/cyd/`, `src/stella/port/esp32/` | CYD drivers, ESP32 port |
| `examples/CYD/` | Firmware sketch, TFT setup and pinned build profile |
| `host/` | Simulated board and screenshot simulator |
| `tests/` | Unit and integration tests |
| `tools/` | Architecture guard, screenshot and font generators, fonts |
| `docs/` | Architecture, design system, app and porting guides, screenshots |
| `AGENTS.md` | Rules and workflows for coding agents (read before changing code) |

## Documentation

- [Architecture](docs/ARCHITECTURE.md): layers, threads, rendering, memory.
- [Design system](docs/DESIGN.md): Celestia Glow tokens, materials, components.
- [Writing an app](docs/APPS.md)
- [Adding a board or display](docs/PORTING.md)

## Hardware checklist (first CYD flash)

1. Boot log shows `Stella 0.6.0 on ESP32-2432S028R (CYD), 320x240 compact`;
   the home screen appears without flicker or colour inversion.
2. *Touch test*: taps land on the four corner targets.
3. Every built-in screen matches `docs/screenshots/320x240/`.
4. *Set up Wi-Fi*: the scan lists nearby networks; joining with the
   on-screen keyboard connects, survives a restart, and *Forget* clears it.
   A wrong password shows "Couldn't join" and retries with growing gaps.
5. The clock shows local time within a minute of connecting; *Weather* shows
   the forecast and "Updated ... ago"; with the router off it keeps the last
   report and says so.
6. Brightness, dim, screen-off and *Dim at night* work; the touch that wakes a
   dark screen does not activate anything, while a tap on a dimmed screen
   brightens it and acts. Single quick taps open apps reliably. Settings
   survive a reset. The brightness slider changes evenly over its whole
   range, also at night ("dimmed for the night"), and the backlight does not
   flicker (20 kHz PWM).
7. *Settings > Display > Calibrate*: with
   `docs/screenshots/320x240/17-calibrate-tone-day.png` (and `-night`) open
   on a good monitor, fine-tune *Contrast* (light and dark mode separately),
   *White level* and *Warmth* until the grey ramp, text and background match;
   *Reset* returns a tab to the CYD defaults. Turning *Smooth sky* off shows
   whether dithering causes banding or flicker. Leave *Panel* at its
   defaults unless the screen shimmers.
   The home screen's ‹ › buttons page through apps while *Set up Wi-Fi* shows;
   tapping a sky in *Appearance* keeps it.
8. *Bedside clock*: with *Settings > Display > Bedside* on *Night*, leave the
   device idle at night (or open *Clock > Bedside*): the screen switches to a
   black sky with faint stars (a few twinkle every 4 s), a sleepy crescent
   moon, a big lilac clock, the date, the weather ("17° Partly cloudy", once
   Wi-Fi has fetched it) and "06:45 · in 8 h 15 min" when an alarm is set;
   any tap returns to the Clock. Is it dim enough for a dark room, are the
   stars visible, and does the black background still flicker?
9. *Phone setup*: *Settings > Wi-Fi > Phone* shows "Stella-Setup", a password
   and 192.168.4.1. Join it from a phone: the setup page should open by
   itself (otherwise open the address). The page lists nearby networks; enter
   a password and Connect. Try a wrong password first: the page and the screen
   say why and it stays open; the right one closes setup and the device
   connects. Cancel and the back button close it; so does waiting 10 minutes.
10. *Lists*: on Settings > Time (time zones), Settings > About, the Wi-Fi
   network list and the weather places, the up/down buttons at the right edge
   scroll by a page; the rail shows where you are.
11. *Software update*: after the one-time USB flash of the default build and
    enabling Pages (see above), merge a PR that bumps `STELLA_VERSION`; once
    the *Publish update* workflow has run, About > Software update > Check
    shows the new version; Install shows progress, then Restart now. After the
    restart the About screen shows it and, a minute later, the `health` log
    says the update is confirmed. To test the rollback: install a build whose
    firmware hangs or resets at boot (never confirmed): the device returns to
    the previous version by itself.
12. *Touch test > Calibrate*: tap the four crosses with the stylus; touches
   then land under the pen. Leaving the test clears its trail.
13. The `health` log line (once a minute) shows `stack ui`/`svc` headroom; it
   should stay above ~1 KB, also right after a weather refresh (TLS).
14. *Alarms and Timer*: a one-minute timer rings over another app and wakes a
   sleeping screen; *Again* restarts it. An alarm set for the next minute
   rings, *Snooze* brings it back nine minutes later, and a one-off alarm
   switches itself off. With a small speaker on the *SPEAK* connector the
   chime is audible but gentle (`STELLA_CYD_TONE_DUTY` adjusts it).
15. Optional: build with `STELLA_CYD_DMA 1` and compare smoothness; any
   corruption means DMA must stay off for this board.
16. Leave it running overnight; no `restarting` line in the log and the
   minimum heap in the `health` line stays stable.

## Licence

No licence has been chosen yet; all rights are reserved by the repository owner
until one is added. Bundled fonts are under the SIL Open Font License 1.1; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
