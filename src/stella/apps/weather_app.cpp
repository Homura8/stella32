#include "weather_app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../ui/layout.h"
#include "../ui/materials.h"
#include "common.h"

namespace stella {
namespace apps {

using namespace layout;

namespace {

const WeatherPlace kPlaces[] = {
    {"Melbourne", -37.81f, 144.96f, "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Sydney", -33.87f, 151.21f, nullptr},
    {"Brisbane", -27.47f, 153.03f, "AEST-10"},
    {"Perth", -31.95f, 115.86f, nullptr},
    {"Auckland", -36.85f, 174.76f, "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Tokyo", 35.68f, 139.69f, "JST-9"},
    {"Singapore", 1.35f, 103.82f, "<+08>-8"},
    {"London", 51.51f, -0.13f, "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Paris", 48.86f, 2.35f, "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"New York", 40.71f, -74.01f, "EST5EDT,M3.2.0,M11.1.0"},
    {"Los Angeles", 34.05f, -118.24f, "PST8PDT,M3.2.0,M11.1.0"},
    {"Greenwich", 51.48f, 0.0f, "UTC0"},
};
constexpr int kPlaceCount = (int)(sizeof(kPlaces) / sizeof(kPlaces[0]));
constexpr uint16_t kPlaceVersion = 1;

uint8_t defaultPlaceFor(const char *tz) {
  for (int i = 0; i < kPlaceCount; ++i)
    if (kPlaces[i].timezone && strcmp(kPlaces[i].timezone, tz) == 0) return (uint8_t)i;
  return 0;
}

int16_t rounded(double v) { return (int16_t)lround(v); }

const char *shortWeekday(int weekday) {
  static const char *const kNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return kNames[(weekday % 7 + 7) % 7];
}

}  // namespace

const WeatherPlace *weatherPlaces(int &count) {
  count = kPlaceCount;
  return kPlaces;
}

const char *weatherLabel(int code) {
  if (code == 0) return "Clear";
  if (code == 1) return "Mostly clear";
  if (code == 2) return "Partly cloudy";
  if (code == 3) return "Overcast";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code == 66 || code == 67) return "Freezing rain";
  if (code >= 61 && code <= 65) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Showers";
  if (code == 85 || code == 86) return "Snow showers";
  if (code >= 95) return "Thunderstorm";
  return "Weather";
}

IconFn weatherIcon(int code, bool day) {
  if (code == 0 || code == 1) return day ? icons::sun : icons::moon;
  if (code == 2) return icons::partlyCloudy;
  if (code == 3) return icons::cloud;
  if (code == 45 || code == 48) return icons::fog;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return icons::snow;
  if (code >= 95) return icons::storm;
  if (code >= 51) return icons::rain;
  return icons::sparkle;
}

// ------------------------------------------------------------------ service

JobPolicy WeatherService::policy() const {
  JobPolicy p;
  p.intervalMs = 20u * 60u * 1000u;
  p.retryMs = 60u * 1000u;
  p.maxBackoffMs = 30u * 60u * 1000u;
  p.deferMs = 3000;
  p.needsNetwork = true;
  return p;
}

JobResult WeatherService::run(uint32_t nowMs) {
  WeatherSnapshot snap = topic_.get();
  const uint8_t place = place_.load() < kPlaceCount ? place_.load() : 0;
  snap.place = place;
  if (!http_ || !http_->available()) return JobResult::Ok;
  char url[384];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,is_day,wind_speed_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
           "&timezone=auto&forecast_days=%d&timeformat=unixtime",
           kPlaces[place].latitude, kPlaces[place].longitude, WeatherReport::kDays);
  parsing_ = WeatherReport{};
  parsing_.place = place;
  seenTemperature_ = seenCode_ = false;
  FetchLimits limits;
  limits.maxBytes = 8 * 1024;  // a forecast is ~1.5 KB
  FetchResult r = http_->getJson(url, *this, limits);
  if (r.ok() && !(seenTemperature_ && seenCode_ && parsing_.dayCount >= 3)) {
    r.kind = FetchResult::Invalid;
    copyText(r.message, sizeof(r.message), "Incomplete forecast");
  }
  if (r.kind == FetchResult::Busy) return JobResult::Deferred;  // keep everything as it is
  if (r.ok()) {
    parsing_.valid = true;
    snap.report = parsing_;
    snap.status.succeeded(nowMs);
  } else {
    snap.status.failed(nowMs, r.message);
  }
  topic_.publish(snap);
  return r.jobResult();
}

void WeatherService::onValue(const JsonPath &path, const JsonValue &v) {
  if (v.type != JsonType::Number) return;
  const int i = path.index();
  if (path.is("current.temperature_2m")) {
    parsing_.temperatureC = rounded(v.number);
    seenTemperature_ = true;
  } else if (path.is("current.apparent_temperature")) {
    parsing_.feelsLikeC = rounded(v.number);
  } else if (path.is("current.relative_humidity_2m")) {
    parsing_.humidity = (uint8_t)(v.number < 0 ? 0 : v.number > 100 ? 100 : v.number);
  } else if (path.is("current.weather_code")) {
    parsing_.code = rounded(v.number);
    seenCode_ = true;
  } else if (path.is("current.is_day")) {
    parsing_.day = v.number != 0;
  } else if (path.is("current.wind_speed_10m")) {
    parsing_.windKmh = (uint16_t)(v.number < 0 ? 0 : lround(v.number));
  } else if (path.is("current.time")) {
    parsing_.observed = (int64_t)v.number;
  } else if (i >= 0 && i < WeatherReport::kDays) {
    WeatherDay &d = parsing_.days[i];
    if (path.is("daily.time[]")) {
      d.date = (int64_t)v.number;
    } else if (path.is("daily.weather_code[]")) {
      d.code = rounded(v.number);
    } else if (path.is("daily.temperature_2m_max[]")) {
      d.maxC = rounded(v.number);
      if (i + 1 > parsing_.dayCount) parsing_.dayCount = (uint8_t)(i + 1);
    } else if (path.is("daily.temperature_2m_min[]")) {
      d.minC = rounded(v.number);
    } else if (path.is("daily.precipitation_probability_max[]")) {
      d.rainChance = (uint8_t)(v.number < 0 ? 0 : v.number > 100 ? 100 : v.number);
    }
  }
}

// ------------------------------------------------------------------ app

WeatherApp::WeatherApp() : main_(*this), places_(*this) {}

const AppInfo &WeatherApp::info() const {
  static const AppInfo kInfo{"weather", "Weather", icons::partlyCloudy, 2, 0};
  return kInfo;
}

void WeatherApp::onInstall(ServiceHost &host) {
  uint8_t saved = 0;
  place_ = host.loadRecordFor(info().id, "place", kPlaceVersion, &saved, sizeof(saved)) == RecordStatus::Ok &&
                   saved < kPlaceCount
               ? saved
               : defaultPlaceFor(host.systemSettings().timezone);
  service_.setPlace(place_);
  service_.attach(host.http());
  host.attachTopic(topic_);
  host.addJob(service_, 3000);
}

void WeatherApp::onStart(AppContext &) {}

void WeatherApp::onTopic(uint16_t topicId, AppContext &ctx) {
  if (topicId == topics::kWeather || topicId == stella::topics::kNetwork) main_.refresh(ctx);
}

void WeatherApp::onRoute(const char *route, AppContext &ctx) {
  if (route && strcmp(route, "places") == 0) ctx.push(places_);
}

void WeatherApp::choosePlace(uint8_t place, AppContext &ctx) {
  if (place == place_) return;
  place_ = place;
  ctx.saveAppRecord("place", kPlaceVersion, &place_, sizeof(place_));
  service_.setPlace(place_);
  ctx.requestJob(service_);
}

// ------------------------------------------------------------------ main screen

void WeatherApp::MainScreen::layout(const Rect &content, const Theme &t) {
  dayCards_ = t.density == Density::Regular ? 4 : 3;
  const int chipH = t.font.caption->lineHeight + t.dp(10);
  const int dayH = t.font.caption->lineHeight * 2 + t.dp(30);
  Rect r[4];
  column(content.inset(t.m.margin, t.m.gapS),
         {flex(), px(chipH), px(dayH), px(t.font.caption->lineHeight)}, t.m.gapS, r);
  hero_ = r[0];
  const int iconBox = hero_.h < t.dp(84) ? hero_.h : t.dp(84);
  Rect h[2];
  row(hero_, {flex(), px(iconBox)}, t.m.gapM, h);
  icon_ = center(h[1], iconBox, iconBox);
  Rect text[3];
  const Font &display = *t.font.display;
  column(center(h[0], h[0].w, display.ascent + t.font.title->lineHeight + t.font.caption->lineHeight + t.dp(4)),
         {px(display.ascent + t.dp(4)), px(t.font.title->lineHeight), px(t.font.caption->lineHeight)}, 0, text);
  temp_ = text[0];
  condition_ = text[1];
  place_ = text[2];
  for (int i = 0; i < 3; ++i) chips_[i] = grid(r[1], 3, 1, i, t.m.gapS);
  for (int i = 0; i < dayCards_; ++i) days_[i] = grid(r[2], dayCards_, 1, i, t.m.gapS);
  status_ = r[3];
}

void WeatherApp::MainScreen::describeFreshness(AppContext &ctx, char *out, size_t capacity) const {
  const DataStatus &s = snap_.status;
  const NetworkStatus net = ctx.network().status();
  if (!snap_.report.valid) {
    if (!net.present) snprintf(out, capacity, "This device has no network");
    else if (!net.connected) snprintf(out, capacity, "Connect to Wi-Fi to see the weather");
    else if (s.failures) snprintf(out, capacity, "%s \xc2\xb7 retrying", s.error);
    else snprintf(out, capacity, "Looking at the sky\xe2\x80\xa6");
    return;
  }
  const uint32_t minutes = elapsedMs(ctx.millis(), s.updatedMs) / 60000u;
  char age[24];
  if (minutes < 1) snprintf(age, sizeof(age), "just now");
  else if (minutes < 60) snprintf(age, sizeof(age), "%lu min ago", (unsigned long)minutes);
  else snprintf(age, sizeof(age), "%lu h ago", (unsigned long)(minutes / 60));
  if (s.freshness(ctx.millis(), WeatherService::kFreshForMs) == Freshness::Fresh) {
    snprintf(out, capacity, "Updated %s", age);
  } else if (!net.connected) {
    snprintf(out, capacity, "Offline \xc2\xb7 from %s", age);
  } else {
    snprintf(out, capacity, "From %s \xc2\xb7 %s", age, s.lastOk ? "refreshing" : s.error);
  }
}

void WeatherApp::MainScreen::refresh(AppContext &ctx) {
  const WeatherSnapshot next = app_.topic_.get();
  char status[sizeof(statusText_)];
  snap_ = next;
  // Day names from the local calendar (forecast days start today).
  const LocalTime now = ctx.localTime();
  for (int i = 0; i < WeatherReport::kDays; ++i) {
    copyText(dayNames_[i], sizeof(dayNames_[i]),
             i == 0 ? "Today" : now.valid ? shortWeekday(now.weekday + i) : "");
  }
  describeFreshness(ctx, status, sizeof(status));
  if (strcmp(status, statusText_) != 0) copyText(statusText_, sizeof(statusText_), status);
  // A new report changes most of the screen; repaint the content area.
  if (isVisible()) ctx.invalidate(ctx.contentRect());
}

void WeatherApp::MainScreen::onShow(AppContext &ctx) { refresh(ctx); }

uint32_t WeatherApp::MainScreen::onTick(AppContext &ctx) {
  char status[sizeof(statusText_)];
  describeFreshness(ctx, status, sizeof(status));
  if (strcmp(status, statusText_) != 0) {
    copyText(statusText_, sizeof(statusText_), status);
    ctx.invalidate(status_);
  }
  return 30000;
}

void WeatherApp::MainScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  const WeatherReport &w = snap_.report;
  int count = 0;
  const WeatherPlace *places = weatherPlaces(count);
  char text[32];

