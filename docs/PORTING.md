# Adding a board or display

Stella separates *what a device is* (board support) from *what the OS does*.
A new board touches only `src/stella/boards/<board>/` and a new example sketch;
apps and the runtime do not change.

## Steps

1. **Profile.** Copy `src/stella/boards/cyd/` to `src/stella/boards/<board>/`.
   Set the panel size *after rotation*, rotation, colour inversion, backlight
   pin, touch controller pins and calibration, and the chunk height
   (`kRenderRows`: `width * rows * 2` bytes of RAM; 20 rows is a good start).
2. **Display.** Implement `stella::Display`: `width()`, `height()`,
   `writePixels(rect, pixels)` (contiguous native-order RGB565, `rect.w *
   rect.h` pixels; finish reading before returning) and `setBacklight(0..255)`.
3. **Touch.** Implement `stella::TouchInput::sample()`, returning screen
   coordinates via `mapTouch()` with the board's `TouchCalibration`. Use the
   *Touch test* app on the device to check the corner targets.
4. **Platform, storage and network.** On ESP32 reuse
   `port/esp32/esp32_port.h` (`Esp32Platform`, `NvsStore`, `Esp32WifiDriver`,
   `Esp32HttpClient`).
5. **Sound (optional).** Implement `stella::Sound::tone(hz)` (non-blocking;
   0 = silence) for a buzzer or speaker; `CydSound` drives the CYD's speaker
   pin with LEDC PWM. Without it, alerts are silent but still shown.
6. **Sketch.** Copy `examples/CYD/` to `examples/<Board>/` with its own
   `tft_setup.h` (if it uses TFT_eSPI) and `sketch.yaml` profile, and add a CI
   compile step.
7. **Simulate.** Add the resolution to the size list in
   `tests/test_runtime.cpp` (`every_screen_renders_at_every_size`) and, if it
   is a new density class, to `host/sim_main.cpp`. Review the screenshots.
8. **Verify on hardware** before claiming support: boot, every screen, touch
   accuracy at the corners, backlight levels, an overnight run.

## Density classes

The theme picks a density from the panel's short side: `< 300 px` is
`Compact`, otherwise `Regular`. A very different panel (e.g. a round display
or 800x480+) may warrant a new class in `ui/theme.cpp` with its own metrics
and font set (`tools/fontgen.py`).

## Non-TFT_eSPI displays

`Display` is a plain interface; any driver works (esp_lcd, LovyanGFX,
a custom SPI driver). Only the board folder knows about it.
