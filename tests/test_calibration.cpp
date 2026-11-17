// Display and touch calibration, the pinned sky and the status bar's back
// target: fixes from the owner's sessions on a real CYD.
#include <string.h>

#include "sim_session.h"
#include "stella/ui/layout.h"
#include "test.h"

using namespace stella;
using namespace stella::host;
using stella::apps::fitTouchAxis;

namespace {

const char *topApp(SimSession &s) { return s.os.foreground() ? s.os.foreground()->info().id : ""; }

int luma(Color c) { return (red8(c) * 299 + green8(c) * 587 + blue8(c) * 114) / 1000; }

// Touch test geometry, from the same theme tokens as its layout().
struct TouchGeometry {
  Rect pad, button;
  Point target(int i, const Theme &t) const {
    const int inset = t.dp(24);
    const int x = (i == 0 || i == 3) ? pad.x + inset : pad.right() - 1 - inset;
    const int y = i < 2 ? pad.y + inset : pad.bottom() - 1 - inset;
    return Point{(int16_t)x, (int16_t)y};
  }
};
TouchGeometry touchGeometry(SimSession &s) {
  const Theme &t = s.os.theme();
  Rect r[2];
  layout::column(s.os.contentRect().inset(t.m.margin, t.m.gapS), {layout::flex(), layout::px(t.m.control)}, t.m.gapS,
                 r);
  Rect bottom[2];
  layout::row(r[1], {layout::flex(), layout::px(t.dp(104))}, t.m.gapM, bottom);
  return TouchGeometry{r[0], bottom[1]};
}

}  // namespace

TEST(touch_fit_recovers_offset_and_scale_and_rejects_nonsense) {
  const int target[4] = {34, 285, 285, 34};
  int measured[4];
  for (int i = 0; i < 4; ++i) measured[i] = target[i] - 7;  // the panel reads 7 px left
  int16_t scale = 0, offset = 0;
  CHECK(fitTouchAxis(measured, target, 4, scale, offset));
  CHECK(scale == 1024 && offset == 7);
  for (int i = 0; i < 4; ++i) measured[i] = (target[i] * 9) / 10 + 5;  // 10% short
  CHECK(fitTouchAxis(measured, target, 4, scale, offset));
  CHECK(scale > 1120 && scale < 1160);
  const int same[4] = {100, 100, 100, 100};
  CHECK(!fitTouchAxis(same, target, 4, scale, offset));  // every tap in one place
  const int far[4] = {200, 20, 20, 200};
  CHECK(!fitTouchAxis(far, target, 4, scale, offset));  // mirrored: a mis-tap
}

// Regression (CYD): touches with the stylus landed ~7 px left of the pen.
TEST(touch_calibration_by_touch_corrects_every_touch) {
  SimSession s(320, 240);
  s.os.launch("touch");
  s.run(100);
  const Theme &t = s.os.theme();
  const TouchGeometry g = touchGeometry(s);
  s.tap(g.button.centerX(), g.button.centerY());  // Calibrate
  for (int i = 0; i < 4; ++i) {
    const Point k = g.target(i, t);
    s.tap(k.x - 7, k.y + 3);  // this panel reads 7 px left and 3 px low
  }
  const SystemSettings &cal = s.os.settings();
  CHECK(cal.touchOffsetX == 7 && cal.touchOffsetY == -3);
  CHECK(cal.touchScaleX == 1024 && cal.touchScaleY == 1024);
  // From now on a touch reported at x - 7 lands on x: the Calibrate button
  // is hit even when the panel reads left of it.
  s.tap(g.button.x + 4, g.button.centerY());
  CHECK(strcmp(s.os.topScreen()->title(), "Calibrate touch") == 0);
  s.tap(g.button.x + 4, g.button.centerY());  // Cancel keeps the calibration
  CHECK(s.os.settings().touchOffsetX == 7);
}

TEST(touch_calibration_left_unfinished_restores_the_old_one) {
  SimSession s(320, 240);
  SystemSettings p = s.os.settings();
  p.touchOffsetX = 5;
  s.os.applySettings(p);
  s.os.launch("touch");
  s.run(100);
  const TouchGeometry g = touchGeometry(s);
  s.tap(g.button.centerX() - 5, g.button.centerY());
  CHECK(s.os.settings().touchOffsetX == 0);  // measuring raw positions
  s.os.goHome();
  s.run(100);
  CHECK(s.os.settings().touchOffsetX == 5);
}

