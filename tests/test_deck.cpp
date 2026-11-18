// The Deck app: the touchscreen as a Bluetooth media-key pad. The radio is a
// simulated HidDevice; a test plays the computer that pairs with it.
#include <memory>
#include <string.h>

#include "sim_session.h"
#include "test.h"

using namespace stella;
using namespace stella::host;
using namespace stella::apps;

namespace {

SimSession *make(int w = 320, int h = 240) {
  return new SimSession(w, h, kEveningEpoch, Appearance::Auto, false, true, /*withDeck=*/true);
}

void tapRect(SimSession &s, const Rect &r) { s.tap(r.centerX(), r.centerY()); }

}  // namespace

TEST(the_radio_runs_only_while_the_deck_is_showing) {
  std::unique_ptr<SimSession> s(make());
  CHECK_EQ(s->dev.hid.starts, 0);  // nothing until it is wanted
  CHECK(s->dev.hid.state() == HidState::Off);
  s->os.launch("deck");
  s->run(300);
  CHECK_EQ(s->dev.hid.starts, 1);
  CHECK(s->dev.hid.state() == HidState::Advertising);
  CHECK(s->dev.hid.name == "StellaOS Deck");
  s->os.goHome();
  s->run(300);
  CHECK(s->dev.hid.state() == HidState::Off && s->dev.hid.stops >= 1);
  // And again.
  s->os.launch("deck");
  s->run(300);
  CHECK_EQ(s->dev.hid.starts, 2);
}

TEST(pads_send_the_media_keys_once_the_computer_has_connected) {
  std::unique_ptr<SimSession> s(make());
  s->os.launch("deck");
  s->run(300);
  // Not connected: a tap sends nothing.
  tapRect(*s, s->deck.pad(1));
  CHECK(s->dev.hid.sent.empty());
  s->dev.hid.connect();
  s->run(700);  // the screen notices
  const uint16_t expected[6] = {hid::kPrevious, hid::kPlayPause, hid::kNext, hid::kMute, hid::kVolumeDown, hid::kVolumeUp};
  for (int i = 0; i < DeckApp::kPads; ++i) tapRect(*s, s->deck.pad(i));
  CHECK_EQ(s->dev.hid.sent.size(), 6u);
  for (size_t i = 0; i < s->dev.hid.sent.size() && i < 6; ++i)
    CHECK(s->dev.hid.sent[i].consumer && s->dev.hid.sent[i].code == expected[i]);
  // The computer goes away: nothing more is sent, and the screen says it is waiting.
  s->dev.hid.disconnect();
  s->run(700);
  tapRect(*s, s->deck.pad(2));
  CHECK_EQ(s->dev.hid.sent.size(), 6u);
}

TEST(forget_removes_the_pairing_and_starts_again) {
  std::unique_ptr<SimSession> s(make());
  s->os.launch("deck");
  s->run(300);
  CHECK(s->deck.forgetButton().w > 0);
  s->dev.hid.connect();  // pairs: one bond
  s->run(700);
  CHECK_EQ(s->dev.hid.bondCount(), 1);
  const int starts = s->dev.hid.starts;
  tapRect(*s, s->deck.forgetButton());
  s->run(700);
  CHECK_EQ(s->dev.hid.bondCount(), 0);
  CHECK(s->dev.hid.starts == starts + 1 && s->dev.hid.state() == HidState::Advertising);
}

TEST(a_radio_that_cannot_start_says_so_and_does_not_crash) {
  std::unique_ptr<SimSession> s(make());
  s->dev.hid.failStart = true;
  s->os.launch("deck");
  s->run(1200);
  CHECK(s->dev.hid.state() == HidState::Failed);
  tapRect(*s, s->deck.pad(0));
  CHECK(s->dev.hid.sent.empty());
  s->os.goHome();
  s->run(300);
  CHECK(s->dev.hid.state() == HidState::Off);
}

TEST(the_deck_renders_at_every_size_and_matches_a_full_redraw) {
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    for (int phase = 0; phase < 3; ++phase) {  // waiting, connected, paired and waiting again
      std::unique_ptr<SimSession> s(make(sz[0], sz[1]));
      s->os.launch("deck");
      s->run(300);
      if (phase >= 1) {
        s->dev.hid.connect();
        s->run(700);
      }
      if (phase == 2) {
        s->dev.hid.disconnect();
        s->run(700);
      }
      const std::vector<Color> incremental = s->dev.display.framebuffer();
      s->os.invalidateAll();
      s->run(20);
      CHECK(incremental == s->dev.display.framebuffer());
      // The pads fit on the screen and are big enough to hit.
      for (int i = 0; i < DeckApp::kPads; ++i) {
        const Rect p = s->deck.pad(i);
        CHECK(p.x >= 0 && p.right() <= sz[0] && p.bottom() <= sz[1] && p.h >= s->os.theme().dp(32));
      }
      // Idle: the radio state is polled but nothing repaints.
      s->dev.display.resetCounters();
      s->run(3000);
      CHECK_EQ(s->dev.display.pixelsWritten(), 0u);
    }
  }
}

TEST(a_board_without_bluetooth_has_no_deck_to_open) {
  SimSession s(320, 240);  // the default build: the app is not installed
  CHECK(s.dev.hid.state() == HidState::Off);
  s.os.launch("deck");
  s.run(300);
  CHECK(strcmp(s.os.foreground()->info().id, "home") == 0);
  CHECK_EQ(s.dev.hid.starts, 0);
}
