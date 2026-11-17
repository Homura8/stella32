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

## Background data (services)

Long work runs as a `Job` on the service thread and publishes a `Topic<T>`.
Register both while installing; the Weather app is a complete example
(`src/stella/apps/weather_app.*`).

```cpp
void onInstall(ServiceHost &host) override {
  service_.attach(host.http());   // HttpService: TLS lease, limits, JSON
  host.attachTopic(topic_);       // id from topics::kFirstUser upwards
  host.addJob(service_, 3000);    // first run after 3 s
}
void onTopic(uint16_t id, AppContext &ctx) override {
  if (id == kMyTopic) screen_.refresh(ctx);  // read topic_.get(), invalidate
}
```

In the job, `http.getJson(url, handler)` returns a `FetchResult`; return its
`jobResult()` (a busy lease becomes `Deferred`, not a failure) and record the
outcome in a `DataStatus` so the screen can explain stale data.

## Lifecycle

| Callback | When |
| --- | --- |
| `App::onStart` | once, the first time the app is launched |
| `Screen::layout` | before showing, and whenever the theme/density changes |
| `Screen::onShow` / `onHide` | screen becomes visible / is covered or left |
| `Screen::onTick` | while visible; return ms until the next tick or `kNoTick` |
| `Screen::onEvent` | Press, Drag, Release, Tap, LongPress (and system events) |
| `App::onTopic` | a topic changed (any running app, foreground or not) |
| `App::onInstall` | at boot, to register jobs and topics |
| `App::onRoute` | launched with a route: `ctx.launch("settings", "wifi")` |
| `App::onWake` | background timer for every app; return ms until the next call |
| `App::onAlert` | the user answered one of the app's alerts, or it timed out |

Navigation: `ctx.push(screen)` for detail views, `ctx.pop()` to go back (the
status bar does this for the user), `ctx.launch(id)`, `ctx.goHome()`.

System pieces apps can use: `ctx.network()` (Wi-Fi status, scan, join,
forget), `TextEntryScreen` (text or password entry with the on-screen
keyboard), `ctx.requestJob(job)` (refresh now), `ctx.requestTick()` (restart
a screen's `onTick` after it returned `kNoTick`).

## Idle faces and fullscreen screens

A screen can cover the whole display: override `fullscreen()` to return true
(no sky, status bar or back gesture under it; `layout()` gets the full display;
`draw()` must paint every pixel, e.g. `c.fill(p.bedBg)` first) and handle
leaving yourself (`ctx.pop()` on a tap). Flag the app `kAppIdleFace` and handle
the route `"bedside"` in `onRoute()` to be started after the idle dim at night.
The Clock app's `BedsideScreen` is the example. `ctx.setNextAlarm(epoch)` lets
the shell and the bedside clock show when your app next rings, and
`ctx.setGlance(icon, "17° Partly cloudy")` offers a one-line summary for the
bedside clock (the Weather app does; publish it from `onWake`, since apps that
were never opened get no topic notifications, and withdraw it when stale).

## Time-based work and alerts

Things that must happen whether or not the app is open (an alarm, a timer)
use `onWake`, not `onTick` (which only runs while a screen is visible).
Return the delay to the *nearest* deadline the app has; `ctx.scheduleWake(ms)`
brings the next call forward (e.g. after the user starts a timer). Use the
wall clock (`ctx.localTime()`) for times of day and `ctx.millis()` with
`core/deadline.h` for durations.

When something needs the user's attention, raise a system alert; it wakes the
screen, plays the chime if asked, and appears over any app:

```cpp
Alert a;
a.title = "Time's up";
a.message = "5 min timer";
a.icon = icons::hourglass;
a.primary = "Done";
a.secondary = "Again";   // optional
a.tag = kDoneTag;        // yours; identifies the alert in onAlert
a.sound = true;
a.timeoutMs = 2 * 60 * 1000;
ctx.showAlert(a);

void onAlert(uint16_t tag, int action, AppContext &ctx) override {
  if (tag == kDoneTag && action == kAlertSecondary) start(ctx);
}
```

The Alarms and Timer apps (`src/stella/apps/alarms_app.*`, `timer_app.*`)
are complete examples. The Stopwatch (`stopwatch_app.*`) shows the other
shape of time-based work: no alert, state that keeps counting while another
app is in front (it only stores the start time, so nothing has to run), a
wake every 12 hours that folds the running segment into the total so the
counter can never wrap, and a screen tick aligned to the next tenth of a
second that repaints only the digits that changed.

## Rules of thumb

- Use theme tokens and the layout DSL; never literal colours or coordinates.
- Keep `draw()` cheap and side-effect free; it is called once per render chunk.
- Precompute strings in `onShow`/`onTick`/`onEvent`, not in `draw()`.
- Invalidate the smallest rectangle that changed (plus `glassOutset()` for
  raised glass). An idle screen should cause no rendering at all.
  Invalidations from a background app are ignored; check `isVisible()` to
  skip work entirely.
- Long work belongs in a `Job` on the service thread, publishing a
  `Topic<T>`; the app reads it in `onTopic`.
- A scrolling `ListView` sets `buttonWidth = t.dp(32)` in `layout()` to get
  up/down buttons and a rail when it overflows (resistive panels make drags
  hard); draw and pick rows in `list.rows()`, not `list.bounds`.
- Persist small app state with `ctx.saveAppRecord(key, version, &data, size)`.
- Check the result at both densities: `python3 tools/screenshots.py`.
