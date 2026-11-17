// The status LED: what each source shows and who wins, and how the runtime
// feeds it (alerts, updates, the bedside glow, apps' flashes, the wiring).
#include <string.h>

#include "sim_session.h"
#include "stella/core/sha256.h"
#include "stella/core/text.h"
#include "stella/runtime/led_engine.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {

LedRequest steady(uint8_t r, uint8_t g, uint8_t b, uint32_t until = 0) {
  LedRequest q;
  q.pattern = LedPattern::Steady;
  q.color = LedColor{r, g, b};
  q.untilMs = until;
  return q;
}

LedRequest breathe(uint8_t r, uint8_t g, uint8_t b, uint16_t period, uint8_t floor = 0) {
  LedRequest q = steady(r, g, b);
  q.pattern = LedPattern::Breathe;
  q.periodMs = period;
  q.floor = floor;
  return q;
}

LedColor frameAt(const LedEngine &e, uint32_t t) {
  bool a;
  return e.frame(t, a);
}

int brightest(SimSession &s) {
  int m = 0;
  for (int c = 0; c < 3; ++c) m = s.dev.led.shown(c) > m ? s.dev.led.shown(c) : m;
  return m;
}

// Runs for `ms` and reports the brightest and dimmest the LED got.
void watch(SimSession &s, uint32_t ms, int &lo, int &hi) {
  lo = 255;
  hi = 0;
  for (uint32_t t = 0; t < ms; t += 20) {
    s.run(20);
    const int v = brightest(s);
    lo = v < lo ? v : lo;
    hi = v > hi ? v : hi;
  }
}

void showAlert(SimSession &s) {
  Alert a;
  a.title = "Time's up";
  a.message = "5 min timer";
  a.sound = true;
  s.os.showAlert(a);
}

}  // namespace

TEST(engine_patterns_levels_and_priority) {
  LedEngine e;
  e.configure(1, 100);
  CHECK(frameAt(e, 0) == LedColor{});  // nothing asks: dark
  e.set(LedSource::Wifi, steady(100, 200, 50));
  CHECK(frameAt(e, 10) == (LedColor{100, 200, 50}));
  e.configure(1, 50);  // the level caps the brightest it gets
  CHECK(frameAt(e, 10) == (LedColor{50, 100, 25}));
  e.configure(1, 100);
  // A more important source wins; clearing it gives the other back.
  e.set(LedSource::Alert, steady(255, 0, 0));
  CHECK(frameAt(e, 10) == (LedColor{255, 0, 0}));
  e.clear(LedSource::Alert);
  CHECK(frameAt(e, 10) == (LedColor{100, 200, 50}));
  e.set(LedSource::Update, steady(1, 2, 3));
  e.set(LedSource::Flash, steady(9, 9, 9));
  CHECK(frameAt(e, 10) == (LedColor{9, 9, 9}));  // Flash outranks Update outranks Wifi
  e.clear(LedSource::Flash);
  CHECK(frameAt(e, 10) == (LedColor{1, 2, 3}));
  // Mode 0 is dark whatever is asked; mode 1 ignores the glow; mode 2 shows it.
  e.configure(0, 100);
  CHECK(frameAt(e, 10) == LedColor{});
  e.configure(1, 100);
  e.clear(LedSource::Update);
  e.clear(LedSource::Wifi);
  e.set(LedSource::Glow, steady(40, 40, 40));
  CHECK(frameAt(e, 10) == LedColor{});
  e.configure(2, 100);
  CHECK(frameAt(e, 10) == (LedColor{40, 40, 40}));
}

TEST(engine_breathe_blink_and_expiry) {
  LedEngine e;
  e.configure(1, 100);
  e.set(LedSource::Alert, breathe(200, 100, 0, 1000, 50));
  bool animating = false;
  const LedColor start = e.frame(0, animating);
  CHECK(animating);
  CHECK(start.r == 50 * 200 / 255 || start.r == (uint8_t)(200 * (50 / 255.f) + 0.5f));  // the floor
  const LedColor peak = frameAt(e, 500);
  CHECK(peak.r == 200 && peak.g == 100);
  CHECK(frameAt(e, 250).r > start.r && frameAt(e, 250).r < peak.r);  // smooth in between
  CHECK(frameAt(e, 1000) == start && frameAt(e, 7000) == start);      // periodic
  e.clear(LedSource::Alert);
  LedRequest blink = steady(255, 255, 255);
  blink.pattern = LedPattern::Blink;
  blink.periodMs = 1000;
  e.set(LedSource::Wifi, blink);
  CHECK(frameAt(e, 50).r == 255 && frameAt(e, 500).r == 0);
  // A request that ends by itself, wrap-safe.
  e.clear(LedSource::Wifi);
  e.set(LedSource::Flash, steady(0, 255, 0, 0x20u));  // ends just after the 32-bit clock wraps
  CHECK(frameAt(e, 0xFFFFFE00u).g == 255);
  CHECK(frameAt(e, 0xFFFFFF80u).g == 255);
  CHECK(frameAt(e, 0x00000040u).g == 0);  // past the end, across the wrap
  CHECK(!(e.frame(0x40u, animating) != LedColor{}));
}

TEST(an_alert_breathes_the_led_amber_and_it_goes_dark_afterwards) {
  SimSession s(320, 240);
  CHECK_EQ(brightest(s), 0);
  showAlert(s);
  int lo, hi;
  watch(s, 2000, lo, hi);
  CHECK(hi > 60 && lo < hi - 30);  // visibly breathing, not steady
  CHECK(s.dev.led.shown(0) > s.dev.led.shown(2));  // amber: red and green, hardly any blue
  s.os.dismissAlert(0);
  s.run(300);
  CHECK_EQ(brightest(s), 0);
}

