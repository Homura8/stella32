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
 apps/        Launcher, Clock, Settings, Touch test            (only via AppContext)
 runtime/     Runtime: navigation, shell, power, settings, supervision, event loop
 app/         App / Screen / AppContext contract, system settings
 ui/          Theme (Celestia Glow tokens), materials, icons, layout DSL, widgets
 service/     Topic<T>, JobScheduler, SharedResource lease, Supervisor, DataStatus
 storage/     Versioned, checksummed records over KeyValueStore
 input/       Events, gesture recognizer, touch calibration
 gfx/ fonts/  Canvas (anti-aliased primitives, text), dirty-rect Renderer, fonts
 hal/         Display, TouchInput, Platform, KeyValueStore interfaces
 osal/        Mutex, RecursiveMutex, Signal, threads (FreeRTOS | std)
 core/        Geometry, colour, wrap-safe time, fixed containers, text, logging
 ───────────────────────────────────────────────────────────────────────────
 port/esp32/  Arduino-ESP32: Platform, NVS store, Wi-Fi job      (device only)
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
| Service (`stella-svc`) | `JobScheduler`: background jobs (Wi-Fi today; HTTP services next) | yes, by the UI thread (10 min) |

- **Apps run only on the UI thread.** No locks are needed in app code.
- **Jobs publish, apps read.** A job publishes a copy of its result to a
  `Topic<T>`; the topic posts one coalesced notification to the UI thread's
  event queue; the runtime calls `App::onTopic()` for running apps, which read
  the value and invalidate what changed.
- **Shared scarce resources** (the single TLS session a PSRAM-less ESP32 can
  afford) use `SharedResource`: Primary clients wait, Optional clients never
  start while a Primary waits. A busy lease is `JobResult::Deferred`, never a
  failure.
- **Retry policy** is declarative (`JobPolicy`): interval, exponential retry
  backoff with a cap, deferral delay, and "needs network" gating. Reconnecting
  or a manual refresh clears backoff.
- **Supervision.** Each thread beats its watch after each unit of work. If a
  thread is silent past its limit the other thread records the name in RTC
  memory and restarts the device; the next boot logs it.

Lock ordering: resource lease → data locks. No lock is held across flash, network
or display I/O.

## Rendering

There is no framebuffer (it would cost 150 KiB). The runtime keeps a
`DirtyRegion` (up to 8 merged rectangles). `Renderer::render()` replays the
frame's draw function once per *chunk* of each dirty rectangle, into one small
buffer (12.5 KiB on the CYD), and streams each chunk to the display. Narrow
updates (a toggle, the clock digits) fit in a single chunk.

The frame is: sky backdrop (`paintSky`) → status bar → top screen (clipped to the
content area) → toast. Draw code must be deterministic and must repaint every
pixel it covers; translucent glass blends over the backdrop drawn first.

Invariant (tested): any sequence of incremental frames produces the same pixels
as a full redraw. Idle screens render nothing.

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

## Persistence

`KeyValueStore` (NVS on ESP32) holds records with a magic number, schema
version, size and CRC-32. Torn, corrupt or foreign records are detected; an
older version is reported so the owner can migrate it. System settings are one
record, written 1.5 s after the last change so slider drags do not wear flash.
Apps get their own namespace (`AppContext::saveAppRecord`).

## Memory budget (CYD build, Arduino-ESP32 3.3.12)

| Item | Size |
| --- | --- |
| Program (flash) | 1,093,729 B of 3 MB (huge_app partition) |
| Static RAM (globals) | 65,260 B; 262,420 B left for stacks and heap |
| Render chunk buffer | 12,800 B (320 x 20 px) |
| Fonts (both densities, flash) | ~96 KB |

## Roadmap

Near-term work this foundation is designed for:

1. Wi-Fi provisioning (setup access point and web page) instead of `secrets.h`.
2. An HTTP(S) service layer: bounded body readers, the TLS lease, `DataStatus`
   freshness, and topics, ported from Celestia Nightstand's proven code.
3. DMA display transfer with double chunk buffers.
4. Screen transitions and gentle animations, opt-in and frame-budgeted.
5. More boards (e.g. 3.5" 480x320 panels, capacitive touch) and an ESP-IDF port.
6. OTA updates with rollback.