  // Hero: big temperature, condition, place (tap to change), icon bubble.
  const int r = icon_.w / 2;
  paintBubble(c, t, icon_.centerX(), icon_.centerY(), r, 2);
  const IconFn icon = w.valid ? weatherIcon(w.code, w.day) : icons::sparkle;
  icon(c, center(icon_, r, r), p.accent);
  if (w.valid) snprintf(text, sizeof(text), "%d\xc2\xb0", w.temperatureC);
  else snprintf(text, sizeof(text), "--\xc2\xb0");
  c.drawText(*t.font.display, text, temp_.x, temp_.y + t.font.display->ascent, w.valid ? p.text : p.textMuted);
  c.drawTextIn(*t.font.title, w.valid ? weatherLabel(w.code) : "Weather", condition_, p.text);
  Rect placeRow = place_;
  if (placePressed_) c.fillRoundRect(placeRow.inset(-t.dp(4), 0), placeRow.h / 2, p.accent, 36);
  const int iconSize = t.dp(10);
  c.drawTextIn(*t.font.caption, places[snap_.place < count ? snap_.place : 0].name,
               Rect(placeRow.x, placeRow.y, placeRow.w - iconSize - t.dp(4), placeRow.h), p.textMuted);
  const int nameW = Canvas::textWidth(*t.font.caption, places[snap_.place < count ? snap_.place : 0].name);
  icons::chevronRight(c, leading(Rect(placeRow.x + nameW + t.dp(2), placeRow.y, iconSize, placeRow.h), iconSize),
                      p.textMuted);

