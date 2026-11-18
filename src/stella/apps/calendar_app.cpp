#include "calendar_app.h"

#include <stdio.h>
#include <string.h>

#include "../ui/layout.h"
#include "../ui/materials.h"
#include "common.h"

namespace stella {
namespace apps {

using namespace layout;

namespace {

void wipe(void *p, size_t n) {
  volatile char *v = static_cast<volatile char *>(p);
  while (n--) *v++ = 0;
}

}  // namespace

// ------------------------------------------------------------------ service

JobPolicy CalendarService::policy() const {
  JobPolicy p;
  p.intervalMs = 30u * 60u * 1000u;
  p.retryMs = 2u * 60u * 1000u;
  p.maxBackoffMs = 30u * 60u * 1000u;
  p.deferMs = 5000;
  p.needsNetwork = true;
  return p;
}

void CalendarService::setZone(const char *posixTz) {
  osal::LockGuard<osal::Mutex> guard(zoneLock_);
  copyText(zone_, sizeof(zone_), posixTz ? posixTz : "UTC0");
}

bool CalendarService::onData(const uint8_t *data, size_t size) {
  feedOk_ = parser_.feed(reinterpret_cast<const char *>(data), size);
  return feedOk_;
}

JobResult CalendarService::run(uint32_t nowMs) {
  if (!http_ || !source_ || !host_) return JobResult::Ok;
  CalendarSnapshot snap = topic_.get();
  char url[kCalendarUrlBytes];
  const bool has = source_->copyAddress(url);
  const uint32_t generation = source_->generation();
  if (generation != generation_ || has != snap.hasAddress) {  // another address: its data is not the old one's
    snap = CalendarSnapshot{};
    generation_ = generation;
  }
  snap.hasAddress = has;
  if (!has) {
    topic_.publish(snap);
    return JobResult::Ok;
  }
  int64_t now = 0;
  if (!host_->wallClock(now)) {  // no time yet: nothing to window by
    wipe(url, sizeof(url));
    topic_.publish(snap);
    return JobResult::Deferred;
  }
  char tz[sizeof(zone_)];
  {
    osal::LockGuard<osal::Mutex> guard(zoneLock_);
    copyText(tz, sizeof(tz), zone_);
  }
  PosixZoneResolver zone;
  zone.set(tz);
  const int64_t today = civil::floorDiv(zone.epochToLocal(now), 86400) * 86400;  // local midnight, as wall seconds
  const int64_t from = zone.localToEpoch(today);
  const int64_t to = from + (int64_t)kWindowDays * 86400;

  calendar_.clear();
  calendar_.setWindow(from - 86400, to);
  parser_.reset();
  feedOk_ = true;
  FetchLimits limits;
  limits.timeoutMs = 60000;
  limits.maxBytes = kMaxFeedBytes;
  limits.leaseWaitMs = 30000;
  FetchResult r = http_->get(url, *this, limits);
  wipe(url, sizeof(url));  // the address is private
  if (r.kind == FetchResult::Busy) {
    topic_.publish(snap);
    return JobResult::Deferred;  // keep everything as it is
  }
  if (r.ok() && !parser_.finish()) {
    r.kind = FetchResult::Invalid;
    copyText(r.message, sizeof(r.message), parser_.error() ? parser_.error() : "Not a calendar");
  } else if (r.kind == FetchResult::Invalid && !feedOk_) {
    copyText(r.message, sizeof(r.message), parser_.error() ? parser_.error() : "Not a calendar");
  } else if (r.kind == FetchResult::TooLarge) {
    copyText(r.message, sizeof(r.message), "Calendar too large");
  }
  if (!r.ok()) {
    snap.status.failed(nowMs, r.message);
    topic_.publish(snap);
    return r.jobResult();
  }
  // What the next weeks hold, in the device's own time zone.
  ICalOccurrence found[CalendarSnapshot::kItems];
  bool more = false;
  const size_t n = calendar_.occurrences(from, from + (int64_t)kShownDays * 86400, zone, found,
                                         CalendarSnapshot::kItems, &more);
  snap.count = (uint8_t)n;
  snap.more = more;
  snap.from = from;
  for (size_t i = 0; i < n; ++i) {
    CalendarItem &item = snap.items[i];
    item.start = found[i].start;
    item.end = found[i].end;
    item.allDay = found[i].allDay;
    copyText(item.title, sizeof(item.title), found[i].summary[0] ? found[i].summary : "(no title)");
    copyText(item.where, sizeof(item.where), found[i].location);
  }
  const ICalParser::Stats &st = parser_.stats();
  snap.events = (uint16_t)(st.events > 0xFFFF ? 0xFFFF : st.events);
  const uint32_t unreadable = st.skipped + st.unsupportedRules + st.unresolvedZones;
  snap.unreadable = (uint16_t)(unreadable > 0xFFFF ? 0xFFFF : unreadable);
  snap.dropped = (uint16_t)(calendar_.dropped() > 0xFFFF ? 0xFFFF : calendar_.dropped());
  snap.status.succeeded(nowMs);
  topic_.publish(snap);
  return JobResult::Ok;
}

// ------------------------------------------------------------------ app

CalendarApp::CalendarApp() {}

const AppInfo &CalendarApp::info() const {
  static const AppInfo kInfo{"calendar", "Calendar", icons::calendar, 2, 0};
  return kInfo;
}

void CalendarApp::onInstall(ServiceHost &host) {
  service_.attach(host.http(), host.calendarFeed(), host);
  host.calendarFeed().attachJob(service_);
  service_.setZone(host.systemSettings().timezone);
  copyText(zone_, sizeof(zone_), host.systemSettings().timezone);
  host.attachTopic(topic_);
  host.addJob(service_, 25000);  // after the first weather fetch has had the network
}

void CalendarApp::onStart(AppContext &ctx) { pushZone(ctx); }

// The service reads the zone from here; a change means the days were cut at the wrong midnight.
void CalendarApp::pushZone(AppContext &ctx) {
  const char *tz = ctx.settings().timezone;
  if (strcmp(tz, zone_) == 0) return;
  copyText(zone_, sizeof(zone_), tz);
  service_.setZone(tz);
  if (ctx.calendar().hasAddress()) ctx.requestJob(service_);
}

void CalendarApp::onTopic(uint16_t topicId, AppContext &ctx) {
  if (topicId == topics::kCalendar || topicId == stella::topics::kNetwork) screen_.refresh(ctx);
}

uint32_t CalendarApp::onWake(AppContext &ctx) {
  pushZone(ctx);
  return 60u * 1000u;
}

// ------------------------------------------------------------------ agenda

void CalendarApp::AgendaScreen::layout(const Rect &content, const Theme &t) {
  const Rect area = content.inset(t.m.margin, t.m.gapS);
  Rect r[2];
  column(area, {flex(), px(t.font.caption->lineHeight)}, t.m.gapS, r);
  const int rowH = t.font.body->lineHeight + t.font.caption->lineHeight + t.dp(10);
  list_.bounds = r[0];
  list_.rowHeight = rowH < t.m.row ? t.m.row : rowH;
  list_.buttonWidth = t.dp(32);
  status_ = r[1];
  // Room for the widest day label ("Wed 28 Sep", "Tomorrow").
  const int a = Canvas::textWidth(*t.font.caption, "Wed 28 Sep"), b = Canvas::textWidth(*t.font.caption, "Tomorrow");
  dayColumn_ = (a > b ? a : b) + t.dp(8);
  // With nothing to list: a title, a hint and (without an address) the button.
  const Rect box = r[0].inset(t.m.padding, t.m.padding);
  Rect parts[5];
  column(box,
         {flex(), px(t.font.headline->lineHeight), px(3 * t.font.body->lineHeight), px(t.m.control), flex()},
         t.m.gapM, parts);
  titleBox_ = parts[1];
  hintBox_ = parts[2];
  setup_.bounds = center(parts[3], t.dp(150), t.m.control);
  setup_.label = "Set up";
  setup_.icon = icons::gear;
  setup_.style = ButtonStyle::Accent;
}

void CalendarApp::AgendaScreen::describeStatus(AppContext &ctx, char *out, size_t capacity) const {
  out[0] = '\0';
  if (!snap_.hasAddress) return;
  const DataStatus &s = snap_.status;
  const NetworkStatus net = ctx.network().status();
  if (!s.hasData) {
    if (!net.present) snprintf(out, capacity, "This device has no network");
    else if (!net.connected) snprintf(out, capacity, "Connect to Wi-Fi to fetch your calendar");
    else if (s.failures) snprintf(out, capacity, "%s \xc2\xb7 retrying", s.error);
    else snprintf(out, capacity, "Fetching your calendar\xe2\x80\xa6");
    return;
  }
  const uint32_t minutes = elapsedMs(ctx.millis(), s.updatedMs) / 60000u;
  char age[24];
  if (minutes < 1) snprintf(age, sizeof(age), "just now");
  else if (minutes < 60) snprintf(age, sizeof(age), "%lu min ago", (unsigned long)minutes);
  else snprintf(age, sizeof(age), "%lu h ago", (unsigned long)(minutes / 60));
  if (s.freshness(ctx.millis(), CalendarService::kFreshForMs) == Freshness::Fresh) snprintf(out, capacity, "Updated %s", age);
  else if (!net.connected) snprintf(out, capacity, "Offline \xc2\xb7 from %s", age);
  else snprintf(out, capacity, "From %s \xc2\xb7 %s", age, s.lastOk ? "refreshing" : s.error);
  const unsigned skipped = (unsigned)snap_.unreadable + snap_.dropped;
  if (skipped) {
    const size_t used = strlen(out);
    snprintf(out + used, capacity - used, " \xc2\xb7 %u not shown", skipped);
  }
}

void CalendarApp::AgendaScreen::refresh(AppContext &ctx) {
  snap_ = app_.topic_.get();
  snap_.hasAddress = ctx.calendar().hasAddress();  // the address changes before the next fetch says so
  const LocalTime now = ctx.localTime();
  const bool h24 = ctx.settings().clock24h;
  const uint8_t order = ctx.settings().dateOrder;
  const int64_t today = now.valid ? civil::daysFromCivil(now.year, now.month, now.day) : 0;
  int64_t lastDay = -1;
  for (int i = 0; i < snap_.count; ++i) {
    const CalendarItem &item = snap_.items[i];
    Row &row = rows_[i];
    row.day[0] = row.line[0] = '\0';
    row.past = now.valid && item.end <= now.epoch && item.end > item.start;
    const LocalTime at = ctx.localTimeAt(item.start < snap_.from ? snap_.from : item.start);
    const int64_t day = civil::daysFromCivil(at.year, at.month, at.day);
    if (day != lastDay) {
      lastDay = day;
      if (day == today) copyText(row.day, sizeof(row.day), "Today");
      else if (day == today + 1) copyText(row.day, sizeof(row.day), "Tomorrow");
      else {
        char text[24];
        formatShortDate(at, text, sizeof(text), order);
        copyText(row.day, sizeof(row.day), text);
      }
    }
    char when[48] = "";
    if (item.allDay) {
      const int64_t span = (item.end - item.start + 43200) / 86400;  // days (a clock change can make one 23 or 25 h)
      if (span > 1) {
        const LocalTime last = ctx.localTimeAt(item.end - 3600);
        char to[24];
        formatShortDate(last, to, sizeof(to), order);
        snprintf(when, sizeof(when), "All day \xc2\xb7 to %s", to);
      } else {
        snprintf(when, sizeof(when), "All day");
      }
    } else {
      const LocalTime s = ctx.localTimeAt(item.start), e = ctx.localTimeAt(item.end);
      char a[12], b[12];
      ctx.formatTime(s, a, sizeof(a));
      const char *sa = h24 ? "" : s.hour < 12 ? " AM" : " PM";
      if (item.end > item.start) {
        ctx.formatTime(e, b, sizeof(b));
        const char *sb = h24 ? "" : e.hour < 12 ? " AM" : " PM";
        snprintf(when, sizeof(when), "%s%s\xe2\x80\x93%s%s", a, strcmp(sa, sb) == 0 ? "" : sa, b, sb);
      } else {
        snprintf(when, sizeof(when), "%s%s", a, sa);
      }
    }
    if (item.where[0]) snprintf(row.line, sizeof(row.line), "%s \xc2\xb7 %s", when, item.where);
    else copyText(row.line, sizeof(row.line), when);
  }
  list_.count = snap_.count;
  if (list_.offset > list_.maxOffset()) list_.offset = list_.maxOffset();
  // The message for an empty list.
  if (!snap_.hasAddress) {
    copyText(title_, sizeof(title_), "No calendar yet");
    copyText(hint_, sizeof(hint_), "Open Settings, then Web page, and paste the private address of your calendar (.ics).");
  } else if (!snap_.status.hasData) {
    copyText(title_, sizeof(title_), "Your calendar");
    copyText(hint_, sizeof(hint_), "The first fetch is on its way.");
  } else {
    copyText(title_, sizeof(title_), "Nothing coming up");
    copyText(hint_, sizeof(hint_), "No events in the next 30 days.");
  }
  char status[sizeof(statusText_)];
  describeStatus(ctx, status, sizeof(status));
  copyText(statusText_, sizeof(statusText_), status);
  if (isVisible()) ctx.invalidate(ctx.contentRect());
}

void CalendarApp::AgendaScreen::onShow(AppContext &ctx) { refresh(ctx); }

// Past events dim and the freshness line ages once a minute.
uint32_t CalendarApp::AgendaScreen::onTick(AppContext &ctx) {
  char before[sizeof(statusText_)];
  copyText(before, sizeof(before), statusText_);
  bool pastChanged = false;
  const LocalTime now = ctx.localTime();
  for (int i = 0; i < snap_.count && now.valid; ++i) {
    const bool past = snap_.items[i].end <= now.epoch && snap_.items[i].end > snap_.items[i].start;
    pastChanged |= past != rows_[i].past;
  }
  char status[sizeof(statusText_)];
  describeStatus(ctx, status, sizeof(status));
  if (pastChanged) {
    refresh(ctx);
  } else if (strcmp(status, before) != 0) {
    copyText(statusText_, sizeof(statusText_), status);
    ctx.invalidate(status_);
  }
  return now.valid ? (uint32_t)(60 - now.second) * 1000u + 50u : 5000u;
}

void CalendarApp::AgendaScreen::drawRow(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed, void *self) {
  const AgendaScreen &s = *static_cast<const AgendaScreen *>(self);
  const Palette &p = t.color;
  const CalendarItem &item = s.snap_.items[index];
  const Row &r = s.rows_[index];
  if (pressed) c.fillRoundRect(row.inset(t.dp(4), 1), t.m.radiusControl, p.accent, 36);
  const Rect inner = row.inset(t.m.padding, 0);
  Rect cols[2];
  layout::row(inner, {px(s.dayColumn_), flex()}, t.m.gapS, cols);
  Rect lines[2];
  column(center(cols[1], cols[1].w, t.font.body->lineHeight + t.font.caption->lineHeight),
         {px(t.font.body->lineHeight), px(t.font.caption->lineHeight)}, 0, lines);
  c.drawTextIn(*t.font.caption, r.day, Rect(cols[0].x, lines[0].y, cols[0].w, lines[0].h), p.accent, Align::Left);
  c.drawTextIn(*t.font.body, item.title, lines[0], r.past ? p.textMuted : p.text);
  c.drawTextIn(*t.font.caption, r.line, lines[1], p.textMuted);
}

void CalendarApp::AgendaScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  if (snap_.count > 0) {
    paintGlass(c, t, list_.bounds, t.m.radiusCard);
    list_.draw(c, t, drawRow, this);
  } else {
    c.drawTextIn(*t.font.headline, title_, titleBox_, p.text, Align::Center);
    c.drawWrapped(*t.font.body, hint_, hintBox_, p.textMuted, 3, Align::Center);
    if (!snap_.hasAddress) setup_.draw(c, t);
  }
  c.drawTextIn(*t.font.caption, statusText_, status_, p.textMuted, Align::Center);
}

bool CalendarApp::AgendaScreen::onEvent(const Event &e, AppContext &ctx) {
  if (snap_.count == 0 && !snap_.hasAddress && setup_.handle(e, ctx)) {
    ctx.launch("settings", "calendar");
    return true;
  }
  if (snap_.count > 0) {
    list_.handle(e, ctx);
    return true;
  }
  return false;
}

}  // namespace apps
}  // namespace stella
