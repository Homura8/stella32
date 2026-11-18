#pragma once
// Deck: the touchscreen as a macro pad for a computer, over Bluetooth Low Energy
// (the radio is the board's `HidDevice`; builds without one do not install this
// app). Six big pads send media keys (previous, play/pause, next, mute, volume
// down and up), which behave the same on Windows, macOS, Linux, Android and iOS,
// so no per-computer setup is needed.
//
// The radio costs memory, so it runs only while this screen is showing: it starts
// when the Deck opens and stops when it closes (the runtime also stops it for the
// length of an over-the-air update). Pairing is the computer's job: it lists
// "StellaOS Deck" among its Bluetooth devices; the pairing is remembered, and
// "Forget" removes it.
#include "../app/app.h"

namespace stella {
namespace apps {

class DeckApp : public App {
 public:
  DeckApp() : screen_(*this) {}
  const AppInfo &info() const override;
  Screen &root() override { return screen_; }
  // For tests and screenshots.
  static constexpr int kPads = 6;
  Rect pad(int i) const { return screen_.padBounds(i); }
  Rect forgetButton() const { return screen_.forgetBounds(); }
  static const char *deviceName() { return "StellaOS Deck"; }

 private:
  class DeckScreen : public Screen {
   public:
    explicit DeckScreen(DeckApp &app) : app_(app) {}
    const char *title() const override { return "Deck"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    void onHide(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;
    Rect padBounds(int i) const { return pads_[i]; }
    Rect forgetBounds() const { return forget_.bounds; }

   private:
    void refresh(AppContext &ctx);
    DeckApp &app_;
    Rect status_, pads_[kPads];
    Button forget_;
    bool pressed_[kPads] = {};
    HidState state_ = HidState::Off;
    int bonds_ = 0;
    bool present_ = false;
    char statusText_[96] = {0};
  };

  DeckScreen screen_;
};

}  // namespace apps
}  // namespace stella
