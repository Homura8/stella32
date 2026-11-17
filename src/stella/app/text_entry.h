#pragma once
// Reusable text entry screen: a field plus the on-screen keyboard. Push it,
// and receive the text through a callback when the user presses the action
// key. Used for Wi-Fi passwords; any app can use it.
//
//   entry.open("Starlight", "Join", /*secret=*/true, onJoin, this);
//   ctx.push(entry);
#include <stddef.h>

#include "../ui/keyboard.h"
#include "app.h"

namespace stella {

class TextEntryScreen : public Screen {
 public:
  static constexpr size_t kMaxLength = 64;
  using Submit = void (*)(const char *text, void *context, AppContext &ctx);

  // Prepare before pushing. `title` and `action` must outlive the screen.
  void open(const char *title, const char *action, bool secret, Submit submit, void *context,
            size_t minLength = 0);
  const char *text() const { return text_; }
  const Keyboard &keyboard() const { return keyboard_; }

  const char *title() const override { return title_; }
  void layout(const Rect &content, const Theme &t) override;
  void draw(Canvas &c, const Theme &t) override;
  bool onEvent(const Event &e, AppContext &ctx) override;
  void onHide(AppContext &ctx) override;

 private:
  void updateDone();
  const char *title_ = "";
  Submit submit_ = nullptr;
  void *context_ = nullptr;
  bool secret_ = false, revealed_ = false, revealPressed_ = false;
  size_t length_ = 0, minLength_ = 0;
  char text_[kMaxLength + 1] = {0};
  Rect field_, textBox_, reveal_;
  Keyboard keyboard_;
};

}  // namespace stella