  // Detail chips.
  char chip[3][24];
  if (w.valid) {
    snprintf(chip[0], 24, "Feels %d\xc2\xb0", w.feelsLikeC);
    snprintf(chip[1], 24, "Humidity %u%%", (unsigned)w.humidity);
    snprintf(chip[2], 24, "Wind %u km/h", (unsigned)w.windKmh);
  } else {
    for (auto &s : chip) snprintf(s, 24, "\xe2\x80\x93");
  }
  for (int i = 0; i < 3; ++i) {
    paintGlass(c, t, chips_[i], chips_[i].h / 2, Elevation::Flat);
    c.drawTextIn(*t.font.caption, chip[i], chips_[i].inset(t.dp(6), 0), p.textMuted, Align::Center);
  }

  // Forecast cards.
  for (int i = 0; i < dayCards_; ++i) {
    const Rect card = days_[i];
    if (!c.visible(card.inset(-glassOutset(t)))) continue;
    paintGlass(c, t, card, t.m.radiusControl);
    Rect parts[3];
    column(card.inset(t.dp(4), t.dp(5)), {px(t.font.caption->lineHeight), flex(), px(t.font.caption->lineHeight)}, 0,
           parts);
    const bool has = w.valid && i < w.dayCount;
    c.drawTextIn(*t.font.caption, has ? dayNames_[i] : "", parts[0], p.textMuted, Align::Center);
    if (has) {
      const int s = parts[1].h < t.dp(24) ? parts[1].h : t.dp(24);
      weatherIcon(w.days[i].code, true)(c, center(parts[1], s, s), p.accent);
      snprintf(text, sizeof(text), "%d\xc2\xb0  %d\xc2\xb0", w.days[i].maxC, w.days[i].minC);
      c.drawTextIn(*t.font.caption, text, parts[2], p.text, Align::Center);
    }
  }
  c.drawTextIn(*t.font.caption, statusText_, status_, p.textMuted, Align::Center);
}

