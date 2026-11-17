#pragma once
// Alarms: up to six alarms, each once or on chosen weekdays, ringing through
// a system alert (with the chime) whichever app is in front. Snooze is nine
// minutes; an unanswered alarm stops by itself after ten.
//
// Alarms follow local wall-clock time, so they need the time to be known
// (Wi-Fi + SNTP); the list says so when it is not.
#include "../app/app.h"
#include "common.h"  // describeWait

namespace stella {
namespace apps {

struct AlarmEntry {
  uint8_t hour = 7, minute = 0;
  uint8_t days = 0;     // bit n = weekday n (0 = Sunday); 0 = once
  uint8_t enabled = 1;
};

// Persisted as the "book" record of the "alarms" app.
struct AlarmBook {
  static constexpr uint16_t kVersion = 1;
  static constexpr int kMax = 6;
  uint8_t count = 0;
  uint8_t reserved[3] = {};
  AlarmEntry alarms[kMax];

  void sanitize();
};

constexpr uint8_t kWeekdays = 0x3E;  // Monday to Friday
constexpr uint8_t kWeekend = 0x41;   // Saturday and Sunday
constexpr uint8_t kEveryDay = 0x7F;

// Pure scheduling logic (unit tested).
bool alarmDue(const AlarmEntry &alarm, const LocalTime &now);  // rings in this minute
// Minutes from now's minute to the alarm's next ring (1 .. 7 * 1440), or -1.
int minutesUntil(const AlarmEntry &alarm, const LocalTime &now);
// Index of the alarm that rings next, or -1; `minutes` receives the wait.
int nextAlarm(const AlarmBook &book, const LocalTime &now, int &minutes);
// "Once", "Every day", "Weekdays", "Weekends" or "Mon, Wed, Fri".
void describeDays(uint8_t days, char *out, size_t capacity, bool weekStartsSunday = false);

class AlarmsApp : public App {
 public:
  static constexpr uint16_t kAlarmTag = 1;
  static constexpr uint32_t kSnoozeSec = 9 * 60;
  static constexpr uint32_t kRingMs = 10u * 60u * 1000u;

  AlarmsApp();
  const AppInfo &info() const override;
  Screen &root() override { return list_; }
  void onInstall(ServiceHost &host) override;
  uint32_t onWake(AppContext &ctx) override;
  void onAlert(uint16_t tag, int action, AppContext &ctx) override;

  // Add (index < 0) or replace an alarm and save; false when the book is full.
  bool setAlarm(int index, const AlarmEntry &alarm, AppContext &ctx);
  const AlarmBook &book() const { return book_; }
  bool snoozed() const { return snoozeUntil_ != 0; }

 private:
  class ListScreen : public Screen {
   public:
    explicit ListScreen(AlarmsApp &app) : app_(app) {}
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    void refresh(AppContext &ctx);  // re-read the book and invalidate what changed

   private:
    static void drawAlarm(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed, void *self);
    void arrange();
    AlarmsApp &app_;
    Theme theme_{};
    Rect area_, header_, body_, card_, empty_;
    Button add_;
    ListView list_;
    char next_[48] = {0};
    char times_[AlarmBook::kMax][12] = {};
    char suffixes_[AlarmBook::kMax][4] = {};
    char days_[AlarmBook::kMax][32] = {};
    uint8_t enabled_[AlarmBook::kMax] = {};
    int count_ = -1;
    bool timeKnown_ = true;
  };

  class EditScreen : public Screen {
   public:
    explicit EditScreen(AlarmsApp &app) : app_(app) {}
    const char *title() const override { return index_ < 0 ? "New alarm" : "Edit alarm"; }
    void layout(const Rect &content, const Theme &t) override;
    void draw(Canvas &c, const Theme &t) override;
    bool onEvent(const Event &e, AppContext &ctx) override;
    void onShow(AppContext &ctx) override;
    void edit(int index);  // -1: a new alarm

   private:
    void arrange();
    void setTime(int hour, int minute, AppContext &ctx);
    void refreshSummary(AppContext &ctx);
    int columnAt(int x, int y) const;  // 0 hour, 1 minute, -1 none
    AlarmsApp &app_;
    Theme theme_{};
    Rect content_, time_, summary_, daysRow_, buttons_;
    Rect columns_[2], colon_, meridiem_;
    Button save_, delete_;
    int index_ = -1;
    AlarmEntry entry_;
    bool clock24h_ = true, meridiemPressed_ = false, sundayFirst_ = false;
    int dragColumn_ = -1, dragBase_ = 0, pressedDay_ = -1;
    char digits_[2][4] = {}, meridiemText_[4] = {0}, summaryText_[72] = {0};
  };

  void save(AppContext &ctx);
  void publishNext(AppContext &ctx);  // tell the shell when the next alarm rings
  void ring(const LocalTime &now, int hour, int minute, bool snoozed, AppContext &ctx);
  void formatAlarm(const AppContext &ctx, int hour, int minute, char *time, size_t timeCap, char *suffix,
                   size_t suffixCap) const;

  AlarmBook book_;
  int64_t lastMinute_ = -1;    // epoch minute last checked
  int64_t snoozeUntil_ = 0;    // epoch seconds; 0 = not snoozed
  uint8_t ringingHour_ = 0, ringingMinute_ = 0;
  ListScreen list_;
  EditScreen edit_;
};

}  // namespace apps
}  // namespace stella
