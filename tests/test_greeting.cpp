// The Greeting app and the gift build that uses it: a message that opens first
// thing until it has been read to the end, and comes back if it was left open.
#include <string.h>

#include "gift.example.h"
#include "sim_session.h"
#include "stella/storage/record.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {

void tapRect(SimSession &s, const Rect &r) { s.tap(r.centerX(), r.centerY()); }
const char *foregroundId(SimSession &s) { return s.os.foreground() ? s.os.foreground()->info().id : ""; }

SimOptions gift() {
  SimOptions o;
  o.gift = &giftContent();
  return o;
}

// Reads the whole message: Continue, then Next on every page, then Done.
void readToTheEnd(SimSession &s) {
  tapRect(s, s.greeting.continueRect());
  s.run(100);
  for (int i = 1; i < s.greeting.words().pageCount; ++i) {
    tapRect(s, s.greeting.nextRect());
    s.run(100);
  }
  tapRect(s, s.greeting.nextRect());  // Done
  s.run(300);
}

}  // namespace

TEST(a_gift_device_opens_on_the_message_and_an_ordinary_one_never_does) {
  SimSession ordinary(320, 240);
  CHECK(!ordinary.greeting.hasWords());  // installed, but dormant: no words, not on Home, never opens
  CHECK(strcmp(foregroundId(ordinary), "home") == 0);

  SimSession s(320, 240, gift());
  CHECK(strcmp(foregroundId(s), "greeting") == 0);
  CHECK(s.greeting.pending());
  CHECK_EQ(s.greeting.stage(), 0);
}

TEST(the_message_is_read_a_page_at_a_time_and_back_goes_back) {
  SimSession s(320, 240, gift());
  tapRect(s, s.greeting.continueRect());
  s.run(100);
  CHECK_EQ(s.greeting.stage(), 1);
  tapRect(s, s.greeting.nextRect());
  s.run(100);
  CHECK_EQ(s.greeting.stage(), 2);
  tapRect(s, s.greeting.backRect());
  s.run(100);
  CHECK_EQ(s.greeting.stage(), 1);
  tapRect(s, s.greeting.backRect());
  s.run(100);
  CHECK_EQ(s.greeting.stage(), 0);  // the first page again
  CHECK(strcmp(foregroundId(s), "greeting") == 0);  // none of that closed it
}

TEST(finishing_the_message_goes_home_and_it_stays_away_after_a_power_cycle) {
  SimSession s(320, 240, gift());
  readToTheEnd(s);
  CHECK(strcmp(foregroundId(s), "home") == 0);
  CHECK(!s.greeting.pending());
  // Unplugged and plugged in again: Home, not the message.
  SimOptions again = gift();
  again.savedStorage = &s.dev.storage;
  SimSession next(320, 240, again);
  CHECK(strcmp(foregroundId(next), "home") == 0);
  CHECK(!next.greeting.pending());
}

TEST(unplugging_before_finishing_brings_the_message_back) {
  SimSession s(320, 240, gift());
  tapRect(s, s.greeting.continueRect());  // halfway through
  s.run(100);
  SimOptions again = gift();
  again.savedStorage = &s.dev.storage;
  SimSession next(320, 240, again);
  CHECK(strcmp(foregroundId(next), "greeting") == 0);
  CHECK_EQ(next.greeting.stage(), 0);  // from the beginning
}

TEST(opening_the_app_again_makes_it_come_back_until_it_is_finished) {
  SimSession s(320, 240, gift());
  readToTheEnd(s);
  SimOptions off = gift();
  off.savedStorage = &s.dev.storage;
  SimSession home(320, 240, off);
  CHECK(strcmp(foregroundId(home), "home") == 0);
  // Open it by hand (as on Home) and unplug: it is shown again next time.
  home.os.launch("greeting");
  home.run(100);
  CHECK(home.greeting.pending());
  SimOptions after = gift();
  after.savedStorage = &home.dev.storage;
  SimSession third(320, 240, after);
  CHECK(strcmp(foregroundId(third), "greeting") == 0);
  // Finish it there and it stays away again.
  readToTheEnd(third);
  SimOptions last = gift();
  last.savedStorage = &third.dev.storage;
  SimSession fourth(320, 240, last);
  CHECK(strcmp(foregroundId(fourth), "home") == 0);
}

TEST(the_message_is_on_home_so_it_can_be_opened_again) {
  SimSession s(320, 240, gift());
  readToTheEnd(s);
  bool found = false;
  for (int slot = 0; slot < 12; ++slot) {
    const char *name = s.launcher.tileName(slot);
    if (name && strcmp(name, "Birthday") == 0) found = true;
  }
  CHECK(found);
}

