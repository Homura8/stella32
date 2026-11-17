# Stella/32 — Agent Instructions

These rules apply to coding agents (and humans) changing this repository. They
exist to keep a small embedded OS correct, portable and pleasant to iterate on.
Read this file completely before making non-trivial changes.

## 1. What this project is

Stella/32 is a lightweight operating environment for ESP32 touch devices: a
multi-app runtime, a Celestia Glow UI toolkit, a dirty-rectangle renderer,
gesture input, background jobs, supervision and versioned settings.

- **First target:** ESP32-2432S028R "Cheap Yellow Display" (classic ESP32, no
  PSRAM, 320x240 ILI9341, XPT2046 resistive touch).
- **Designed for more screens:** nothing above the HAL may assume 320x240.
- **Visual language:** Celestia Glow (<https://github.com/Homura8/celestiaglow>):
  soft pastel skies, frosted glass, quiet text, rounded forms, Quicksand and
  Baloo 2. See `docs/DESIGN.md`.

Hardware status is recorded in `README.md`. **Never claim behaviour was verified
on a device unless it was actually run there.**

## 2. Read before modifying

At minimum, read the files you will touch plus:

| Change | Read first |
| --- | --- |
| Any | `README.md`, `docs/ARCHITECTURE.md`, this file |
| Screens, widgets, colours, spacing | `docs/DESIGN.md`, `src/stella/ui/theme.*`, `src/stella/ui/layout.h` |
| A new or changed app | `docs/APPS.md`, `src/stella/app/app.h`, an existing app in `src/stella/apps/` |
| A new board or display | `docs/PORTING.md`, `src/stella/hal/hal.h`, `src/stella/boards/cyd/` |
| Threads, jobs, data from the network | `docs/ARCHITECTURE.md` (threads, networking), `src/stella/service/`, `src/stella/net/`, the Weather app as an example |
| Time-based work, alerts, sound | `docs/APPS.md` (time-based work and alerts), the Timer and Alarms apps |
| Persistent data | `src/stella/storage/record.h` |

## 3. Authority order

When guidance conflicts, prefer:

1. the explicit, current request of the repository owner;
2. recorded decisions in this file and `docs/`;
3. tests and reproducible behaviour (host tests, screenshots, firmware build);
4. the established architecture;
5. existing reviewed implementation;
6. model preference.

Confidence is not evidence.

## 4. Invariants (do not break)

These are enforced by `tools/check_architecture.py`, the tests, or review. If a
change needs to break one, stop and ask the owner; then update this file.

1. **Layering.** `src/stella/<layer>/` may only include the layers allowed in
   `tools/check_architecture.py`. Apps never include `hal/`, `osal/`,
   `runtime/`, `port/` or `boards/`; they talk to the OS only through
   `AppContext`.
2. **Portability.** Only `port/`, `boards/` and `osal/osal_freertos.cpp` may
   include Arduino, ESP-IDF, FreeRTOS or driver headers. Everything else
   builds and is tested on the host.
3. **No heap after boot** in `src/stella` (outside `port/`/`boards/`): no
   `malloc`/`new`/`std::vector`/`std::string`/`std::function`. Use fixed
   capacities (`core/containers.h`) and report overflow.
4. **One UI thread.** All app, screen, widget and runtime code runs on the UI
   thread. Background work is a `Job` on the service thread. Data crosses
   threads only through `Topic<T>` (published copies) and the runtime's event
   queue. No shared mutable globals.
5. **Deferred is not Failed.** A busy shared resource (`SharedResource` lease)
   means `JobResult::Deferred`: keep last-good data, error text and backoff.
6. **Lock ordering.** Resource lease before any data lock; never hold a lock
   across network, flash (NVS) or display I/O.
7. **Rendering contract.** `Screen::draw()` is deterministic, has no side
   effects, and is replayed per chunk; anything it paints must change only
   after `invalidate()` of the affected rectangle (include `glassOutset()` for
   raised surfaces). Incremental rendering must equal a full redraw (tested).
8. **Design tokens only.** Screens and widgets use `Theme` colours, metrics,
   radii and fonts. No colour literals and no pixel coordinates in apps;
   use `t.dp(n)` for the rare custom length.
9. **Layouts live in `layout()`.** Every rectangle a screen uses is computed in
   `Screen::layout()` with the `layout::` DSL from the content rect and theme.
   `draw()` and `onEvent()` only read those rectangles.
10. **Every screen works at every density.** The tests render all screens at
    240x320, 320x240, 480x320 and 800x480; keep it that way when adding screens.
11. **Persisted structs are versioned.** Changing a stored struct's layout
    means bumping its version and handling `RecordStatus::OldVersion`. Keys and
    app ids are at most 15 characters.
12. **Wrap-safe time.** Compare millisecond timestamps only with
    `core/deadline.h` helpers, never with `<`/`>`.
13. **Supervision.** Long-running threads beat their `Supervisor` watch after
    each unit of work. A new thread needs a watch with a limit several times its
    worst-case gap.
14. **Foreground-only painting.** Only the foreground app repaints; the
    runtime drops background invalidations. Do not work around it.
15. **Secrets.** Wi-Fi passwords and API keys live only in versioned records
    or `secrets.h`; wipe transient copies (see `WifiManager`, `TextEntryScreen`)
    and never put them in logs, URLs that are logged, or topics.
16. **Generated files.** `src/stella/fonts/fonts_*.cpp` come from
    `tools/fontgen.py`; `docs/screenshots/` from `tools/screenshots.py`. Edit
    the generator, never the output.

## 5. How to make common changes

- **Change a screen's layout:** edit only that screen's `layout()` (rows,
  columns, flex weights, theme tokens). Rebuild, regenerate screenshots, look
  at both densities and both skies.
- **Restyle the system:** edit `src/stella/ui/theme.cpp` (palette, metrics) or
  `materials.cpp` (sky, glass). All apps follow automatically.
- **Add an app:** follow `docs/APPS.md`. Add it to the simulator
  (`host/sim_session.*`, `host/sim_main.cpp`) and the tests.
- **Add a widget:** put it in `src/stella/ui/widgets.*` as a small value type
  with `draw()` and `handle()`; add tests in `tests/test_input_ui.cpp`.
- **Add a background service:** implement a `Job`, register it and its
  `Topic<T>` in `App::onInstall(ServiceHost&)` (topic ids from
  `topics::kFirstUser`), fetch with `HttpService::getJson`, track freshness
  with `DataStatus`, and return `FetchResult::jobResult()` (lease contention
  is `Deferred`). Copy the Weather app's structure.
- **Do something at a time, in any app:** use `App::onWake()` (returning the
  nearest deadline) and raise a system alert with `AppContext::showAlert()`;
  `onTick()` only runs while a screen is visible. Copy the Timer app.
- **Add a board or display:** follow `docs/PORTING.md`. Do not touch apps.
- **Change fonts or sizes:** edit `ROLES` in `tools/fontgen.py`, run it, commit
  the regenerated `fonts_*.cpp`, and check flash size.

## 6. Verification before declaring work complete

Run, from the repository root:

```bash
python3 tools/check_architecture.py
cmake -S . -B build && cmake --build build -j && ./build/stella_tests
python3 tools/screenshots.py --check
```

For UI changes, regenerate with `python3 tools/screenshots.py`, **look at the
PNGs** (`docs/screenshots/overview.png` and the 480x320 set), and commit them
with the change. For firmware-affecting changes also run
`arduino-cli compile --profile cyd examples/CYD` and report flash/RAM.

If a command cannot be run, say so rather than implying it passed. Add a
regression test for every real bug you fix.

## 7. Change discipline

- Keep changes narrowly scoped; do not rewrite working code into a preferred
  style or add speculative abstractions.
- Preserve user-visible behaviour unless the request requires otherwise.
- Update docs in the same change when commands, architecture rules, design
  tokens or support claims change.
- Dependencies: justify any new one (licence, flash/RAM, maintenance). Prefer
  small, clear code over a library.

## 8. Security and privacy

- Never commit `secrets.h`, Wi-Fi credentials, API keys, tokens, private URLs,
  device logs with personal data, or firmware binaries.
- Never log credentials or tokens; log presence/length instead.
- Treat all network data as untrusted input with bounded parsing.

## 9. UI quality (Celestia Glow)

Stella should feel calm, soft and deliberate.

- Every element earns its space; prefer fewer, clearer controls.
- Use glass cards for grouped content, pills for choices, muted captions for
  secondary text, the phase accent for emphasis. Never pure black/white text.
- Keep motion and redraws minimal; idle screens should not repaint.
- Touch targets stay at least `t.dp(32)` high.

## 10. Git and pull requests

- Never commit directly to `main`; work on a dedicated branch.
- Keep commits focused; review the diff for accidental changes.
- Open or update a pull request targeting `main` with: summary, checks run and
  results, flash/RAM impact for firmware changes, dependency changes, known
  limitations, and the hardware checks the owner should perform.
- Never merge pull requests; merging is reserved for the owner.