bool WeatherApp::MainScreen::onEvent(const Event &e, AppContext &ctx) {
  const Rect hit = Rect(place_.x - 4, place_.y - 6, place_.w / 2 + 8, place_.h + 12);
  if (trackPress(hit, e, placePressed_, ctx, place_.inset(-ctx.theme().dp(6), 0))) {
    ctx.push(app_.places_);
    return true;
  }
  if (e.type == EventType::LongPress && hero_.contains(e.x, e.y)) {
    ctx.requestJob(app_.service_);
    ctx.toast("Refreshing weather");
    return true;
  }
  return false;
}

// ------------------------------------------------------------------ place picker

void WeatherApp::PlaceScreen::layout(const Rect &content, const Theme &t) {
  card_ = content.inset(t.m.margin, t.m.gapS);
  list_.bounds = card_.inset(t.dp(2), t.dp(4));
  list_.rowHeight = t.dp(30);
}

void WeatherApp::PlaceScreen::onShow(AppContext &) {
  int count = 0;
  weatherPlaces(count);
  list_.count = count;
  list_.scrollTo(app_.place_);
}

void WeatherApp::PlaceScreen::drawPlace(Canvas &c, const Theme &t, int index, const Rect &row, bool pressed,
                                        void *self) {
  const PlaceScreen &screen = *static_cast<const PlaceScreen *>(self);
  int count = 0;
  const WeatherPlace *places = weatherPlaces(count);
  if (pressed) c.fillRoundRect(row.inset(t.dp(4), 1), t.m.radiusControl, t.color.accent, 36);
  const Rect inner = row.inset(t.m.padding, 0);
  const bool selected = index == screen.app_.place_;
  c.drawTextIn(*t.font.body, places[index].name, inner, selected ? t.color.accent : t.color.text);
  if (selected) icons::check(c, trailing(inner, t.dp(14)), t.color.accent);
}

void WeatherApp::PlaceScreen::draw(Canvas &c, const Theme &t) {
  paintGlass(c, t, card_, t.m.radiusCard);
  list_.draw(c, t, drawPlace, this);
}

bool WeatherApp::PlaceScreen::onEvent(const Event &e, AppContext &ctx) {
  const int picked = list_.handle(e, ctx);
  if (picked < 0) return true;
  app_.choosePlace((uint8_t)picked, ctx);
  ctx.pop();
  return true;
}

}  // namespace apps
}  // namespace stella
