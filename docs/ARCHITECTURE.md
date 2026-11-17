# Architecture

Stella/32 is a small operating environment, not a kernel: it runs on top of
FreeRTOS (via Arduino-ESP32 today) and provides what an application needs on a
small touch device: an app model, a UI toolkit with a coherent visual language,
input, rendering, background work, storage, power and supervision.

## Goals

1. **Robust on a PSRAM-less ESP32.** Bounded memory, no heap after boot in the
   portable code, supervised threads, versioned persistent data.
2. **Many screens, one codebase.** Apps never see pixel coordinates of a
   particular panel; layouts are derived from the display size and density.
3. **Easy to iterate.** Visual decisions live in one theme; each screen's layout
   lives in one function; screenshots are generated on the host so a change can
   be seen (by people and agents) without hardware.
4. **Testable without hardware.** Everything above the HAL compiles and runs on
   Linux under AddressSanitizer and UndefinedBehaviorSanitizer.

## Layers

```
 apps/        Launcher, Clock, Weather, Alarms, Timer, Settings  (only via AppContext)
 runtime/     Runtime: navigation, shell, alerts, power, settings, supervision, event loop
 app/         App / Screen / AppContext contract, system settings
 ui/          Theme (Celestia Glow tokens), materials, icons, layout DSL, widgets
 net/         WifiManager (Wi-Fi service), HttpService, streaming JSON parser
 service/     Topic<T>, JobScheduler, SharedResource lease, Supervisor, DataStatus
 storage/     Versioned, checksummed records over KeyValueStore
 input/       Events, gesture recognizer, touch calibration
 gfx/ fonts/  Canvas (anti-aliased primitives, text), dirty-rect Renderer, fonts
 hal/         Display, TouchInput, Platform, KeyValueStore, WifiDriver, HttpClient, Sound
 osal/        Mutex, RecursiveMutex, Signal, threads (FreeRTOS | std)
 core/        Geometry, colour, wrap-safe time, fixed containers, text, logging
 ───────────────────────────────────────────────────────────────────────────
 port/esp32/  Arduino-ESP32: Platform, NVS, Wi-Fi + HTTPS drivers (device only)
 boards/cyd/  ESP32-2432S028R display + touch drivers, profile   (device only)
 host/        Simulated board, screenshot simulator              (host only)
```

`tools/check_architecture.py` enforces which layer may include which, that only
`port/`, `boards/` and the FreeRTOS OSAL use platform headers, and that portable
code does not allocate on the heap. CI runs it on every change.

## Threads and data flow

| Thread | Runs | Supervised |
| --- | --- | --- |
| UI (Arduino `loopTask`) | `Runtime::run()`: input, app callbacks, rendering, settings persistence | yes, by the service thread (3 min) |
| Service (`stella-svc`) | `JobScheduler`: Wi-Fi, weather and any app's jobs; TLS runs here (16 KiB stack) | yes, by the UI thread (10 min) |

- **Apps run only on the UI thread.** No locks are needed in app code.
- **Jobs publish, apps read.** A job publishes a copy of its result to a
  `Topic<T>`; the topic posts one coalesced notification to the UI thread's
  event queue; the runtime calls `App::onTopic()` for running apps, which read
  the value and invalidate what changed.
- **Shared scarce resources** (the single TLS session a PSRAM-less ESP32 can
  afford) use `SharedResource`: Primary clients wait, Optional clients never
  start while a Primary waits. A busy lease is `JobResult::Deferred`, never a
  failure.
- **Apps own their services.** `App::onInstall(ServiceHost&)` registers an
  app's jobs and topics at boot (the Weather app adds its fetcher this way);
  `AppContext::requestJob()` asks for an immediate run ("refresh").
- **Background apps cannot repaint.** The runtime ignores `invalidate()` from
  any app other than the foreground one; `Screen::isVisible()` lets screens
  skip work while hidden.
- **Background timers.** `App::onWake()` runs for every installed app, in
  front or not, at the delay it returns (`AppContext::scheduleWake()` brings
  it forward). Alarms check once a minute; the timer wakes when it ends. It
  runs on the UI thread, so it must be short.
- **Retry policy** is declarative (`JobPolicy`): interval, exponential retry
  backoff with a cap, deferral delay, and "needs network" gating. Reconnecting
  or a manual refresh clears backoff.
- **Supervision.** Each thread beats its watch after each unit of work. If a
  thread is silent past its limit the other thread records the name in RTC
  memory and restarts the device; the next boot logs it.

