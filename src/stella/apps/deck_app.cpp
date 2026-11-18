#include "deck_app.h"

#include <stdio.h>
#include <string.h>

#include "../core/text.h"
#include "../ui/layout.h"
#include "../ui/materials.h"

namespace stella {
namespace apps {

using namespace layout;

namespace {

struct Pad {
  const char *label;
  IconFn icon;
  uint16_t usage;  // a consumer-control usage
};

const Pad kButtons[DeckApp::kPads] = {
    {"Previous", icons::skipPrevious, hid::kPrevious}, {"Play / Pause", icons::playPause, hid::kPlayPause},
    {"Next", icons::skipNext, hid::kNext},             {"Mute", icons::speaker, hid::kMute},
    {"Volume -", icons::speaker, hid::kVolumeDown},    {"Volume +", icons::speaker, hid::kVolumeUp},
};

}  // namespace

const AppInfo &DeckApp::info() const {
  static const AppInfo kInfo{"deck", "Deck", icons::bluetooth, 1, 0};
  return kInfo;
}

void DeckApp::DeckScreen::layout(const Rect &content, const Theme &t) {
  const Rect area = content.inset(t.m.margin, t.m.gapS);
  Rect r[2];
  column(area, {px(3 * t.font.caption->lineHeight + t.dp(4)), flex()}, t.m.gapM, r);
  Rect top[2];
  row(r[0], {flex(), px(t.dp(84))}, t.m.gapM, top);
  status_ = top[0];
  forget_.bounds = center(top[1], t.dp(84), t.m.control - t.dp(4));
  forget_.label = "Forget";
  forget_.style = ButtonStyle::Quiet;
  for (int i = 0; i < kPads; ++i) pads_[i] = grid(r[1], 3, 2, i, t.m.gapM);
}

void DeckApp::DeckScreen::refresh(AppContext &ctx) {
  HidDevice *h = ctx.hid();
  const bool present = h && h->available();
  const HidState state = present ? h->state() : HidState::Off;
  const int bonds = present ? h->bondCount() : 0;
  char text[sizeof(statusText_)];
  if (!present) {
    snprintf(text, sizeof(text), "Bluetooth is not in this build.");
  } else if (state == HidState::Connected) {
    snprintf(text, sizeof(text), "Connected");
  } else if (state == HidState::Failed) {
    snprintf(text, sizeof(text), "%s", h->error()[0] ? h->error() : "Bluetooth did not start");
  } else if (state == HidState::Advertising && bonds == 0) {
    snprintf(text, sizeof(text), "On your computer, open Bluetooth settings and add \"%s\".", DeckApp::deviceName());
  } else if (state == HidState::Advertising) {
    snprintf(text, sizeof(text), "Waiting for your computer\xe2\x80\xa6");
  } else {
    snprintf(text, sizeof(text), "Starting Bluetooth\xe2\x80\xa6");
  }
  if (strcmp(text, statusText_) != 0 || state != state_ || bonds != bonds_ || present != present_) {
    copyText(statusText_, sizeof(statusText_), text);
    state_ = state;
    bonds_ = bonds;
    present_ = present;
    ctx.invalidate(ctx.contentRect());
  }
}

void DeckApp::DeckScreen::onShow(AppContext &ctx) {
  HidDevice *h = ctx.hid();
  if (h && h->available() && h->state() == HidState::Off) h->start(DeckApp::deviceName());
  statusText_[0] = '\0';
  for (bool &p : pressed_) p = false;
  refresh(ctx);
}

void DeckApp::DeckScreen::onHide(AppContext &ctx) {
  HidDevice *h = ctx.hid();
  if (h && h->state() != HidState::Off) h->stop();  // the radio only runs while the Deck is showing
}

uint32_t DeckApp::DeckScreen::onTick(AppContext &ctx) {
  refresh(ctx);
  return 500;
}

void DeckApp::DeckScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  const bool connected = state_ == HidState::Connected;
  // The status: the Bluetooth rune, and what to do or that it is connected.
  const int icon = t.dp(18);
  icons::bluetooth(c, center(leading(status_, icon), icon, icon), connected ? p.accent : p.textMuted);
  const bool instruction = state_ == HidState::Advertising && bonds_ == 0;  // the longest text: smaller
  c.drawWrapped(instruction ? *t.font.caption : *t.font.body, statusText_,
                Rect(status_.x + icon + t.dp(8), status_.y, status_.w - icon - t.dp(8), status_.h),
                state_ == HidState::Failed ? p.danger : connected ? p.accent : p.textMuted, instruction ? 3 : 2, Align::Left);
  if (bonds_ > 0) forget_.draw(c, t);
  for (int i = 0; i < kPads; ++i) {
    const Rect pad = pads_[i];
    paintGlass(c, t, pad, t.m.radiusCard);
    if (pressed_[i]) c.fillRoundRect(pad, t.m.radiusCard, p.accent, 46);
    const Color ink = connected ? p.text : p.textMuted;
    const int size = t.dp(28);
    Rect parts[2];
    column(pad.inset(t.dp(4), t.dp(6)), {flex(), px(t.font.caption->lineHeight)}, t.dp(2), parts);
    kButtons[i].icon(c, center(parts[0], size, size), connected ? p.accent : p.textMuted);
    c.drawTextIn(*t.font.caption, kButtons[i].label, parts[1], ink, Align::Center);
  }
}

bool DeckApp::DeckScreen::onEvent(const Event &e, AppContext &ctx) {
  HidDevice *h = ctx.hid();
  if (bonds_ > 0 && forget_.handle(e, ctx)) {
    if (h) {
      h->stop();
      h->forgetBonds();
      h->start(DeckApp::deviceName());
    }
    ctx.toast("Paired computers forgotten");
    refresh(ctx);
    return true;
  }
  for (int i = 0; i < kPads; ++i) {
    bool pressed = pressed_[i];
    const bool tapped = trackPress(pads_[i], e, pressed, ctx, pads_[i].inset(-glassOutset(ctx.theme())));
    if (pressed != pressed_[i]) pressed_[i] = pressed;
    if (!tapped) continue;
    if (!h || state_ != HidState::Connected) ctx.toast("Pair your computer first");
    else if (!h->sendConsumer(kButtons[i].usage)) ctx.toast("Not connected");
    return true;
  }
  return e.isPointer();
}

}  // namespace apps
}  // namespace stella