TEST(leaving_the_touch_test_clears_its_trail) {
  SimSession s(320, 240);
  s.os.launch("touch");
  s.run(100);
  const std::vector<Color> clean = s.dev.display.framebuffer();
  const Rect c = s.os.contentRect();
  s.drag(c.x + 60, c.y + 50, c.x + 220, c.y + 120);
  CHECK(clean != s.dev.display.framebuffer());
  s.os.goHome();
  s.run(100);
  s.os.launch("touch");
  s.run(100);
  CHECK(clean == s.dev.display.framebuffer());
}

TEST(contrast_deepens_backgrounds_and_light_mode_text) {
  Look plain, deep;
  deep.contrast = 80;
  const Theme lightA = makeTheme(Density::Compact, false, SkyPhase::Day, plain);
  const Theme lightB = makeTheme(Density::Compact, false, SkyPhase::Day, deep);
  CHECK(luma(lightB.color.text) < luma(lightA.color.text));  // light-lilac text gets darker
  CHECK(luma(lightB.color.textMuted) < luma(lightA.color.textMuted));
  CHECK(luma(lightB.color.sky[0]) < luma(lightA.color.sky[0]));
  // Regression (CYD): in dark mode, contrast made toggles and accents vanish
  // into the background. Backgrounds deepen; light inks keep their level.
  const Theme darkA = makeTheme(Density::Compact, true, SkyPhase::Night, plain);
  const Theme darkB = makeTheme(Density::Compact, true, SkyPhase::Night, deep);
  CHECK(luma(darkB.color.sky[0]) < luma(darkA.color.sky[0]));
  CHECK(luma(darkB.color.glass) < luma(darkA.color.glass));
  CHECK(darkB.color.text == darkA.color.text && darkB.color.accent == darkA.color.accent);
  CHECK(darkB.color.line == darkA.color.line);
  // White stays white; mid greys deepen.
  CHECK(lookColor(deep, 0xffffff) == lookColor(plain, 0xffffff));
  CHECK(luma(lookColor(deep, 0x808080)) < luma(lookColor(plain, 0x808080)));
}

// Regression (CYD): with high contrast, pure white glared next to the
// near-whites ("looks HDR"). White level compresses only the highlights.
TEST(white_level_compresses_highlights_and_warmth_trims_blue) {
  Look full, soft;
  soft.whites = 40;
  CHECK(luma(lookColor(soft, 0xffffff)) < 240);
  CHECK(lookColor(soft, 0x606060) == lookColor(full, 0x606060));  // below the knee: unchanged
  const int gapFull = luma(lookColor(full, 0xffffff)) - luma(lookColor(full, 0xe0e0e0));
  const int gapSoft = luma(lookColor(soft, 0xffffff)) - luma(lookColor(soft, 0xe0e0e0));
  CHECK(gapSoft < gapFull);
  Look warm;
  warm.warmth = 100;
  const Color w = lookColor(warm, 0xffffff);
  CHECK(red8(w) > 245 && blue8(w) < red8(w) - 30 && green8(w) < red8(w));
}

TEST(calibration_settings_reach_the_panel_and_theme) {
  SimSession s(320, 240);
  const int before = s.dev.display.trims;
  SystemSettings p = s.os.settings();
  p.panelVcom = -6;
  p.panelRefreshHz = 100;
  p.contrastDark = 50;  // the fixture is at night: dark mode
  p.dither = false;
  s.os.applySettings(p);
  CHECK(s.dev.display.trims == before + 1);
  CHECK(s.dev.display.lastTrim.vcom == -6 && s.dev.display.lastTrim.refreshHz == 100);
  CHECK(s.os.theme().look.contrast == 50 && !s.os.theme().look.dither);
  s.os.applySettings(s.os.settings());  // unchanged trim: not resent
  CHECK(s.dev.display.trims == before + 1);
  s.run(200);
  const std::vector<Color> incremental = s.dev.display.framebuffer();
  s.os.invalidateAll();
  s.run(20);
  CHECK(incremental == s.dev.display.framebuffer());
}

