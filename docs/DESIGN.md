# Design system: Celestia Glow for small screens

Stella's interface inherits the visual language of
[Celestia Glow](https://github.com/Homura8/celestiaglow): a *frosted
observatory*: soft pastel skies, translucent glass surfaces, quiet text colours
and playful rounded forms. Everything here is implemented in
`src/stella/ui/theme.cpp` (tokens) and `src/stella/ui/materials.cpp` (surfaces);
change those, not individual screens.

![Compact screens](screenshots/overview.png)

## Principles

- **Calm.** One accent at a time, muted secondary text, generous spacing.
  Every element earns its space.
- **Soft.** Rounded cards and pill controls; anti-aliased edges everywhere.
- **Atmospheric.** The sky changes with the time of day; nights are starlit
  and dim for a bedside device.
- **Legible first.** Decorative effects never reduce text contrast; touch
  targets stay at least 32 dp tall.

## Sky (background)

`paintSky()` draws the site's 120° three-stop gradient (`--bg-1..3`) with soft
pink (top-left), sky-tint (top-right) and gold (bottom) washes, using ordered
dithering so RGB565 never bands.

| Phase | Hours | Light accent | Dark accent |
| --- | --- | --- | --- |
| Dawn | 05–08 | `#85586b` | `#e1bccb` |
| Day | 08–17 | `#5d6980` | `#c1cbe0` |
| Dusk | 17–20 | `#775386` | `#d6b7e5` |
| Night | 20–05 | `#625e84` | `#bdbde7` |

Appearance `Auto` is light at dawn/day and dark ("starlit") at dusk/night;
`Light` and `Dark` pin the mode while the sky still follows the phase.

## Materials

| Token | Light | Dark | Use |
| --- | --- | --- | --- |
| `text` | `#3d354d` | `#eee8f5` | primary text |
| `textMuted` | `#685e74` | `#bcb2c9` | captions, values, chevrons |
| `glass` | `#fffdff` @ 70% | `#2e283f` @ 83% | cards, pills |
| `rim` | white @ 94% | `#eddeff` @ 18% | 1 px highlight edge on glass |
| `line` | `#5d4878` @ 13% | `#d7c5ef` @ 16% | hairlines, tracks |
| `shadow` | `#4b346c` @ 12% | `#080414` @ 27% | soft two-layer drop shadow |
| `surface` | `#faf7fb` | `#302a3d` | opaque chips, toggle/slider knobs |
| `tints[0..3]` | pink, lavender, sky, peach | deep versions | icon bubbles |

`paintGlass()` = shadow + translucent fill + rim. Raised surfaces cast shadow
`glassOutset()` pixels outside their rectangle; invalidate that margin too.

## Typography

| Role | Font | Compact | Regular | Use |
| --- | --- | --- | --- | --- |
| `caption` | Quicksand 700 | 11 px | 13 px | labels, values, status bar |
| `body` | Quicksand 600 | 14 px | 17 px | default text, list rows |
| `title` | Baloo 2 700 | 19 px | 24 px | screen titles, emphasis |
| `headline` | Baloo 2 700 | 26 px | 34 px | greetings, big values |
| `display` | Baloo 2 700 | 64 px | 88 px | clock digits only |

Fonts are rasterised with 4-bit anti-aliasing by `tools/fontgen.py` from the
site's own WOFF files.

## Metrics

All lengths come from `Theme::m` (and `t.dp(n)` for rare custom lengths, where
16 = 1.0x on compact panels).

| Token | Compact | Regular |
| --- | --- | --- |
| `margin` (screen edge) | 10 | 14 |
| `gapS / gapM / gapL` | 4 / 8 / 12 | 6 / 11 / 17 |
| `padding` (inside cards) | 10 | 14 |
| `radiusCard / radiusControl` | 16 / 12 | 22 / 16 |
| `statusBar` | 26 | 34 |
| `control` (buttons, segmented) | 36 | 50 |
| `row` (list rows) | 40 | 56 |
| `icon` | 24 | 32 |

## Components

- **Status bar:** sparkle + "Stella" at home; back chevron + screen title
  elsewhere (tap the left third to go back, long-press anywhere on it for
  home). Network icon and time at the right, in `textMuted`.
- **Glass card:** groups related rows; rows separated by hairlines.
- **List row:** icon bubble, title, optional muted value, chevron.
- **Segmented choice:** glass pill; the selected chip is an opaque `surface`
  chip with a hairline and accent text (the site's sky chooser).
- **Toggle:** pill track (accent when on) with a soft knob.
- **Slider:** thin track, accent fill, `surface` knob with an accent ring.
- **Toast:** glass pill above the bottom edge, 2.2 s.
- **Keyboard:** four rows sized from its bounds; letter keys are glass,
  function keys `surface` chips, the action key is an accent pill (greyed
  until the input is valid). One-shot shift, two symbol pages; long-press
  delete clears.
- **Text field:** glass pill, accent caret; secrets show bullets with a
  *Show/Hide* toggle.
- **Wi-Fi:** signal shown as 0-4 arcs (`icons::wifiBars`); the status bar
  icon is muted when connected, accent while joining, soft pink on failure.
- **Alert:** a soft veil over the whole screen and a near-opaque glass card:
  icon bubble (in the app's tint) with a `headline` title, a muted message of
  up to two lines, then a glass secondary and an accent primary button.
- **Alarms:** "Next alarm in ..." caption; a glass list of alarms (time in
  `title`, days in `caption`, a switch). The editor has display-font hour and
  minute digits (tap the top/bottom half or drag to change), an AM/PM pill in
  12-hour mode, Monday-first day chips (accent when on) and Delete/Save.
- **Timer:** display-font countdown over a thin accent progress track, a
  muted "Ends at ..." line, preset chips and Reset / +1 min / Start.
- **Weather:** large display-font temperature, condition in `title`, three
  (compact) or four (regular) glass forecast cards, a muted freshness line.

## Reviewing visual changes

`python3 tools/screenshots.py` renders every screen with the production
renderer at 320x240 (compact) and 480x320 (regular), at night (dark) and in
the morning (light), into `docs/screenshots/`. Look at the PNGs before and
after every UI change, and commit them with it. CI fails if the rendered UI
differs from the committed screenshots.
