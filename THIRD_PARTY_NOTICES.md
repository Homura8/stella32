# Third-party notices

## Fonts

The anti-aliased glyph tables in `src/stella/fonts/fonts_*.cpp` are rasterised
by `tools/fontgen.py` from the WOFF files in `tools/fonts/`, taken from the
owner's Celestia Glow repository (<https://github.com/Homura8/celestiaglow>):

- **Quicksand** (600, 700): Copyright 2011 The Quicksand Project Authors, with
  Reserved Font Name "Quicksand". SIL Open Font License 1.1:
  `tools/fonts/quicksand-OFL.txt`.
- **Baloo 2** (700): Copyright 2019 The Baloo 2 Project Authors. SIL Open Font
  License 1.1: `tools/fonts/baloo-2-OFL.txt`.

The rasterised tables are derived bitmaps of these fonts, are distributed under
the same licence, and are referred to in code by role (caption, body, title,
headline, display) rather than by the reserved font names.

## Design and code lineage

- The visual language (palettes, glass materials, sky phases, typography)
  follows Celestia Glow, the owner's own project.
- The gesture debouncing, shared TLS lease, task supervision, wrap-safe time
  and checksummed records adapt ideas from Celestia Nightstand, the owner's
  own project.

## Build-time dependencies (not bundled)

Arduino-ESP32 (Espressif, LGPL-2.1/Apache-2.0 components), TFT_eSPI (Bodmer,
FreeBSD), XPT2046_Touchscreen (Paul Stoffregen, MIT). They are fetched by
arduino-cli from their upstream sources.