TEST(settings_v3_records_are_migrated_with_board_defaults) {
  MemoryStore store;
  struct V3 {
    Appearance appearance;
    uint8_t brightness;
    uint16_t dimAfterSec, sleepAfterSec;
    bool clock24h;
    char timezone[48];
    bool nightDim;
    ColorTone colorTone;
  } v3{Appearance::Light, 150, 60, 0, true, "AEST-10", true, ColorTone::Vivid};
  CHECK(saveRecord(&store, "sys", "settings", 3, &v3, sizeof(v3)));
  SystemSettings s;
  s.contrastLight = 86;  // board defaults (the CYD sketch sets them)
  s.contrastDark = 74;
  bool migrated = false;
  CHECK(loadSettings(&store, s, &migrated) == RecordStatus::Ok && migrated);
  CHECK(s.brightness == 150 && s.colorTone == ColorTone::Vivid && s.contrastLight == 86 && s.contrastDark == 74 &&
        s.dither);
  CHECK(s.touchScaleX == 1024 && s.skyPin == 0);
}

// The sky swatches looked tappable but did nothing.
TEST(tapping_a_sky_keeps_it_and_tapping_again_follows_the_time) {
  SimSession s(320, 240, kMorningEpoch);  // day
  s.os.launch("settings");
  s.run(100);
  const Point row = s.settingsRow(2);
  s.tap(row.x, row.y);  // Appearance
  const Theme &t = s.os.theme();
  Rect r[3];
  layout::column(s.os.contentRect().inset(t.m.margin, t.m.gapS),
                 {layout::px(t.m.control), layout::px(t.font.caption->lineHeight), layout::flex()}, t.m.gapS, r);
  const Rect night = layout::grid(Rect(r[2].x, r[2].y + t.m.gapL, r[2].w, t.dp(40)), 4, 1, 3, t.m.gapS);
  s.tap(night.centerX(), night.centerY());
  CHECK(s.os.settings().skyPin == 4 && s.os.theme().phase == SkyPhase::Night);
  // Night dimming follows the clock, not the pinned sky: still full brightness.
  CHECK(s.dev.display.backlight() == backlightDuty(s.os.settings().brightness));
  s.tap(night.centerX(), night.centerY());
  CHECK(s.os.settings().skyPin == 0 && s.os.theme().phase == SkyPhase::Day);
}

// Regression (CYD): the back button was hard to hit.
TEST(back_target_is_at_least_32dp_tall_and_a_third_wide) {
  for (const int *size : {(const int[]){320, 240}, (const int[]){480, 320}}) {
    SimSession s(size[0], size[1]);
    const Theme &t = s.os.theme();
    CHECK(t.m.statusBar >= t.dp(32));
    s.os.launch("settings");
    s.run(100);
    const Point row = s.settingsRow(1);
    s.tap(row.x, row.y);
    CHECK(s.os.topScreen() != &s.settings.root());
    s.tap(size[0] / 3 - 4, t.m.statusBar - 2);  // bottom-right corner of the target
    CHECK(s.os.topScreen() == &s.settings.root() && strcmp(topApp(s), "settings") == 0);
  }
}

TEST(display_screen_rows_and_calibrate) {
  SimSession s(320, 240);
  s.os.launch("settings");
  s.run(100);
  const Point row = s.settingsRow(1);
  s.tap(row.x, row.y);  // Display
  const Theme &t = s.os.theme();
  const Rect area = s.os.contentRect().inset(t.m.margin, t.m.gapS);
  const int want = t.m.control + t.dp(4), fit = (area.h - 2 * t.dp(4)) / 5;
  const int rowH = want <= fit ? want : fit < t.dp(32) ? t.dp(32) : fit;
  CHECK(rowH >= t.dp(32));
  const int top = area.y + t.dp(4);
  // Row 1: Dim at night toggles.
  const bool before = s.os.settings().nightDim;
  s.tap(area.right() - t.m.padding - t.dp(20), top + rowH + rowH / 2);
  CHECK(s.os.settings().nightDim != before);
  // Row 4: Calibrate opens the calibration screen.
  s.tap(area.centerX(), top + 4 * rowH + rowH / 2);
  CHECK(s.os.topScreen()->title() && strcmp(s.os.topScreen()->title(), "Calibrate") == 0);
  // Its tabs switch between Tone, Colour and Panel.
  const int tabsW = area.w - t.dp(72) - t.m.gapM;
  s.tap(area.x + tabsW * 5 / 6, area.y + t.m.control / 2);
  s.run(100);
  CHECK(!s.os.renderPending());
}