Lock ordering: resource lease → data locks. No lock is held across flash, network
or display I/O.

## Networking

- **Wi-Fi** is an OS service (`net/wifi_manager.*`): saved credentials (a
  versioned record, wiped from RAM after use), reconnection with backoff
  (5 s doubling to 2 min), non-blocking scans, SNTP once connected, and two
  topics (`NetworkStatus`, `WifiScan`). Apps control it through
  `AppContext::network()`; the board supplies a small `WifiDriver`.
- **HTTP** (`net/http_service.*`): jobs call `HttpService::getJson(url,
  handler)`. It takes the shared TLS lease (busy = `Deferred`), enforces
  size and time limits, verifies certificates (core CA bundle on ESP32), and
  streams the body through a bounded SAX parser (`net/json.*`, ~0.5 KiB, depth
  10): no response is ever held in RAM.
- **Phone setup** (`net/setup_portal.*`, `SetupServer` in the HAL): from
  *Settings > Wi-Fi > Phone* the device scans once, then opens a WPA2 access
  point ("Stella-Setup", a random 8-character password shown only on the
  device screen) with a captive DNS and a tiny web server, so a phone opens
  the setup page by itself. The page lists the networks found and takes a name
  and password; the portable `SetupPortal` validates and bounds the form (32 /
  63 bytes), escapes everything it echoes, and asks `WifiManager` to join. The
  server is polled from the Wi-Fi job (every 25 ms while open). It closes on
  success, on *Cancel* or leaving the screen, and after 10 minutes. A wrong
  password leaves it open with the reason on the page and on the device. The
  page is plain HTTP over the protected access point (three clients);
  the temporary password lives only in `WifiManager` (mutex-guarded, wiped on
  close), never in a topic or a log. Only the thin `Esp32SetupServer` (sockets,
  DNS, access point) lives in the port. **The server never waits**: a phone
  that joins opens several connections at once (captive-portal probes,
  speculative sockets that send nothing), and the Arduino `WebServer` serves
  one at a time and waits up to five seconds on each silent one, which made
  the page take about a minute on a real phone (0.6.0). `Esp32SetupServer`
  accepts every pending connection into one of a few slots, feeds whatever
  has arrived to the portable, bounded `HttpRequestParser`
  (`net/http_request.*`: method, path and a form body of at most 512 bytes;
  header lines are inspected and dropped, so nothing grows), answers as soon
  as a request is complete and closes sockets that stay silent for 2.5 s.