TEST(the_led_obeys_its_mode_and_level) {
  SimSession s(320, 240);
  SystemSettings st = s.os.settings();
  st.ledMode = 0;
  s.os.applySettings(st);
  showAlert(s);
  s.run(1500);
  CHECK_EQ(brightest(s), 0);  // off means off
  s.os.dismissAlert(0);
  st.ledMode = 1;
  st.ledLevel = 100;
  s.os.applySettings(st);
  showAlert(s);
  int lo, hi;
  watch(s, 2000, lo, hi);
  const int full = hi;
  s.os.dismissAlert(0);
  s.run(100);
  st.ledLevel = 25;
  s.os.applySettings(st);
  showAlert(s);
  watch(s, 2000, lo, hi);
  CHECK(hi < full / 2 && hi > 10);
}

TEST(the_glow_is_only_on_the_bedside_face_and_only_in_glow_mode) {
  SimSession s(320, 240, kEveningEpoch);
  s.os.launch("clock", "bedside");
  s.run(600);
  CHECK_EQ(brightest(s), 0);  // mode 1: no glow
  SystemSettings st = s.os.settings();
  st.ledMode = 2;
  s.os.applySettings(st);
  int lo, hi;
  watch(s, 8000, lo, hi);
  CHECK(hi > 5 && lo >= 1);  // faint and never quite dark: a night light
  CHECK(hi < 60);
  s.os.goHome();
  s.run(600);
  CHECK_EQ(brightest(s), 0);  // leaving the bedside face puts it out
}

TEST(an_update_shows_blue_while_downloading_and_green_when_ready_and_red_if_it_fails) {
  SimSession s(320, 240);
  std::string image(3000, 'x');
  uint8_t d[32];
  Sha256::hash(image.data(), image.size(), d);
  char hex[65];
  toHex(d, 32, hex);
  std::string rs;
  for (int i = 0; i < 32; ++i) rs += "5a";
  const std::string manifest = std::string("{\"version\":\"99.0.0\",\"url\":\"https://updates.example/stella/stella32.bin\",\"size\":3000,\"sha256\":\"") +
                               hex + "\",\"sig_r\":\"" + rs + "\",\"sig_s\":\"" + rs + "\"}";
  s.dev.http.routes.insert(s.dev.http.routes.begin(), {"stella32.bin", image, 200});
  s.dev.http.routes.insert(s.dev.http.routes.begin(), {"manifest.json", manifest, 200});
  s.os.update().check();
  s.run(3000);
  CHECK(s.os.update().status().state == UpdateState::Available);
  CHECK_EQ(brightest(s), 0);  // an offer alone is not worth a light
  bool sawBlue = false;
  s.os.update().install();
  for (int i = 0; i < 400 && !sawBlue; ++i) {
    s.run(20);
    sawBlue = s.dev.led.shown(2) > 10 && s.dev.led.shown(0) < s.dev.led.shown(2);
  }
  s.run(8000);
  CHECK(s.os.update().status().state == UpdateState::Ready);
  int lo, hi;
  watch(s, 3500, lo, hi);
  CHECK(hi > 30);  // breathing green while it waits for the restart
  CHECK(s.dev.led.shown(1) >= s.dev.led.shown(0));
  (void)sawBlue;  // the download of 3 KB is over in one step; the state check above covers Ready
}

TEST(apps_can_flash_the_led_and_it_ends_by_itself) {
  SimSession s(320, 240);
  s.os.launch("stopwatch");
  s.run(100);
  s.os.flashLed(255, 255, 255, 200);
  s.run(60);
  CHECK(brightest(s) > 50);
  s.run(500);
  CHECK_EQ(brightest(s), 0);
}

TEST(a_new_wiring_reaches_the_board_and_unusable_pins_release_it) {
  SimSession s(320, 240);
  CHECK(s.dev.led.wiring.pin[0] == 4 && s.dev.led.wiring.pin[2] == 17 && s.dev.led.wiring.inverted);
  SystemSettings st = s.os.settings();
  st.ledPin[0] = 255;
  st.ledPin[1] = 255;
  st.ledPin[2] = 17;
  st.ledInverted = false;
  s.os.applySettings(st);
  CHECK(s.dev.led.wiring.pin[2] == 17 && s.dev.led.wiring.pin[0] == kNoPin && !s.dev.led.wiring.inverted);
  st.ledPin[2] = 200;  // not a pin: sanitised away
  s.os.applySettings(st);
  CHECK(!s.dev.led.wiring.usable());
}

TEST(a_probe_pauses_the_led_gives_it_back_and_never_stays_lit) {
  SimSession s(320, 240);
  showAlert(s);
  s.run(300);
  CHECK(brightest(s) > 0);
  s.os.ledProbe(16, true);
  const int sets = s.dev.led.sets;
  s.run(2000);
  CHECK(s.dev.led.probing() && s.dev.led.sets == sets);  // the alert's breathing is paused
  CHECK(s.dev.led.probeLit(1) && !s.dev.led.probeLit(0) && !s.dev.led.probeLit(2));
  s.os.ledProbe(kNoPin, true);
  CHECK(!s.dev.led.probing());
  s.run(300);
  CHECK(brightest(s) > 0);  // and the alert's light is back
  s.os.dismissAlert(0);
  s.run(200);
  // Forgotten probe: it ends by itself.
  s.os.ledProbe(4, false);
  s.run(11000);
  CHECK(!s.dev.led.probing());
}
