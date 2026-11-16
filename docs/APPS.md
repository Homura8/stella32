# Writing an app

An app is a statically allocated object with an `AppInfo` and a root
`Screen`. It talks to the system only through `AppContext` (time, theme,
navigation, toasts, settings, storage, status).

```cpp
// src/stella/apps/hello_app.h
#include "../app/app.h"

namespace stella::apps {

class HelloApp : public App {
 public:
  const AppInfo &info() const override {
    static const AppInfo kInfo{"hello", "Hello", icons::sparkle, 2, 0};
    return kInfo;  // id: stable, <= 15 chars; tint: Palette::tints index
  }
  Screen &root() override { return screen_; }

 private:
  class HelloScreen : public Screen {
   public:
    // 1. Every rectangle comes from the content rect and theme tokens.
    void layout(const Rect &content, const Theme &t) override {
      Rect r[3];
      layout::column(content.inset(t.m.margin, t.m.gapS),
                     {layout::flex(), layout::px(t.font.headline->lineHeight), layout::px(t.m.control)},
                     t.m.gapM, r);
      title_ = r[1];
      button_.bounds = layout::center(r[2], t.dp(140), t.m.control);
      button_.label = "Say hello";
    }
    // 2. Draw is deterministic: it reads state, never changes it.
    void draw(Canvas &c, const Theme &t) override {
      c.drawTextIn(*t.font.headline, text_, title_, t.color.text, Align::Center);
      button_.draw(c, t);
    }
    // 3. Events change state and invalidate exactly what changed.
    bool onEvent(const Event &e, AppContext &ctx) override {
      if (!button_.handle(e, ctx)) return false;
      text_ = "Hello, sky";
      ctx.invalidate(title_);
      ctx.toast("Waved hello");
      return true;
    }

   private:
    Rect title_;
    Button button_;
    const char *text_ = "Hi";
  };
  HelloScreen screen_;
};

}  // namespace stella::apps
```

Install it in the board sketch (and in `host/sim_session.*` so it appears in
screenshots and tests):

```cpp
static stella::apps::HelloApp hello;
os.install(hello);
```

## Lifecycle

| Callback | When |
| --- | --- |
| `App::onStart` | once, the first time the app is launched |
| `Screen::layout` | before showing, and whenever the theme/density changes |
| `Screen::onShow` / `onHide` | screen becomes visible / is covered or left |
| `Screen::onTick` | while visible; return ms until the next tick or `kNoTick` |
| `Screen::onEvent` | Press, Drag, Release, Tap, LongPress (and system events) |
| `App::onTopic` | a topic changed (any running app, foreground or not) |

Navigation: `ctx.push(screen)` for detail views, `ctx.pop()` to go back (the
status bar does this for the user), `ctx.launch(id)`, `ctx.goHome()`.

## Rules of thumb

- Use theme tokens and the layout DSL; never literal colours or coordinates.
- Keep `draw()` cheap and side-effect free; it is called once per render chunk.
- Precompute strings in `onShow`/`onTick`/`onEvent`, not in `draw()`.
- Invalidate the smallest rectangle that changed (plus `glassOutset()` for
  raised glass). An idle screen should cause no rendering at all.
- Long work belongs in a `Job` on the service thread, publishing a
  `Topic<T>`; the app reads it in `onTopic`.
- Persist small app state with `ctx.saveAppRecord(key, version, &data, size)`.
- Check the result at both densities: `python3 tools/screenshots.py`.