- **Freshness** (`service/data_status.h`): every data service keeps its last
  good value and says *why* it might be old (Weather: "Offline · from 12 min
  ago").

## Fullscreen screens and the idle face

A `Screen` may be `fullscreen()`: the shell then draws no sky and no status bar
under it (and offers no back or home gestures), `layout()` gets the whole
display, `draw()` paints every pixel, and the backlight runs at a third of
its level (never below the minimum). The screen handles its own events,
including leaving. The bedside clock is one (after Celestia Nightstand's):
a black field (`Palette::bedBg`, so nothing to dither), faint stars, a sleepy
crescent moon and a ringed planet, the largest numerals that fit (the
`bedside` font), the date, the weather and the next alarm.

It animates calmly on a 16 s cycle (`bedsideFrameAt`): every 4 s some stars
twinkle, the big sparkle swells, and the moon and planet shift a pixel; once
per cycle the moon blinks. Only the spots that change are invalidated, so a
frame costs a few thousand pixels.

An app flagged `kAppIdleFace` offers such a face. After the idle dim, if the
setting *Bedside* is *Night* (and it is night by the clock) or *Always*, and
the time is known, the runtime calls `launch(id, "bedside")`, but only when
Home or that app is in front, so nothing the user is doing is replaced. Any
tap on the face leaves it (`pop()`), landing on the Clock.

Apps may offer a short line for idle faces with `AppContext::setGlance(icon,
text)`; the Weather app publishes "17° Partly cloudy" (and withdraws it when
the data is over 3 hours old). It does this from `onWake`, because apps that
were never opened receive no topic notifications.

Apps that ring tell the shell when with `AppContext::setNextAlarm(epoch)`;
`SystemStatus::nextAlarm` is the earliest. `localTimeAt(epoch)` gives that
time's wall-clock components in the configured zone (daylight saving
included).

## Status LED and ambient light

Two optional HAL devices (`Board::led`, `Board::light`). Both are data-driven
because boards of the same name differ.

**LED** (`runtime/led_engine.*`, `Led` in `hal/hal.h`): several things may want
it at once, so each is a *source* with a pattern (off, steady, breathe, blink)
and the most important active one wins: alert > app flash > update > Wi-Fi >
bedside glow. The runtime feeds the sources: an alert breathes amber until
answered; an update breathes blue while it downloads, green when it waits for
the restart, and flashes red if it fails; phone setup breathes violet, and a
join started in Settings flashes green or red; apps call `flashLed()` (the
Stopwatch blinks on a lap); in glow mode the bedside face has a faint breathing
violet light. `ledMode` is Off / Alerts and progress / Glow and `ledLevel` caps
the brightness. The glow is opt-in because a light in a dark bedroom is a
choice. The wiring (which pin is red, green and blue, active-low or not) is
the `ledPin[3]`/`ledInverted` settings: the board's sketch gives the common
default, and *Settings > Sound & light > Find my LED* finds the real one. An LED
cannot be sensed, so the person is asked: the board offers the pins that are
safe to drive (`Led::candidatePins`, never display, touch, speaker or storage
pins), the screen drives one at a time (low, then high, with every other
candidate left floating so nothing else can light) and asks which colour
appeared. A probe ends by itself after 10 s and the normal LED comes back.

**Ambient light** (`runtime/ambient.*`, `LightSensor`): an LDR beside a
backlit screen, in a case of any colour, has no useful absolute value.
*Settings > Sound & light > Light sensor* measures it in its place: with the
room dark it reads the sensor at four exact backlight levels (the runtime's
`holdBacklight`) and fits `reading = dark + self * duty/255`, so the screen's
own glow becomes a known term; then in normal room light it reads once more
and subtracts the screen's glow to get `bright`. Afterwards a reading is
corrected for the backlight that was lit, placed between dark (0) and bright
(1) and smoothed; the sign of (bright - dark) is the polarity, so any wiring and
case colour work. The user's brightness is multiplied by `0.12 + 0.88 * level^0.6`
(most of the dimming is between dark and a dim room), moved in steps of at most
8/255 a quarter second and only when it differs by 6 or more, so it glides and
does not hunt. A calibration whose two ends are less than 60 counts apart is
rejected (covered sensor), and auto dim cannot be switched on without one.

## Alerts

`AppContext::showAlert()` puts a modal glass card over everything (status bar
included) with one or two buttons, for things that must not be missed: alarms,
timers. The runtime:

- queues up to four (an app's alert with the same tag is updated in place);
- wakes the display and keeps it awake while an alert is up;
- cancels a touch that was in progress, so an alert is never answered by a
  finger that was already down, and routes all input to the alert's buttons;
- plays the chime (a soft C-E-G arpeggio, then a rest) on the board's optional
  `Sound` while an alert with `sound` is up, sequenced from the UI loop;
- closes it after its `timeoutMs` and tells the owning app the outcome in
  `App::onAlert()` (primary, secondary or timed out).

## Rendering

There is no framebuffer (it would cost 150 KiB). The runtime keeps a
`DirtyRegion` (up to 8 merged rectangles). `Renderer::render()` replays the
frame's draw function once per *chunk* of each dirty rectangle, into one small
buffer (12.5 KiB on the CYD), and streams each chunk to the display. Narrow
updates (a toggle, the clock digits) fit in a single chunk.

The frame is: sky backdrop (`paintSky`) → status bar → top screen (clipped to the
content area) → toast → alert (veil and card), if any. Draw code must be deterministic and must repaint every
pixel it covers; translucent glass blends over the backdrop drawn first.

Invariant (tested): any sequence of incremental frames produces the same pixels
as a full redraw. Idle screens render nothing.

Performance work (host benchmark `stella_bench`, relative numbers):
- RGB565 blending on packed pixels with 5-bit alpha (one multiply per pixel,
  verified exactly against a per-channel reference).
- Anti-aliasing maths only in rounded corners; straight runs are plain fills.
- Shadows painted only outside cards (as CSS `box-shadow`), not under glass.
- Sky: fixed-point gradient, washes sampled every 8 px on a screen-anchored
  grid and interpolated, shift-only ordered dithering.
- Result: a full 320x240 frame costs about half of 0.1.0; the sky about 40%,
  a glass card about 15%.
- Optional DMA (`Display::writePixelsAsync`): with a second chunk buffer the
  next chunk is drawn while the previous one is transferred (tested with a
  display double that reads buffers late).

## UI model

- **Theme** (`ui/theme.*`): palette, metrics and fonts for a density class
  (`Compact` ≈ 320x240, `Regular` ≈ 480x320 and up), light or dark, and a sky
  phase (dawn, day, dusk, night). Appearance `Auto` follows the sky: light by
  day, dark from dusk. The runtime re-themes when the phase changes.
- **Layout DSL** (`ui/layout.h`): `row`/`column` of fixed (`px`) and flexible
  (`flex`) lengths, `grid`, `center`, `leading`/`trailing`. Screens compute all
  rectangles in `Screen::layout()`.
- **Widgets** (`ui/widgets.h`): Button, Toggle, Slider, Segmented, ListRow,
  ListView. Small value types; the owning screen sets `bounds`, draws them and
  forwards events.
- **Shell**: status bar (back affordance or Stella brand, title, network, time),
  toasts, the sky backdrop, and navigation gestures (tap the left of the status
  bar to go back, long-press it to go home).

## App model

An `App` has a `root()` screen and an `AppInfo` (id, name, icon, tint, flags).
Navigation is a per-app stack of `Screen`s (`push`/`pop`), so switching apps
resumes where you left. Screens get `onShow`/`onHide`, `onEvent`, and an
optional periodic `onTick` that returns its next delay (no timers to leak).
Apps are statically allocated and installed at boot; there is no dynamic
loading on a PSRAM-less ESP32.

## Power

`Active → Dimmed → Off` after configurable idle times. The touch that wakes a
dark (Off) display is consumed so it never activates what is under the finger;
a dimmed display is still readable, so a touch there brightens it and acts. Rendering
pauses while the backlight is off; the dirty region is kept for wake.

Brightness is stored as a *perceived* level; `backlightDuty()` maps it to PWM
duty with a 2.2 power curve (LEDs look much brighter than their duty cycle).
Dimmed is half the level, *Dim at night* 45% of it, so the slider keeps
working at night. The CYD drives its backlight with 20 kHz PWM (the Arduino
`analogWrite` default of 1 kHz can be seen as flicker).

## Calibration

Panels and touch controllers differ from batch to batch, so both are tuned on
the device and stored in the system settings (record v5):

- **Display** (*Settings > Display > Calibrate*): `Look` (tone, contrast,
  dithering) is applied when the theme is built; `Display::PanelTrim` (VCOM
  offset, frame rate) is sent to the panel by the runtime (after
  `waitIdle()`), only when it changes. Defaults render the design exactly;
  board sketches set their panel's defaults through `RuntimeConfig::defaults`,
  which also fill fields an older settings record lacks and are what each
  calibration tab's *Reset* restores (`AppContext::defaultSettings()`).
- **Touch** (*Touch test > Calibrate*): four crosses are tapped with the
  correction switched off; a least-squares fit per axis gives a scale and
  offset that the runtime applies to every touch before gestures. Implausible
  fits (over 20% scale or 60 px offset) are rejected.

## Updates (over the air)

`Settings > About > Software update` checks for, installs and restarts into a
new firmware, with no USB cable and nothing to set up beyond one flash.
**Installing is always the person's tap.** *Looking* can run by itself
(`SystemSettings::autoUpdate`, on by default, "Check daily" on the update
screen): the update job makes a quiet check two minutes after boot and then
once a day, only from a calm state (never over an offer or a result nobody has
seen), and shows nothing of a failure (it retries after 30 minutes, backing off
to 6 hours); an offer appears as "Update x.y.z" on Settings' About row
(`net/firmware_update.*`, `tools/update_tool.py`):

1. **Publishing is automatic.** The `Publish update` workflow runs on every
   merge to `main`: it builds the firmware and publishes it with a
   `manifest.json` to GitHub Pages (one-time: repository *Settings > Pages >
   Source: GitHub Actions*). The device's built-in address is
   `https://homura8.github.io/stella32/manifest.json` (override with
   `STELLA_UPDATE_URL` in `secrets.h`). An update is offered when
   `STELLA_VERSION` is *higher* than the running version, so bump it when you
   want one.
2. The **manifest** (HTTPS JSON, no redirects) names `version`, the image `url`,
   `size` and `sha256`. It is parsed by the bounded JSON parser and every field
   is validated (HTTPS only, sizes, hex lengths, a real `a.b.c` version).
3. The image streams into the **other** app slot while it is hashed. Size and
   SHA-256 must match the manifest exactly (more or fewer bytes, or a cut
   download, abort the install).
4. Only then is the slot chosen for the next boot, **pending**: the bootloader
   (rollback is enabled in Arduino-ESP32 3.3.12's SDK) returns to the previous
   image at the next reset unless the new one is confirmed. The runtime
   confirms after **one healthy minute** (`Runtime::step`); the sketch defers
   the Arduino core's own immediate confirmation with `verifyRollbackLater()`.
   A new image that crashes, hangs (the supervisor restarts it after 3
   minutes) or never draws a frame is therefore replaced by the old one by
   itself. A new update is refused until the last one is confirmed.

**When the device resets mid-update** (reported on a real CYD: the download
started and the device restarted at 0 %), nothing says why without a serial
cable. So the update leaves a small note in retained RAM (`update_crumb`, next
to the supervisor's restart record): *starting*, *downloading N %*,
*finishing*. The next boot reports it once, and Settings > About > Last reset
reads, for example, `Crash · update: downloading 34%` (or `Watchdog`,
`Brownout`, ...). Every handled outcome clears the note, so it only appears
after a real reset. The ESP32 slot also erases flash *as the image is written*
(`OTA_WITH_SEQUENTIAL_WRITES`) instead of the whole slot before the first byte:
one multi-second erase is the longest, most power-hungry operation of an
update, and the prime suspect for a reset right at the start (the failed
device reported *Watchdog* as its last reset, with plenty of heap and stack).
Every fourth chunk written also yields one tick, so the idle task that feeds
the task watchdog always gets to run, however fast the connection is.

Every failure (unreachable server, bad manifest, damaged or oversize download,
a flash write error, an invalid image) aborts the slot write and leaves the
running firmware untouched. The code never writes over the running image: a
single-app layout (the `cyd-huge` profile) has no other slot, so updates report
*unavailable* there.

**The one-time setup** is a USB flash of the default build: it uses the
two-slot layout (1.9 MB per slot; the firmware is 1.5 MB). Settings and the
saved Wi-Fi network are kept: both layouts keep NVS at 0x9000.

**What protects it, and what does not.** The download is HTTPS to the
update host (certificate chain verified against the core's bundle) and must
match the manifest's SHA-256 and size; the new image must also boot well to be
kept. That does *not* protect against a compromised update host: whoever can
publish to your GitHub Pages (or take over the GitHub account or its CI) could
publish firmware your device would install. For a personal device that is a
reasonable trade for zero setup. To close it, **optionally** sign updates:

```bash
pip install cryptography
python3 tools/update_tool.py keygen --private update-key.pem --public update-public.pem  # private key: offline, never in git
# put the printed STELLA_UPDATE_PUBLIC_KEY in examples/CYD/secrets.h and flash by USB once
python3 tools/update_tool.py sign --key update-key.pem --image CYD.ino.bin --version 0.6.1 --url https://... --out manifest.json
```

With a public key built in, the manifest must carry an ECDSA P-256 signature
(`sig_r`, `sig_s`) over SHA-256(`"stella-update-v1|<version>|<hex image
sha256>"`) that verifies with it (mbedTLS); the *version* is signed, so an old
signed image cannot be relabelled as new. Then even a compromised host cannot
install anything, but you must sign and upload each release yourself (the
workflow does not hold your private key).

The manifest's strings are limited to 95 characters (the device's JSON value
buffer). To go back to an older version, flash by USB.

## Persistence

`KeyValueStore` (NVS on ESP32) holds records with a magic number, schema
version, size and CRC-32. Torn, corrupt or foreign records are detected; an
older version is reported so the owner can migrate it. System settings are one
record, written 1.5 s after the last change so slider drags do not wear flash.
Apps get their own namespace (`AppContext::saveAppRecord`).

## Memory budget (CYD build, Arduino-ESP32 3.3.12)

| Item | Size |
| --- | --- |
| Program (flash) | 1,486,503 B of a 1,966,080 B app slot (74%); mbedTLS/HTTPS is most of it |
| Static RAM (globals) | 84,228 B; 243,452 B left for stacks and heap (the phone-setup server's four request slots are ~3 KB of it) |
| With DMA (`STELLA_CYD_DMA 1`) | not re-measured since 0.6.0 (then 1,495,365 B flash, 93,412 B static RAM) |
| Single-app layout (`cyd-huge`) | the same firmware in a 3 MB slot (46%), no updates |
| Render chunk buffer | 12,800 B (320 x 20 px); a second one with DMA |
| Service thread stack | 16 KiB (TLS handshakes) |
| Fonts (both densities, flash) | ~155 KB (the bedside numerals are ~56 KB of it) |

## Roadmap

Near-term work this foundation is designed for:

1. More boards (the T-Display S3 has buttons instead of touch) and an ESP-IDF
   port.

Done: alarms, timer and system alerts (0.3.0).
