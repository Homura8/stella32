// Stability: a "monkey" taps, drags, long-presses, launches, jumps the clock and changes
// settings at random through every app at every screen size (with the optional ones on:
// Stats with values, a calendar, the Deck), and after each burst the OS must still be
// consistent. The seeds are fixed so a failure is reproducible; the CI runs this suite under
// AddressSanitizer and UBSan, which catch what the checks here cannot see.
#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include "sim_session.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {

uint32_t gSeed = 0;  // the monkey being run, for the failure message

struct Rng {
  uint32_t x;
  uint32_t next() {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
  }
  int upto(int n) { return (int)(next() % (uint32_t)n); }
};

// One request to the device's web page, as a script on the network would send it.
void push(SimSession &s, const char *path, const std::string &body) {
  if (!s.dev.remote.handler) return;
  char pin[8];
  s.os.remote().pin(pin);
  RemoteRequest req;
  req.post = true;
  req.path = path;
  req.pin = pin;
  req.body = body.c_str();
  req.bodyLen = body.size();
  char reply[256];
  RemoteResponse res;
  res.body = reply;
  res.capacity = sizeof(reply);
  s.dev.remote.handler->handleRemote(req, res);
}

const char *const kApps[] = {"home", "clock", "weather", "alarms", "timer", "stopwatch", "settings", "values", "calendar", "deck", "touch"};
const char *const kRoutes[] = {nullptr, "customise", "wifi", "remote", "units", "idle", "calendar", "whatsnew", "places", "bedside", "dashboard"};

void checkConsistent(SimSession &s, int width, int height, const char *what) {
  CHECK(s.os.foreground() != nullptr);
  CHECK(s.os.topScreen() != nullptr);
  const SystemSettings &st = s.os.settings();
  CHECK(st.brightness >= 16 && st.volume <= 100 && st.dateOrder <= 1 && st.idleFace <= 1 && st.bedside <= 2);
  CHECK(strlen(st.timezone) > 0 && strlen(st.timezone) < sizeof(st.timezone));
  CHECK(s.dev.display.framebuffer().size() == (size_t)width * height);
  s.run(400);
  CHECK(!s.os.renderPending());
  // What is on screen is what a clean redraw gives, except on a fullscreen idle face (it animates on
  // its own clock) or while an alert's veil is fading.
  if (!s.os.topScreen()->fullscreen()) {
    const std::vector<Color> incremental = s.dev.display.framebuffer();
    s.os.invalidateAll();
    // Redraw without letting time pass, so a clock or a countdown cannot tick in between.
    for (int i = 0; i < 200 && s.os.renderPending(); ++i) s.os.step();
    if (incremental != s.dev.display.framebuffer()) {
      int x0 = width, y0 = height, x1 = -1, y1 = -1, n = 0;
      const std::vector<Color> &after = s.dev.display.framebuffer();
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
          if (incremental[(size_t)y * width + x] != after[(size_t)y * width + x]) {
            ++n;
            x0 = x < x0 ? x : x0;
            y0 = y < y0 ? y : y0;
            x1 = x > x1 ? x : x1;
            y1 = y > y1 ? y : y1;
          }
      fprintf(stderr, "  [seed 0x%x] frame differs from a full redraw after: %s (app %s, screen \"%s\"): %d pixels in (%d,%d)-(%d,%d)\n", gSeed, what,
              s.os.foreground()->info().id, s.os.topScreen()->title() ? s.os.topScreen()->title() : "-", n, x0, y0, x1, y1);
      CHECK(false);
    }
  }
}

