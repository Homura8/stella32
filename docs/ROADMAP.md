# Roadmap

What to build next, in what order, and why. Written so that another agent (or
the owner, months later) can pick it up cold. Read `AGENTS.md` first: its
invariants apply to everything here. This file records *intent*; the code and
`docs/ARCHITECTURE.md` record what exists.

## Direction

Stella/32 is meant to replace **Celestia Nightstand** (the owner's earlier CYD
firmware) on a much better base, and to be a good **desk screen** (under a
monitor) as much as a bedside clock. Decisions made by the owner, which
shape everything below:

- **The base CYD only.** No extra hardware is planned: no SD card, no
  accelerometer or other add-on sensors, no speaker assumed (the owner runs a
  bare CYD), and the back-of-board BOOT button is not used because the
  touchscreen already does everything. (A speaker still works if present;
  alarms and timers stay but are not a headline feature.)
- **Friends will get preloaded CYDs.** Features must be optional and the Home
  screen customisable; defaults must be sensible for someone who has none of
  the owner's accounts (no transit, no UniFi, no calendar).
- **Cheap and calm.** Few, clear controls; Celestia Glow look (`docs/DESIGN.md`).
- **Privacy.** The repository is public. Calendar URLs, API keys, stops, home
  network details and so on are entered on the device (phone page) and stay in
  versioned records on it. Never in the repo, logs, URLs that are logged, or
  topics (AGENTS.md invariant 15).
- **The LilyGO T-Display S3 comes later**, and needs a button-driven input
  model (it has no touch).
- **Usage-limit-aware work.** Prefer small, separately mergeable PRs, each with
  its tests, screenshots and docs. The owner merges; never merge for them.

## Where things stand (0.8.1)

Built and merged: the runtime, Celestia Glow toolkit, dirty-rectangle renderer,
apps (Home, Clock with bedside face, Weather, Alarms, Timer, Stopwatch,
Settings, Touch test), Wi-Fi onboarding on the device and from a phone,
over-the-air updates with rollback and a daily check, volume, a status LED
with *Find my LED*, and ambient-light dimming with an in-case calibration.
Screen transitions were tried and **removed** (too slow on the CYD); the
original two-layer shadows were kept after a softer version was rejected.

Verified on a real CYD by the owner: it boots, Wi-Fi, the weather, an
over-the-air update (0.7.1 to 0.8.0), the light sensor calibration (with a
phone flashlight), and *Find my LED*. (The phone-setup page opened but slowly
on 0.6.0; the fix in 0.6.1 has not been reported on since.) Everything else in `README.md`'s checklist is host-tested only. Do not
claim device behaviour that has not been seen there.

## Order of work

Small first; each step unlocks the next.

### 1. Quick wins (no new infrastructure)

**Sunrise and sunset.** Compute on the device from the Weather place's
latitude/longitude and the date (a standard solar-position algorithm; no
network, no heap). Show on the Weather screen and the bedside face. Then let
the sky phases, night dimming and the bedside auto-start follow *real* sunrise
and sunset instead of the fixed hours in `phaseForHour()`. Tests: known
dates and places (compare against published tables), polar edge cases
(no sunrise), time zones and daylight-saving changes.

**Better Weather.** Same Open-Meteo source (no key). Add an hourly strip
(temperature and rain chance for the next ~12 hours), a "rain from about 3 pm"
line (the first hour above a threshold in the next day), sunrise/sunset, and
air quality / UV from Open-Meteo's free endpoints if the JSON stays bounded.
Keep the existing freshness model (`DataStatus`) and the glance line.
Reference: Celestia Nightstand's weather screen ("Next 3 hours: rain 65%").

**What's new.** After an over-the-air update, the first boot of a new version
shows a short, scrollable changelog once (store the last seen version in a
record; the text comes from a compiled-in table, newest first, written for
people, not commit messages). Optional follow-up: an "install updates
overnight" setting that still only installs inside a window and never without
the person having turned it on.

**Portrait mode, not just landscape.** The owner wants the CYD usable upright
too (a phone-like stand, or on a wall). The foundation is there: the HAL does
not assume 320x240 and the host tests already render 240x320; what is missing is
polish and a way to choose it. So this item is more than the rotation setting
below: every screen needs a deliberate portrait layout (the Home header's time
and words do not fit side by side at 240 px wide, and the Weather screen's
sunrise/sunset line hides itself there), the touch calibration is per rotation,
and the screenshots must include a 240x320 set that is *looked at*, not only
rendered. Do it together with the rotation setting.

