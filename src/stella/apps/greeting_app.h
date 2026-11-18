#pragma once
// A bespoke greeting, as an app: a starry message and a few short pages of
// explanation. It is not part of an ordinary StellaOS: the app is installed in
// every build but stays out of sight (not on Home, never opens by itself) until the
// device has words for it. A build made for one person offers those words
// (examples/CYD/gift.example.h); the app saves them on the device the first time,
// so they, and the app, survive an over-the-air update to an ordinary build.
//
// While it is "pending" the device shows it first thing when it starts. It stays
// pending until the person finishes the last page (Done); opening the app again
// makes it pending again, so unplugging while it is open brings it back.
#include "../app/app.h"

namespace stella {
namespace apps {

// The words, as a build author writes them. Every string must outlive the call
// (string literals do).
struct GreetingContent {
  static constexpr int kMaxPages = 6;
  const char *name = "Hello";   // its name on Home (15 characters at most)
  const char *headline = "";    // the first page: the big line (47 characters) ...
  const char *wish = "";        // ... and a softer line under it (79)
  const char *pages[kMaxPages] = {};  // then short pages, each a few lines (183 characters), read one at a time
  int pageCount = 0;
};

// The same words as the device keeps them (a versioned record in the app's own
// storage): fixed buffers, no heap. Longer text is cut to fit, so keep to the limits above.
struct GreetingWords {
  static constexpr int kMaxPages = GreetingContent::kMaxPages;
  static constexpr int kNameLen = 16, kHeadlineLen = 48, kWishLen = 80, kPageLen = 184;  // kept small: this sits in RAM in every build
  char name[kNameLen];
  char headline[kHeadlineLen];
  char wish[kWishLen];
  char pages[kMaxPages][kPageLen];
  uint8_t pageCount;

  static GreetingWords from(const GreetingContent &content);
  bool same(const GreetingWords &other) const;  // byte for byte
  uint32_t checksum() const;                    // over everything, to tell a complete saved set from a torn one
};

class GreetingApp : public App {
 public:
  GreetingApp();
  // For a build made for one person: these words are saved on the device (replacing any other) the
  // first time it starts with them. Call before the app is installed.
  void offer(const GreetingContent &content) { offered_ = &content; }
  const AppInfo &info() const override { return hasWords_ ? shown_ : hidden_; }
  Screen &root() override { return screen_; }
  void onInstall(ServiceHost &host) override;
  uint32_t onWake(AppContext &ctx) override;
  bool launchAtBoot() const override { return hasWords_ && pending_; }
  bool hasWords() const { return hasWords_; }  // else it is dormant: not on Home, never shown by itself
  bool pending() const { return pending_; }
  const GreetingWords &words() const { return words_; }

  // Where things are (tests and screenshots).
  Rect continueRect() const { return screen_.continueRect(); }
  Rect backRect() const { return screen_.backRect(); }
  Rect nextRect() const { return screen_.nextRect(); }
  int stage() const { return screen_.stage(); }
  // How many lines `text` takes in `font` within `width` pixels.
  static int linesFor(const Font &font, const char *text, int width);
  // Whether the headline, the wish and every page fit the screen as laid out.
  bool fits() const { return screen_.fits(); }

 private:
  static constexpr uint16_t kStateVersion = 1, kWordsVersion = 1;
  // The words are saved as a head record plus one record per page (each under the 1 KB record limit),
  // pages first and the head last, with a checksum, so a power cut midway leaves nothing half-read.
  bool loadWords(ServiceHost &host);
  void saveWords(AppContext &ctx);
  void setPending(bool pending, AppContext &ctx);

  class GreetingScreen : public Screen {
   public:
    explicit GreetingScreen(GreetingApp &app) : app_(app) {}
    bool fullscreen() const override { return true; }
    void layout(const Rect &area, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;

    Rect continueRect() const { return continue_.bounds; }
    Rect backRect() const { return back_.bounds; }
    Rect nextRect() const { return next_.bounds; }
    int stage() const { return stage_; }
    bool fits() const { return fits_; }

   private:
    static constexpr int kStars = 16;
    static int starLevel(int star, int frame);
    Rect starBox(int star) const;
    void go(int stage, AppContext &ctx);
    void animate(AppContext &ctx, int frame);
    void drawPill(Canvas &c, const Theme &t, const Button &b, bool primary) const;

    GreetingApp &app_;
    Rect area_, art_, headline_, wish_, text_[GreetingContent::kMaxPages], dots_, twinkle_[3];
    Point mid_, left_, right_, farLeft_, farRight_;  // the sparkles over the words
    Button continue_, back_, next_;
    Point stars_[kStars];
    int starSize_ = 1, sparkle_ = 0, stage_ = 0, frame_ = 0;
    bool fits_ = true;
  };

  GreetingWords words_;
  const GreetingContent *offered_ = nullptr;
  AppInfo shown_, hidden_;
  GreetingScreen screen_;
  bool hasWords_ = false;
  bool pending_ = true;  // a device that has never shown it shows it
  bool saveWords_ = false, savePending_ = false;  // to write at the first wake (needs the app context)
};

}  // namespace apps
}  // namespace stella
