#include "weather_app.h"

#include "../core/solar.h"

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

RainOutlook rainOutlook(const WeatherReport &w, int64_t nowEpoch) {
  RainOutlook out;
  if (!w.valid || w.hourCount == 0) return out;
  out.kind = RainOutlook::Dry;
  int possible = -1;
  for (int i = 0; i < w.hourCount; ++i) {
    const WeatherHour &h = w.hours[i];
    if (h.time + 3600 <= nowEpoch) continue;  // an hour that is over
    if (h.rainChance > out.chance && out.kind == RainOutlook::Dry) out.chance = h.rainChance;
    if (h.rainChance >= 50) {
      out.kind = RainOutlook::Likely;
      out.at = h.time;
      out.now = h.time <= nowEpoch;
      out.chance = h.rainChance;
      return out;
    }
    if (h.rainChance >= 30 && possible < 0) possible = i;
  }
  if (possible >= 0) {
    out.kind = RainOutlook::Possible;
    out.at = w.hours[possible].time;
    out.now = w.hours[possible].time <= nowEpoch;
    out.chance = w.hours[possible].rainChance;
  }
  return out;
}

// "3 pm" or "15:00" for the hour starting at `epoch`, in the clock's style.
static void hourWords(AppContext &ctx, int64_t epoch, char *out, size_t capacity) {
  const LocalTime lt = ctx.localTimeAt(epoch);
  if (ctx.settings().clock24h) snprintf(out, capacity, "%02d:00", lt.hour);
  else snprintf(out, capacity, "%d %s", lt.hour % 12 == 0 ? 12 : lt.hour % 12, lt.hour < 12 ? "am" : "pm");
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

int WeatherService::buildUrl(char *out, size_t capacity, uint8_t place) {
  if (place >= kPlaceCount) place = 0;
  const int n = snprintf(out, capacity,
                         "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
                         "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,is_day,wind_speed_10m"
                         "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
                         "&hourly=temperature_2m,precipitation_probability,weather_code&forecast_hours=%d"
                         "&timezone=auto&forecast_days=%d&timeformat=unixtime",
                         kPlaces[place].latitude, kPlaces[place].longitude, WeatherReport::kHours, WeatherReport::kDays);
  return n > 0 && (size_t)n < capacity ? n : -1;
}

JobResult WeatherService::run(uint32_t nowMs) {
  WeatherSnapshot snap = topic_.get();
  const uint8_t place = place_.load() < kPlaceCount ? place_.load() : 0;
  snap.place = place;
  if (!http_ || !http_->available()) return JobResult::Ok;
  char url[kUrlBytes];
  if (buildUrl(url, sizeof(url), place) < 0) {  // a bug, not a network problem: say so, send nothing
    snap.status.failed(nowMs, "Weather request too long");
    topic_.publish(snap);
    return JobResult::Failed;
  }
  parsing_ = WeatherReport{};
  parsing_.place = place;
  seenTemperature_ = seenCode_ = false;
  FetchLimits limits;
  limits.maxBytes = 8 * 1024;  // a forecast with its hours is ~2.5 KB
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
  } else if (i >= 0 && i < WeatherReport::kHours && path.is("hourly.time[]")) {
    parsing_.hours[i].time = (int64_t)v.number;
  } else if (i >= 0 && i < WeatherReport::kHours && path.is("hourly.temperature_2m[]")) {
    const long t = rounded(v.number);
    parsing_.hours[i].tempC = (int8_t)(t < -100 ? -100 : t > 100 ? 100 : t);
    if (i + 1 > parsing_.hourCount) parsing_.hourCount = (uint8_t)(i + 1);
  } else if (i >= 0 && i < WeatherReport::kHours && path.is("hourly.precipitation_probability[]")) {
    parsing_.hours[i].rainChance = (uint8_t)(v.number < 0 ? 0 : v.number > 100 ? 100 : v.number);
  } else if (i >= 0 && i < WeatherReport::kHours && path.is("hourly.weather_code[]")) {
    parsing_.hours[i].code = rounded(v.number);
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

WeatherApp::WeatherApp() : main_(*this), hourly_(*this), places_(*this) {}

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
  if (topicId == topics::kWeather && hourly_.isVisible()) hourly_.refresh(ctx);
  if (topicId == topics::kWeather) publishGlance(ctx);
}

// A recent observation is offered to the bedside clock; old data is withdrawn
// rather than shown as if it were current.
bool WeatherApp::publishGlance(AppContext &ctx) {
  constexpr uint32_t kGlanceMaxAgeMs = 3u * 60u * 60u * 1000u;
  const WeatherSnapshot s = topic_.get();
  if (!s.report.valid || elapsedMs(ctx.millis(), s.status.updatedMs) > kGlanceMaxAgeMs) {
    ctx.setGlance(nullptr, "");
    return false;
  }
  char text[32];
  snprintf(text, sizeof(text), "%d\xc2\xb0 %s", displayTemperature(s.report.temperatureC, ctx.settings().fahrenheit),
           weatherLabel(s.report.code));
  ctx.setGlance(weatherIcon(s.report.code, s.report.day), text);
  return true;
}

// Apps that were never opened get no topic notifications, so the glance is
// also published from here: soon while there is nothing yet, then every 10 min
// (which also withdraws data that has grown too old).
uint32_t WeatherApp::onWake(AppContext &ctx) {
  publishSun(ctx);
  return publishGlance(ctx) ? 10u * 60u * 1000u : 30u * 1000u;
}

// Today's sunrise and sunset at the chosen place, to the shell: it needs no
// forecast (only the place and the date), so this works before the first fetch.
void WeatherApp::publishSun(AppContext &ctx) {
  const LocalTime now = ctx.localTime();
  int count = 0;
  const WeatherPlace *places = weatherPlaces(count);
  if (!now.valid || place_ >= count) {
    ctx.setSun(0, 0, 0);
    return;
  }
  const int64_t midnight = now.epoch - (now.hour * 3600 + now.minute * 60 + now.second);
  const SunTimes sun = sunTimes(midnight + 12 * 3600, places[place_].latitude, places[place_].longitude);
  ctx.setSun(sun.valid ? sun.sunrise : 0, sun.valid ? sun.sunset : 0, sun.polarDay ? 1 : sun.polarNight ? 2 : 0);
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
  publishSun(ctx);  // a new place has its own sun
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
    // Open-Meteo data is CC BY 4.0: attribution is required.
    snprintf(out, capacity, "Updated %s \xc2\xb7 Open-Meteo", age);
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
  fahrenheit_ = ctx.settings().fahrenheit;
  // Day names from the local calendar (forecast days start today).
  const LocalTime now = ctx.localTime();
  for (int i = 0; i < WeatherReport::kDays; ++i) {
    copyText(dayNames_[i], sizeof(dayNames_[i]),
             i == 0 ? "Today" : now.valid ? shortWeekday(now.weekday + i) : "");
  }
  describeFreshness(ctx, status, sizeof(status));
  if (strcmp(status, statusText_) != 0) copyText(statusText_, sizeof(statusText_), status);
  updateSun(ctx);
  // The middle chip: will it rain soon? (Tapping it opens the next hours.)
  const RainOutlook rain = rainOutlook(snap_.report, ctx.localTime().valid ? ctx.localTime().epoch : 0);
  char hour[12];
  if (rain.kind != RainOutlook::Unknown && rain.kind != RainOutlook::Dry) hourWords(ctx, rain.at, hour, sizeof(hour));
  if (rain.kind == RainOutlook::Unknown) snprintf(rainChip_, sizeof(rainChip_), "Next hours");
  else if (rain.kind == RainOutlook::Dry) snprintf(rainChip_, sizeof(rainChip_), "Dry for %d h", (int)snap_.report.hourCount);
  else if (rain.now) snprintf(rainChip_, sizeof(rainChip_), rain.kind == RainOutlook::Likely ? "Rain now" : "Showers now");
  else snprintf(rainChip_, sizeof(rainChip_), rain.kind == RainOutlook::Likely ? "Rain ~%s" : "Showers ~%s", hour);
  // A new report changes most of the screen; repaint the content area.
  if (isVisible()) ctx.invalidate(ctx.contentRect());
}

// Today's sunrise and sunset at the chosen place, from the date alone.
void WeatherApp::MainScreen::updateSun(AppContext &ctx) {
  sunRise_[0] = sunSet_[0] = '\0';
  sunKind_ = 0;
  const WeatherReport &w = snap_.report;
  int count = 0;
  const WeatherPlace *places = weatherPlaces(count);
  if (!w.valid || w.dayCount == 0 || w.days[0].date <= 0 || snap_.place >= count) return;
  const WeatherPlace &place = places[snap_.place];
  const SunTimes sun = sunTimes(w.days[0].date + 12 * 3600, place.latitude, place.longitude);
  if (!sun.valid) {
    sunKind_ = sun.polarDay ? 1 : 2;
    return;
  }
  const bool h24 = ctx.settings().clock24h;
  const int64_t moments[2] = {sun.sunrise, sun.sunset};
  char *out[2] = {sunRise_, sunSet_};
  for (int i = 0; i < 2; ++i) {
    const LocalTime lt = ctx.localTimeAt(moments[i]);
    const int hour = h24 ? lt.hour : (lt.hour % 12 == 0 ? 12 : lt.hour % 12);
    snprintf(out[i], 8, h24 ? "%02d:%02d" : "%d:%02d", hour, lt.minute);
  }
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
  if (w.valid) snprintf(text, sizeof(text), "%d\xc2\xb0", displayTemperature(w.temperatureC, fahrenheit_));
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
  // Sunrise and sunset at the right end of the same line, when they fit: the
  // sun for the day starting and the moon for it ending.
  if (sunRise_[0] || sunKind_) {
    const int small = t.dp(10), gapSmall = t.dp(3), between = t.dp(8);
    const char *polar = sunKind_ == 1 ? "Sun stays up" : "Sun stays down";
    const int riseW = sunKind_ ? Canvas::textWidth(*t.font.caption, polar) : Canvas::textWidth(*t.font.caption, sunRise_);
    const int setW = sunKind_ ? 0 : Canvas::textWidth(*t.font.caption, sunSet_);
    const int total = sunKind_ ? riseW : small + gapSmall + riseW + between + small + gapSmall + setW;
    const int room = placeRow.w - nameW - iconSize - t.dp(12);
    if (total <= room) {
      int x = placeRow.right() - total;
      if (sunKind_) {
        c.drawTextIn(*t.font.caption, polar, Rect(x, placeRow.y, riseW, placeRow.h), p.textMuted);
      } else {
        icons::sun(c, leading(Rect(x, placeRow.y, small, placeRow.h), small), p.accent);
        x += small + gapSmall;
        c.drawTextIn(*t.font.caption, sunRise_, Rect(x, placeRow.y, riseW, placeRow.h), p.textMuted);
        x += riseW + between;
        icons::moon(c, leading(Rect(x, placeRow.y, small, placeRow.h), small), p.accent);
        x += small + gapSmall;
        c.drawTextIn(*t.font.caption, sunSet_, Rect(x, placeRow.y, setW, placeRow.h), p.textMuted);
      }
    }
  }

  // Detail chips.
  char chip[3][24];
  if (w.valid) {
    snprintf(chip[0], 24, "Feels %d\xc2\xb0", displayTemperature(w.feelsLikeC, fahrenheit_));
    snprintf(chip[1], 24, "%s", rainChip_);
    snprintf(chip[2], 24, "Wind %u km/h", (unsigned)w.windKmh);
  } else {
    for (auto &s : chip) snprintf(s, 24, "\xe2\x80\x93");
  }
  for (int i = 0; i < 3; ++i) {
    paintGlass(c, t, chips_[i], chips_[i].h / 2, Elevation::Flat);
    if (i == 1) {  // the tappable one: it leads to the next hours
      if (rainPressed_) c.fillRoundRect(chips_[i], chips_[i].h / 2, p.accent, 46);
      const int arrow = t.dp(8);
      c.drawTextIn(*t.font.caption, chip[i], chips_[i].inset(t.dp(6), 0).inset(0, 0), p.accent, Align::Center);
      icons::chevronRight(c, trailing(Rect(chips_[i].x, chips_[i].y, chips_[i].w - t.dp(6), chips_[i].h), arrow), p.textMuted);
      continue;
    }
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
      snprintf(text, sizeof(text), "%d\xc2\xb0  %d\xc2\xb0", displayTemperature(w.days[i].maxC, fahrenheit_),
               displayTemperature(w.days[i].minC, fahrenheit_));
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
  if (snap_.report.valid && trackPress(chips_[1], e, rainPressed_, ctx, chips_[1].inset(-ctx.theme().dp(6)))) {
    ctx.push(app_.hourly_);
    return true;
  }
  if (e.type == EventType::LongPress && hero_.contains(e.x, e.y)) {
    ctx.requestJob(app_.service_);
    ctx.toast("Refreshing weather");
    return true;
  }
  return false;
}

// ------------------------------------------------------------------ next hours

void WeatherApp::HourlyScreen::layout(const Rect &content, const Theme &t) {
  Rect r[3];
  column(content.inset(t.m.margin, t.m.gapS),
         {px(t.font.body->lineHeight * 2 + t.dp(12)), flex(), px(t.font.caption->lineHeight)}, t.m.gapS, r);
  summary_ = r[0];
  chart_ = r[1];
  details_ = r[2];
}

void WeatherApp::HourlyScreen::onShow(AppContext &ctx) { refresh(ctx); }

void WeatherApp::HourlyScreen::refresh(AppContext &ctx) {
  report_ = app_.topic_.get().report;
  fahrenheit_ = ctx.settings().fahrenheit;
  const LocalTime now = ctx.localTime();
  const RainOutlook rain = rainOutlook(report_, now.valid ? now.epoch : 0);
  char hour[12];
  if (rain.kind == RainOutlook::Likely || rain.kind == RainOutlook::Possible) hourWords(ctx, rain.at, hour, sizeof(hour));
  switch (rain.kind) {
    case RainOutlook::Unknown: snprintf(summaryText_, sizeof(summaryText_), "No hourly forecast yet"); break;
    case RainOutlook::Dry:
      snprintf(summaryText_, sizeof(summaryText_), "No rain expected in the next %d hours", (int)report_.hourCount);
      break;
    case RainOutlook::Possible:
      if (rain.now) snprintf(summaryText_, sizeof(summaryText_), "A chance of showers now (%u%%)", (unsigned)rain.chance);
      else snprintf(summaryText_, sizeof(summaryText_), "Showers possible from about %s (%u%%)", hour, (unsigned)rain.chance);
      break;
    case RainOutlook::Likely:
      if (rain.now) snprintf(summaryText_, sizeof(summaryText_), "Rain is likely now (%u%%)", (unsigned)rain.chance);
      else snprintf(summaryText_, sizeof(summaryText_), "Rain likely from about %s (%u%%)", hour, (unsigned)rain.chance);
      break;
  }
  if (report_.valid) snprintf(detailText_, sizeof(detailText_), "Humidity %u%% \xc2\xb7 Wind %u km/h", (unsigned)report_.humidity, (unsigned)report_.windKmh);
  else detailText_[0] = '\0';
  const bool h24 = ctx.settings().clock24h;
  for (int i = 0; i < report_.hourCount && i < WeatherReport::kHours; ++i) {
    const LocalTime lt = ctx.localTimeAt(report_.hours[i].time);
    if (h24) snprintf(labels_[i], sizeof(labels_[i]), "%02d", lt.hour);
    else snprintf(labels_[i], sizeof(labels_[i]), "%d%c", lt.hour % 12 == 0 ? 12 : lt.hour % 12, lt.hour < 12 ? 'a' : 'p');
  }
  if (isVisible()) ctx.invalidate(ctx.contentRect());
}

void WeatherApp::HourlyScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  paintGlass(c, t, summary_, t.m.radiusCard);
  c.drawWrapped(*t.font.body, summaryText_, summary_.inset(t.m.padding, t.dp(6)), p.text, 2, Align::Center);
  paintGlass(c, t, chart_, t.m.radiusCard);
  const int n = report_.hourCount;
  if (n > 0) {
    const Rect inner = chart_.inset(t.dp(6), t.dp(8));
    const int colW = inner.w / WeatherReport::kHours;
    const int x0 = inner.x + (inner.w - colW * n) / 2;
    const int lineH = t.font.caption->lineHeight;
    const int barTop = inner.y + lineH + t.dp(4), barBottom = inner.bottom() - lineH - t.dp(4);
    const int barH = barBottom > barTop ? barBottom - barTop : 0;
    const int barW = colW > t.dp(8) ? t.dp(8) : colW - 2;
    for (int i = 0; i < n; ++i) {
      const WeatherHour &h = report_.hours[i];
      const Rect col(x0 + i * colW, inner.y, colW, inner.h);
      char temp[8];
      snprintf(temp, sizeof(temp), "%d\xc2\xb0", displayTemperature(h.tempC, fahrenheit_));
      c.drawTextIn(*t.font.caption, temp, Rect(col.x - 2, inner.y, col.w + 4, lineH), p.text, Align::Center);
      const Rect track(col.centerX() - barW / 2, barTop, barW, barH);
      c.fillRoundRect(track, barW / 2, p.line, (uint8_t)(p.lineAlpha * 2));
      const int fill = barH * h.rainChance / 100;
      if (fill > 0) c.fillRoundRect(Rect(track.x, track.bottom() - (fill < barW ? barW : fill), barW, fill < barW ? barW : fill), barW / 2, p.accent);
      c.drawTextIn(*t.font.caption, labels_[i], Rect(col.x - 2, inner.bottom() - lineH, col.w + 4, lineH), p.textMuted, Align::Center);
    }
  }
  c.drawTextIn(*t.font.caption, detailText_, details_, p.textMuted, Align::Center);
}

bool WeatherApp::HourlyScreen::onEvent(const Event &, AppContext &) { return false; }

// ------------------------------------------------------------------ place picker

void WeatherApp::PlaceScreen::layout(const Rect &content, const Theme &t) {
  card_ = content.inset(t.m.margin, t.m.gapS);
  list_.bounds = card_.inset(t.dp(2), t.dp(4));
  list_.rowHeight = t.dp(30);
  list_.buttonWidth = t.dp(32);
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