**Manual screen rotation.** Settings > Display: 0 / 90 / 180 / 270 degrees,
for desk or wall use (the CYD has no accelerometer, so it is a setting).
Needs `Display::setRotation`-style support in the HAL and CYD driver, the
touch mapping and calibration per rotation, a re-layout, and tests at both
aspect ratios (the suite already renders 240x320 and 320x240; AGENTS.md
invariant 10). Persist in the settings record (bump the version; see
`system_settings.cpp` for how older records migrate).

**Small units and locale settings.** Temperature unit (°C/°F), week start,
date order. Cheap, and it matters for friends.

### 2. Customisable Home

The owner wants each device's Home to be configurable. Settings > Home: show or
hide each app and reorder them (a persisted list in the settings record).
Defaults: Clock, Weather, Alarms, Timer, Stopwatch, Settings visible; Touch test
hidden (calibration lives in Settings). Optional apps added later (Calendar,
Network, Brief) are *off* until set up. The launcher already pages; keep the
touch targets at `t.dp(32)` and the ListView scroll buttons for the list.

### 3. The remote page (phone and PC on the home network)

The foundation for wallpaper, configuration entry, and integrations. A small
web server on the device's normal Wi-Fi connection (the phone-setup server in
`port/esp32/esp32_port.h` is the starting point, but it only runs as an access
point):

- Reachable by name (`stella.local` through mDNS) and by IP (shown on screen).
- **Protected**: a PIN shown on the device screen is required; rate-limit
  attempts; plain HTTP is acceptable on a home LAN but nothing sensitive is
  ever sent back (no stored secrets are readable through it, only writable).
- Needs a **streaming** request body: `HttpRequestParser` currently keeps at
  most 512 bytes. Add a bounded streaming mode that hands body chunks to a
  handler (for the wallpaper) without buffering them.
- A tiny JSON API so a PC script or Home Assistant can *push* values
  (`POST /api/push`) that widgets show; widgets must treat pushed data as
  untrusted and bounded.
- Used for: wallpaper upload, Home layout, entering the calendar URL and API
  keys (instead of the resistive keyboard), sending the clock a note.

### 4. Custom wallpaper (full resolution, from the phone)

The owner wants **full 320x240 resolution**, "compressed with JPEG". The
constraint: with the default `min_spiffs` layout there are only **128 KB** of
data flash (two 1.9 MB app slots), no PSRAM, and the renderer works in 20-row
chunks with no frame buffer. Two designs; spike both before committing:

1. **8-bit palette image, converted on the phone** (recommended to try first).
   The phone page decodes any photo, scales/crops to 320x240, darkens and
   softens it (so glass text stays readable), reduces it to a 256-colour RGB565
   palette with dithering, and uploads 256 x 2 B + 76,800 B = ~77 KB. It fits
   the 128 KB partition (use `esp_partition_write` directly), needs **no
   decoder on the device**, and is trivially random-access, which the chunked
   renderer needs (the dirty-rectangle renderer asks for arbitrary rows in any
   order). Compare quality against the sky it replaces; a blurred photo
   quantises well.
2. **JPEG on the device** (what the owner suggested). ~15-35 KB at quality
   60-75, so size is no problem, but decoding is: JPEG is sequential, the
   renderer is not. Options: encode on the phone with a restart marker per MCU
   row and index them on upload (then decode only the needed rows with a small
   decoder such as TJpgDec/JPEGDEC, caching one ~10 KB strip), or decode on
   demand per chunk and accept re-decoding cost. Needs measuring on the device
   (frame time) before it is acceptable; the same 80 ms-per-frame lesson as the
   transitions applies.

Either way: the wallpaper replaces `paintSky` behind the glass (keep the sky as
the fallback and for the status-bar colour), has a scrim strength setting, is
dropped on a factory reset, and every screen must still pass the 4-density
render tests (AGENTS.md invariants 7 and 10). Persist with a version and a
checksum (`storage/record.h` conventions).

### 5. Dashboard idle face (the desk use)

Today the idle face is the bedside clock only. Add a choice in Settings >
Display: **Bedside clock / Dashboard / Off**. The dashboard cycles widget
pages (time + weather, calendar, network, pushed values) with the usual
`kAppIdleFace` mechanism; it must stay calm (slow, repaint-minimal) and must
not light up a dark room (it follows the ambient-light dimming).

### 6. Bringing Celestia Nightstand across

Source of truth: the owner's `celestia-nightstand` repository (private;
`NightstandCYD/` for the firmware, `previews/` for screenshots,
`docs/ARCHITECTURE.md` for its constraints). Its features and where they land:

| Nightstand | In Stella | Notes |
| --- | --- | --- |
| Home with the next class, weather, "daily brief" | Customisable Home + a Calendar app | |
| University calendar (iCalendar fetch and parse) | **Calendar app** (`net/` parser + app) | Bounded, streaming parse; time-zone and all-day handling; freshness/coverage warnings as Nightstand does; the URL is entered on the remote page and never logged |
| Bedside face with "First class 09:00 · wake ~06:50", sleep window | Bedside face lines + a **sleep planner** | Needs the calendar; port `planner.h`'s logic with its tests |
| Travel (Melbourne bus + train / tram, departures, transfers) | **Transit app**, optional | Very personal and key-gated (PTV); keep the stops and keys on the device only. Port the planner and its 60 reference comparisons if wanted |
| Network (UniFi inventory and traffic) | **Network app**, optional | Read-only; host and API key entered on the device; the owner uses it, friends will not |
| Daily brief (Gemini) | **Brief app**, optional | Needs a key; **explicit consent every time** because it costs money and sends data out (Nightstand shows a confirm dialog; keep it) |
| Setup captive portal | Already done (`net/setup_portal.*`) | |
| Weather | Better Weather above | |

Rules for the port: reuse the *logic* and tests, not the code layout; every
integration is an optional app with its own Job, `Topic<T>`, `DataStatus`
freshness and `Deferred` (not Failed) on a busy TLS lease (invariants 4-6);
network data is untrusted and parsed with bounded streaming parsers; nothing
personal (stops, subjects, coordinates, URLs) is committed.

### 7. Bluetooth Low Energy: a macro deck (RAM is the constraint)

The owner's most-wanted extra: the touchscreen as a **macro pad** for a PC
(media keys, mute, screenshot, open a terminal) over BLE HID. The CYD's USB
port is serial-only, so BLE is the only way to act as a keyboard.

Budget before starting (measure, do not assume): the default build is ~1.48 MB of
a 1.9 MB slot and ~84 KB static RAM, and the heap dipped to ~66 KB free during an
update. A BLE host adds flash and tens of KB of RAM. Use **NimBLE** (not
Bluedroid). Plan:

- A compile-time profile first (`sketch.yaml`), measure flash and the `Lowest
  free` heap in *Settings > About* with and without BLE, with Wi-Fi + a TLS
  fetch running. If the margin is too thin, start the stack only while the deck
  screen is open and stop it on leaving (and never during an update).
- BLE callbacks run on the host task: they may only post to the runtime's event
  queue (invariant 4); no app/UI code in them.
- UI: a Deck app with a grid of buttons; layouts edited on the remote page; a
  pairing screen showing the state. Mac, Windows and Linux differ in shortcuts:
  store per-button key codes, not names.
- Risk: radio sharing with Wi-Fi (throughput during downloads); test the update
  path with BLE compiled in.

### 8. Wi-Fi extras and other integrations

- **mDNS** (`stella.local`), needed by the remote page.
- **ESP-NOW** between CYDs (e.g. a friend's pair sharing a note or a timer).
- **Push API** (above) enabling: PC stats (CPU, temperatures, now playing)
  from a small script, Home Assistant / MQTT values and tap-to-toggle buttons,
  GitHub CI status for the owner's repositories.
- **Guest Wi-Fi QR code** screen (needs a small QR encoder; check size).
- Countdowns, world clocks, RSS headlines, air quality/UV/tides (some from
  Open-Meteo's free endpoints).
- LED **quiet hours** (no alert light between chosen times).

## Not planned (owner decisions)

- Extra hardware: SD card, accelerometer, environment sensors, battery.
- Using the BOOT button.
- Screen transitions (removed in 0.8.0: on the owner's CYD the first slide was
  very laggy and the safeguard then switched them off). If animation returns it
  must be measured on a real CYD first; the renderer has no frame buffer.
- Softer drop shadows (rejected; the original two-layer shadow stays).

## Later

- **LilyGO T-Display S3** (170x320, no touch, two buttons): needs a
  button-driven input model and a layout pass; `docs/PORTING.md` describes the
  HAL work. The HAL already keeps nothing above it dependent on 320x240.
- An ESP-IDF port.

## Working notes for the next agent

- Verify with the commands in `AGENTS.md` section 6, look at the regenerated
  screenshots, and say plainly what was not run on a device.
- The owner tests on real CYDs and reports back; hardware checklists live in
  `README.md`. Over-the-air updates are published by the `Publish update`
  workflow on every merge to `main`; bump `STELLA_VERSION` (and note it in the
  PR) to offer one. After a failed update, *Settings > About > Last reset*
  names what the device was doing (`update_crumb`).
- The Arduino setup that works: `arduino-cli compile --profile cyd examples/CYD`
  (Arduino-ESP32 3.3.12, TFT_eSPI 2.5.43, XPT2046_Touchscreen 1.4.0), partition
  scheme "Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)".
- `tools/check_architecture.py` rejects the words `new <identifier>` even in
  strings; reword (for example "fresh update").
- PRs may be stacked on each other; the owner merges them in order and deletes
  the branches so GitHub retargets the rest.