TEST(calibration_screens_render_at_every_size) {
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    SimSession s(sz[0], sz[1]);
    const Theme &t = s.os.theme();
    s.os.launch("settings");
    s.run(100);
    const Point display = s.settingsRow(1);
    s.tap(display.x, display.y);
    const Rect area = s.os.contentRect().inset(t.m.margin, t.m.gapS);
    const int want = t.m.control + t.dp(4), fit = (area.h - 2 * t.dp(4)) / 5;
    const int rowH = want <= fit ? want : fit < t.dp(32) ? t.dp(32) : fit;
    CHECK(area.y + t.dp(4) + 5 * rowH <= area.bottom() + t.dp(8));  // the card fits
    s.tap(area.centerX(), area.y + t.dp(4) + 4 * rowH + rowH / 2);
    CHECK(s.os.topScreen()->title() && strcmp(s.os.topScreen()->title(), "Calibrate") == 0);
    const int tabsW = area.w - t.dp(72) - t.m.gapM;
    for (int tab = 1; tab < 3; ++tab) {
      s.tap(area.x + tabsW * (2 * tab + 1) / 6, area.y + t.m.control / 2);
      s.run(100);
      CHECK(!s.os.renderPending());
    }
  }
}

TEST(settings_v4_records_are_migrated_keeping_calibration) {
  MemoryStore store;
  struct V4 {
    Appearance appearance;
    uint8_t brightness;
    uint16_t dimAfterSec, sleepAfterSec;
    bool clock24h;
    char timezone[48];
    bool nightDim;
    ColorTone colorTone;
    uint8_t contrast;
    bool dither;
    int8_t panelVcom;
    uint8_t panelRefreshHz;
    uint8_t skyPin;
    int16_t touchScaleX, touchOffsetX, touchScaleY, touchOffsetY;
  } v4{Appearance::Dark, 120, 60, 0, true, "AEST-10", true, ColorTone::Rich, 74, false, -4, 100, 2,
       1030, 7, 1020, -2};
  CHECK(saveRecord(&store, "sys", "settings", 4, &v4, sizeof(v4)));
  SystemSettings s;
  s.contrastLight = 86;
  s.contrastDark = 74;
  s.whiteLevel = 70;
  bool migrated = false;
  CHECK(loadSettings(&store, s, &migrated) == RecordStatus::Ok && migrated);
  CHECK(s.colorTone == ColorTone::Rich && !s.dither && s.panelVcom == -4 && s.panelRefreshHz == 100 && s.skyPin == 2);
  CHECK(s.touchScaleX == 1030 && s.touchOffsetX == 7 && s.touchScaleY == 1020 && s.touchOffsetY == -2);
  CHECK(s.contrastLight == 86 && s.contrastDark == 74 && s.whiteLevel == 70);  // board defaults
}

TEST(calibrate_reset_restores_the_board_defaults_for_its_tab) {
  SimSession s(320, 240);
  SystemSettings p = s.os.settings();
  p.whiteLevel = 20;
  p.warmth = 90;
  p.colorTone = ColorTone::Vivid;
  s.os.applySettings(p);
  s.os.launch("settings");
  s.run(100);
  const Point display = s.settingsRow(1);
  s.tap(display.x, display.y);
  const Theme &t = s.os.theme();
  const Rect area = s.os.contentRect().inset(t.m.margin, t.m.gapS);
  const int want = t.m.control + t.dp(4), fit = (area.h - 2 * t.dp(4)) / 5;
  const int rowH = want <= fit ? want : fit < t.dp(32) ? t.dp(32) : fit;
  s.tap(area.centerX(), area.y + t.dp(4) + 4 * rowH + rowH / 2);  // Calibrate
  s.tap(area.right() - t.dp(36), area.y + t.m.control / 2);       // Reset (Tone tab)
  CHECK(s.os.settings().whiteLevel == s.os.defaultSettings().whiteLevel);
  CHECK(s.os.settings().warmth == s.os.defaultSettings().warmth);
  CHECK(s.os.settings().colorTone == ColorTone::Vivid);  // other tabs untouched
}