TEST(a_reset_makes_the_message_come_first_again) {
  SimSession s(320, 240, gift());
  readToTheEnd(s);
  s.os.launch("settings", "reset");
  s.run(200);
  tapRect(s, s.settings.resetEraseRect());
  s.run(1500);
  CHECK_EQ((int)s.dev.storage.count(), 0);
  SimOptions again = gift();
  again.savedStorage = &s.dev.storage;
  SimSession fresh(320, 240, again);
  CHECK(strcmp(foregroundId(fresh), "greeting") == 0);  // a new install shows it
}

TEST(every_page_fits_its_screen_and_no_word_is_cut_at_the_sizes_that_matter) {
  const int sizes[][2] = {{320, 240}, {480, 320}, {800, 480}, {240, 320}};
  for (const auto &sz : sizes) {
    SimSession s(sz[0], sz[1], gift());
    const Theme &t = s.os.theme();
    CHECK(s.greeting.fits());
    const int width = sz[0] - 2 * t.m.margin;
    const apps::GreetingWords &c = s.greeting.words();
    // Every word of every page fits a line whole (a long web address would otherwise be shortened) on all but the
    // narrow portrait panel, which is not a CYD.
    if (sz[0] >= 320) {
      auto wordsFit = [&](const char *text, const Font &font) {
        char word[96];
        size_t n = 0;
        for (const char *p = text;; ++p) {
          if (*p == ' ' || *p == '\0') {
            word[n] = '\0';
            if (Canvas::textWidth(font, word) > width) return false;
            n = 0;
            if (*p == '\0') break;
          } else if (n < sizeof(word) - 1) {
            word[n++] = *p;
          }
        }
        return true;
      };
      CHECK(wordsFit(c.headline, *t.font.headline));
      CHECK(wordsFit(c.wish, *t.font.title));
      for (int i = 0; i < c.pageCount; ++i) CHECK(wordsFit(c.pages[i], *t.font.body));
    }
  }
}

TEST(the_message_twinkles_without_repainting_the_whole_screen) {
  SimSession s(320, 240, gift());
  s.run(500);
  s.dev.display.resetCounters();
  s.run(9000);  // two or three twinkles
  CHECK(s.dev.display.pixelsWritten() > 0);
  CHECK(s.dev.display.pixelsWritten() < (uint64_t)320 * 240 / 3);  // a few stars and a sparkle, never everything
}

TEST(the_example_words_are_saved_whole_not_cut_to_fit) {
  SimSession s(320, 240, gift());
  const apps::GreetingContent &c = giftContent();
  const apps::GreetingWords &w = s.greeting.words();
  CHECK(strcmp(w.name, c.name) == 0);
  CHECK(strcmp(w.headline, c.headline) == 0);
  CHECK(strcmp(w.wish, c.wish) == 0);
  CHECK_EQ((int)w.pageCount, c.pageCount);
  for (int i = 0; i < c.pageCount; ++i) CHECK(strcmp(w.pages[i], c.pages[i]) == 0);
}

TEST(the_message_survives_an_update_to_an_ordinary_build_that_has_no_words_of_its_own) {
  SimSession gifted(320, 240, gift());
  tapRect(gifted, gifted.greeting.continueRect());  // not finished
  gifted.run(100);
  // The ordinary firmware: the same app, but nothing offered. The device's own record is enough.
  SimOptions updated;
  updated.savedStorage = &gifted.dev.storage;
  SimSession next(320, 240, updated);
  CHECK(next.greeting.hasWords());
  CHECK(strcmp(next.greeting.words().headline, giftContent().headline) == 0);
  CHECK(strcmp(foregroundId(next), "greeting") == 0);  // still unread, so first
  readToTheEnd(next);
  CHECK(strcmp(foregroundId(next), "home") == 0);
  // And from then on it is the Birthday app on Home, still there after another power cycle.
  SimOptions again;
  again.savedStorage = &next.dev.storage;
  SimSession later(320, 240, again);
  CHECK(strcmp(foregroundId(later), "home") == 0);
  bool found = false;
  for (int slot = 0; slot < 12; ++slot) {
    const char *name = later.launcher.tileName(slot);
    if (name && strcmp(name, "Birthday") == 0) found = true;
  }
  CHECK(found);
}

