#pragma once
// Calendar: the next few weeks of events from an iCalendar feed (a Google
// Calendar "secret address in iCal format", a university timetable...). The
// address is private, so it is set on the remote page (Settings > Calendar says
// where) and kept only in a versioned record (`net/calendar_source.h`).
//
// A job on the service thread fetches the feed (https only, streamed through
// `ICalParser`, so a large file costs no more memory than a small one), keeps
// what matters for the next 35 days (`ICalCalendar`), expands repeats in the
// device's own time zone and publishes a CalendarSnapshot topic: at most 16
// occurrences, earliest first. The screen only reads that topic. Last-good data
// stays on screen with a freshness line when the network or the feed fails.
#include <atomic>

#include "../app/app.h"
#include "../net/ical_calendar.h"
#include "../net/posix_zone.h"
#include "../service/data_status.h"

namespace stella {
namespace apps {

struct CalendarItem {
  int64_t start = 0;  // UTC; an all-day event: local midnight of its first day
  int64_t end = 0;
  bool allDay = false;
  char title[36] = {0};
  char where[20] = {0};
};

struct CalendarSnapshot {
  static constexpr int kItems = 16;
  bool hasAddress = false;
  uint8_t count = 0;
  bool more = false;           // there were more events than fit
  uint16_t events = 0;         // read from the feed
  uint16_t unreadable = 0;     // skipped: no usable start, an unsupported repeat, a zone it did not know
  uint16_t dropped = 0;        // did not fit in memory
  int64_t from = 0;            // local midnight of the first day shown
  CalendarItem items[kItems];
  DataStatus status;
};

namespace topics {
constexpr uint16_t kCalendar = stella::topics::kFirstUser + 1;
}

class CalendarService : public Job, public HttpSink {
 public:
  static constexpr uint32_t kFreshForMs = 90u * 60u * 1000u;
  static constexpr size_t kMaxFeedBytes = 2u * 1024u * 1024u;
  static constexpr int kWindowDays = 35;   // what is kept from the feed
  static constexpr int kShownDays = 30;    // what the screen lists

  explicit CalendarService(Topic<CalendarSnapshot> &topic) : topic_(topic), parser_(calendar_) {}
  void attach(HttpService &http, CalendarSource &source, ServiceHost &host) {
    http_ = &http;
    source_ = &source;
    host_ = &host;
  }
  // The device's POSIX time zone (the UI thread tells the service when it changes).
  void setZone(const char *posixTz);

  const char *name() const override { return "calendar"; }
  JobPolicy policy() const override;
  JobResult run(uint32_t nowMs) override;
  bool onData(const uint8_t *data, size_t size) override;

 private:
  Topic<CalendarSnapshot> &topic_;
  HttpService *http_ = nullptr;
  CalendarSource *source_ = nullptr;
  ServiceHost *host_ = nullptr;
  ICalCalendar calendar_;
  ICalParser parser_;
  osal::Mutex zoneLock_;
  char zone_[48] = "UTC0";
  uint32_t generation_ = 0;  // of the address the snapshot's items came from
  bool feedOk_ = true;
};

class CalendarApp : public App {
 public:
  CalendarApp();
  const AppInfo &info() const override;
  Screen &root() override { return screen_; }
  void onInstall(ServiceHost &host) override;
  void onStart(AppContext &ctx) override;
  void onTopic(uint16_t topicId, AppContext &ctx) override;
  uint32_t onWake(AppContext &ctx) override;
  // For tests and screenshots.
  Rect setupButton() const { return screen_.setupBounds(); }
  Rect listBounds() const { return screen_.listBounds(); }
  CalendarSnapshot snapshot() const { return topic_.get(); }

 private:
  class AgendaScreen : public Screen {
   public:
    explicit AgendaScreen(CalendarApp &app) : app_(app) {}
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    uint32_t onTick(AppContext &ctx) override;
    void refresh(AppContext &ctx);
    Rect setupBounds() const { return setup_.bounds; }
    Rect listBounds() const { return list_.bounds; }

   private:
    struct Row {
      char day[10];     // "Today", "Tomorrow", "Wed 19" on the first event of a day, else empty
      char line[72];    // "09:00-10:30 · Room 4"
      bool past;        // already over
    };
    static void drawRow(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed, void *self);
    void describeStatus(AppContext &ctx, char *out, size_t capacity) const;
    CalendarApp &app_;
    Rect status_, titleBox_, hintBox_;
    ListView list_;
    Button setup_;
    CalendarSnapshot snap_;
    Row rows_[CalendarSnapshot::kItems];
    int dayColumn_ = 0;
    char statusText_[72] = {0};
    char title_[40] = {0}, hint_[96] = {0};  // the message when there is nothing to list
  };

  void pushZone(AppContext &ctx);

  Topic<CalendarSnapshot> topic_{topics::kCalendar};
  CalendarService service_{topic_};
  AgendaScreen screen_{*this};
  char zone_[48] = {0};
};

}  // namespace apps
}  // namespace stella