// One monkey: `steps` random actions with checks every 25, at one screen size.
void runMonkey(int width, int height, uint32_t seed, int steps) {
  std::unique_ptr<SimSession> s(new SimSession(width, height, kMorningEpoch, Appearance::Auto, false, true, /*withDeck=*/true));
  gSeed = seed;
  Rng rng{seed ? seed : 1u};
  std::vector<std::string> visited;
  // Everything optional on: the web page (so values can be pushed), a calendar, some values.
  s->os.remote().setEnabled(true);
  s->run(1500);
  push(*s, "/api/push", "CPU=37%&Temp=61C&Fan=1180 rpm");
  s->os.calendar().setAddress("https://calendar.example/feeds/home/basic.ics");
  s->run(3000);
  char what[96];
  for (int step = 0; step < steps; ++step) {
    const int kind = rng.upto(100);
    if (kind < 46) {
      snprintf(what, sizeof(what), "tap step %d", step);
      s->tap(rng.upto(width), rng.upto(height));
    } else if (kind < 60) {
      snprintf(what, sizeof(what), "drag step %d", step);
      s->drag(rng.upto(width), rng.upto(height), rng.upto(width), rng.upto(height));
    } else if (kind < 65) {
      snprintf(what, sizeof(what), "long press step %d", step);
      s->longPress(rng.upto(width), rng.upto(height));
    } else if (kind < 76) {
      const char *app = kApps[rng.upto((int)(sizeof(kApps) / sizeof(kApps[0])))];
      const char *route = kRoutes[rng.upto((int)(sizeof(kRoutes) / sizeof(kRoutes[0])))];
      snprintf(what, sizeof(what), "launch %s %s step %d", app, route ? route : "-", step);
      s->os.launch(app, route);
    } else if (kind < 80) {
      snprintf(what, sizeof(what), "home step %d", step);
      s->os.goHome();
    } else if (kind < 90) {
      snprintf(what, sizeof(what), "time passes step %d", step);
      s->run(1 + (uint32_t)rng.upto(4000));
    } else if (kind < 92) {
      snprintf(what, sizeof(what), "the clock jumps step %d", step);
      s->dev.platform.advance(1 + (uint32_t)rng.upto(8 * 3600) * 1000u);
    } else if (kind < 96) {
      snprintf(what, sizeof(what), "settings change step %d", step);
      SystemSettings st = s->os.settings();
      st.clock24h = rng.upto(2) != 0;
      st.fahrenheit = rng.upto(2) != 0;
      st.weekStartsSunday = rng.upto(2) != 0;
      st.dateOrder = (uint8_t)rng.upto(2);
      st.idleFace = (uint8_t)rng.upto(2);
      st.bedside = (uint8_t)rng.upto(3);
      st.dimAfterSec = rng.upto(2) ? 30 : 60;
      st.appearance = (Appearance)rng.upto(3);
      s->os.applySettings(st);
    } else if (kind < 98) {
      snprintf(what, sizeof(what), "a script pushes values step %d", step);
      push(*s, "/api/push", "k" + std::to_string(rng.upto(12)) + "=" + std::to_string(rng.upto(1000)));
    } else {
      snprintf(what, sizeof(what), "wifi goes away step %d", step);
      s->dev.wifi.dropLink();  // the service reconnects with backoff
    }
    s->run(80);
    const std::string id = s->os.foreground()->info().id;
    bool seen = false;
    for (const std::string &v : visited) seen |= v == id;
    if (!seen) visited.push_back(id);
    if (step % 25 == 24) checkConsistent(*s, width, height, what);
  }
  checkConsistent(*s, width, height, "the end");
  if (steps >= 200) CHECK(visited.size() >= 7);  // the monkey really did wander through the apps
}

}  // namespace

TEST(a_monkey_cannot_break_the_os_at_any_size) {
  // The CYD's own size with two seeds, the other sizes (portrait, 480x320, 800x480) with one.
  runMonkey(320, 240, 0x1234567u, 260);
  runMonkey(320, 240, 0x9e3779b9u, 260);
  runMonkey(240, 320, 0x1234567u, 260);
  runMonkey(480, 320, 0x1234567u, 260);
  runMonkey(800, 480, 0x1234567u, 260);
}

// A soak: STELLA_MONKEY_SEEDS=40 ./build/stella_tests runs that many more seeds at the CYD's
// size (nothing happens without the variable).
TEST(a_monkey_soak_when_asked_for) {
  const char *env = getenv("STELLA_MONKEY_SEEDS");
  const int n = env ? atoi(env) : 0;
  for (int i = 0; i < n; ++i) runMonkey(320, 240, 0x85ebca6bu * (uint32_t)(i + 3), 400);
}
