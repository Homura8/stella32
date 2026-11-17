#include "text_entry.h"

#include <string.h>

#include "../ui/layout.h"
#include "../ui/materials.h"

namespace stella {

using namespace layout;

void TextEntryScreen::open(const char *title, const char *action, bool secret, Submit submit, void *context,
                           size_t minLength) {
  title_ = title ? title : "";
  keyboard_.doneLabel = action ? action : "Done";
  keyboard_.mode = Keyboard::Mode::Lower;
  secret_ = secret;
  revealed_ = false;
  submit_ = submit;
  context_ = context;
  minLength_ = minLength;
  length_ = 0;
  memset(text_, 0, sizeof(text_));
  updateDone();
}

void TextEntryScreen::updateDone() { keyboard_.doneEnabled = length_ >= minLength_; }

void TextEntryScreen::layout(const Rect &content, const Theme &t) {
  Rect r[2];
  column(content.inset(t.m.margin, t.m.gapS), {px(t.m.control), flex()}, t.m.gapS, r);
  field_ = r[0];
  Rect f[2];
  row(field_.inset(t.m.padding, 0), {flex(), px(secret_ ? t.dp(40) : 0)}, t.m.gapS, f);
  textBox_ = f[0];
  reveal_ = f[1];
  keyboard_.bounds = r[1];
}

void TextEntryScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  paintGlass(c, t, field_, field_.h / 2);
  char shown[kMaxLength * 3 + 2];
  if (secret_ && !revealed_) {
    size_t n = 0;
    for (size_t i = 0; i < length_ && n + 3 < sizeof(shown); ++i) {  // U+2022 bullet
      shown[n++] = '\xe2';
      shown[n++] = '\x80';
      shown[n++] = '\xa2';
    }
    shown[n] = '\0';
  } else {
    memcpy(shown, text_, length_ + 1);
  }
  // Keep the end of long text (where the caret is) visible.
  const Font &f = *t.font.body;
  const char *visible = shown;
  while (*visible && Canvas::textWidth(f, visible) > textBox_.w - t.dp(6)) {
    do { ++visible; } while (((unsigned char)*visible & 0xC0) == 0x80);
  }
  if (length_ == 0) {
    c.drawTextIn(f, secret_ ? "Password" : "Type here", textBox_, p.textMuted);
  } else {
    c.drawTextIn(f, visible, textBox_, p.text);
  }
  const int caretX = textBox_.x + (length_ ? Canvas::textWidth(f, visible) : 0) + 1;
  c.fillRect(Rect(caretX, textBox_.centerY() - f.ascent / 2 - 1, t.dp(2) > 1 ? t.dp(2) : 1, f.ascent + 2), p.accent);
  if (secret_) {
    if (revealPressed_) c.fillRoundRect(reveal_.inset(0, t.dp(4)), t.m.radiusControl, p.accent, 36);
    c.drawTextIn(*t.font.caption, revealed_ ? "Hide" : "Show", reveal_, p.accent, Align::Center);
  }
  keyboard_.draw(c, t);
}

bool TextEntryScreen::onEvent(const Event &e, AppContext &ctx) {
  if (secret_ && trackPress(reveal_.inset(-4), e, revealPressed_, ctx, reveal_)) {
    revealed_ = !revealed_;
    ctx.invalidate(field_);
    return true;
  }
  const KeyInput key = keyboard_.handle(e, ctx);
  switch (key.kind) {
    case KeyInput::Char:
      if (length_ < kMaxLength) {
        text_[length_++] = key.ch;
        text_[length_] = '\0';
      }
      break;
    case KeyInput::Backspace:
      if (length_ > 0) text_[--length_] = '\0';
      break;
    case KeyInput::Clear:
      length_ = 0;
      text_[0] = '\0';
      break;
    case KeyInput::Done:
      if (submit_) submit_(text_, context_, ctx);
      return true;
    case KeyInput::None:
      return true;
  }
  const bool wasEnabled = keyboard_.doneEnabled;
  updateDone();
  ctx.invalidate(field_);
  if (wasEnabled != keyboard_.doneEnabled) ctx.invalidate(keyboard_.bounds);
  return true;
}

void TextEntryScreen::onHide(AppContext &) {
  // Never leave a typed secret in memory longer than needed.
  volatile char *p = text_;
  for (size_t i = 0; i < sizeof(text_); ++i) p[i] = 0;
  length_ = 0;
}

}  // namespace stella
