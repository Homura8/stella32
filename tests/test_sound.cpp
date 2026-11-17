// Speaker volume: the setting reaches the board, the preview chime is short and
// polite, and old settings records gain the new fields with their defaults.
#include <string.h>

#include "sim_session.h"
#include "stella/app/system_settings.h"
#include "stella/storage/record.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

TEST(volume_reaches_the_speaker_and_is_clamped) {
  SimSession s(320, 240);
  CHECK_EQ(s.dev.sound.volume(), 60);  // the default, set at boot
  SystemSettings st = s.os.settings();
  st.volume = 25;
  s.os.applySettings(st);
  CHECK_EQ(s.dev.sound.volume(), 25);
  st.volume = 250;  // out of range
  s.os.applySettings(st);
  CHECK_EQ(s.os.settings().volume, 100);
  CHECK_EQ(s.dev.sound.volume(), 100);
}

TEST(preview_chime_plays_two_notes_then_stops_and_respects_mute) {
  SimSession s(320, 240);
  const int before = s.dev.sound.tones();
  s.os.previewChime();
  s.run(600);
  CHECK_EQ(s.dev.sound.tones() - before, 2);
  CHECK_EQ(s.dev.sound.current(), 0);  // silent afterwards
  SystemSettings st = s.os.settings();
  st.volume = 0;
  s.os.applySettings(st);
  const int muted = s.dev.sound.tones();
  s.os.previewChime();
  s.run(600);
  CHECK_EQ(s.dev.sound.tones(), muted);
}

TEST(settings_v6_records_gain_volume_and_auto_update_defaults) {
  MemoryStore store;
  struct V6 {
    Appearance appearance;
    uint8_t brightness;
    uint16_t dimAfterSec, sleepAfterSec;
    bool clock24h;
    char timezone[48];
    bool nightDim;
    uint8_t bedside;
    ColorTone colorTone;
    uint8_t contrastLight, contrastDark, whiteLevel, warmth;
    bool dither;
    int8_t panelVcom;
    uint8_t panelRefreshHz;
    uint8_t skyPin;
    int16_t touchScaleX, touchOffsetX, touchScaleY, touchOffsetY;
  } v6{Appearance::Dark, 130, 60, 0, false, "AEST-10", true, 2, ColorTone::Rich, 86, 74, 70, 30, true, 0, 0, 0,
       1024, 7, 1024, 0};
  CHECK(saveRecord(&store, "sys", "settings", 6, &v6, sizeof(v6)));
  SystemSettings s;
  bool migrated = false;
  CHECK(loadSettings(&store, s, &migrated) == RecordStatus::Ok);
  CHECK(migrated);
  CHECK(s.appearance == Appearance::Dark && s.brightness == 130 && !s.clock24h);
  CHECK(strcmp(s.timezone, "AEST-10") == 0 && s.bedside == 2 && s.touchOffsetX == 7);
  CHECK_EQ(s.volume, 60);
  CHECK(s.autoUpdate);
  // And the new layout round-trips.
  s.volume = 35;
  s.autoUpdate = false;
  CHECK(saveSettings(&store, s));
  SystemSettings again;
  CHECK(loadSettings(&store, again, &migrated) == RecordStatus::Ok && !migrated);
  CHECK(again.volume == 35 && !again.autoUpdate);
}