TEST(new_words_replace_the_saved_ones_and_are_unread_again) {
  SimSession first(320, 240, gift());
  readToTheEnd(first);
  apps::GreetingContent other = giftContent();
  other.headline = "Happy Birthday, Sam!";
  SimOptions o;
  o.gift = &other;
  o.savedStorage = &first.dev.storage;
  SimSession second(320, 240, o);
  CHECK(strcmp(second.greeting.words().headline, "Happy Birthday, Sam!") == 0);
  CHECK(strcmp(foregroundId(second), "greeting") == 0);  // different words: worth reading
  // The same words again, once read, stay quiet.
  readToTheEnd(second);
  SimOptions same;
  same.gift = &other;
  same.savedStorage = &second.dev.storage;
  SimSession third(320, 240, same);
  CHECK(strcmp(foregroundId(third), "home") == 0);
}

TEST(a_device_without_words_writes_nothing_and_a_hand_opened_dormant_app_is_harmless) {
  SimSession s(320, 240);
  const size_t before = s.dev.storage.count();
  s.os.launch("greeting");  // only reachable like this (a test, or a script); there is nothing to read
  s.run(200);
  tapRect(s, s.greeting.continueRect());
  s.run(200);
  CHECK(strcmp(foregroundId(s), "home") == 0);
  CHECK_EQ((int)s.dev.storage.count(), (int)before);
  CHECK(!s.greeting.hasWords());
}

TEST(a_torn_set_of_saved_words_is_ignored_and_a_gift_build_puts_it_right) {
  SimSession gifted(320, 240, gift());
  std::vector<uint8_t> *page = gifted.dev.storage.raw("greeting", "p1");
  CHECK(page != nullptr);
  if (!page) return;
  (*page)[page->size() / 2] ^= 0x5A;  // damaged, as if the power went mid-write (the record's own CRC aside)
  // Rewrite it with a valid record around different text: the head's checksum no longer matches.
  char other[apps::GreetingWords::kPageLen] = "Not the words that were saved.";
  CHECK(saveRecord(&gifted.dev.storage, "greeting", "p1", 1, other, sizeof(other)));
  SimOptions ordinary;
  ordinary.savedStorage = &gifted.dev.storage;
  SimSession plain(320, 240, ordinary);
  CHECK(!plain.greeting.hasWords());  // nothing half-read; the app stays out of sight
  CHECK(strcmp(foregroundId(plain), "home") == 0);
  SimOptions again = gift();
  again.savedStorage = &gifted.dev.storage;
  SimSession fixed(320, 240, again);
  CHECK(fixed.greeting.hasWords());
  CHECK(strcmp(fixed.greeting.words().pages[1], giftContent().pages[1]) == 0);
}

namespace {
SystemSettings brightWithIdleDim(SimSession &s, uint16_t dimAfterSec) {
  SystemSettings st = s.os.settings();
  st.brightness = 200;
  st.nightDim = false;        // the sky's own cap is a separate setting, off here so the levels compare cleanly
  st.autoBrightness = false;  // and so is the light sensor
  st.dimAfterSec = dimAfterSec;
  st.sleepAfterSec = 0;
  return st;
}
}  // namespace

TEST(the_message_is_as_bright_as_any_screen_not_cut_to_a_third_like_an_idle_face) {
  SimSession s(320, 240, gift());
  s.os.applySettings(brightWithIdleDim(s, 0));
  s.run(200);
  const uint8_t greeting = s.dev.display.backlight();
  readToTheEnd(s);
  s.run(200);
  CHECK(strcmp(foregroundId(s), "home") == 0);
  const uint8_t home = s.dev.display.backlight();
  CHECK_EQ((int)greeting, (int)home);  // the same level as an ordinary screen
  s.os.launch("clock", "bedside");     // the idle face really is dimmer
  s.run(200);
  CHECK(s.dev.display.backlight() < home);
}

TEST(the_message_does_not_dim_while_it_is_being_read_but_home_still_does) {
  SimSession s(320, 240, gift());
  s.os.applySettings(brightWithIdleDim(s, 30));
  s.run(200);
  const uint8_t full = s.dev.display.backlight();
  s.run(120000);  // two minutes with nobody touching: far longer than the idle dim
  CHECK_EQ((int)s.dev.display.backlight(), (int)full);
  // Reading goes on: a tap on Continue and the next page are just as bright.
  tapRect(s, s.greeting.continueRect());
  s.run(200);
  CHECK_EQ((int)s.dev.display.backlight(), (int)full);
  // Elsewhere the idle dim is unchanged.
  readToTheEnd(s);
  s.os.applySettings(brightWithIdleDim(s, 30));
  s.run(60000);
  CHECK(s.dev.display.backlight() < full);
}

TEST(the_screen_off_setting_still_applies_to_the_message) {
  SimSession s(320, 240, gift());
  SystemSettings st = brightWithIdleDim(s, 30);
  st.sleepAfterSec = 300;
  s.os.applySettings(st);
  s.run(400000);
  CHECK_EQ((int)s.dev.display.backlight(), 0);
}
