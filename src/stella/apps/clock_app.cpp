#include "clock_app.h"

#include <stdio.h>
#include <string.h>

#include "../ui/layout.h"
#include "../ui/materials.h"
#include "common.h"

namespace stella {
namespace apps {

using namespace layout;

ClockApp::ClockApp() : screen_(*this) {}

const AppInfo &ClockApp::info() const {
  static const AppInfo kInfo{"clock", "Clock", icons::clock, 2, kAppIdleFace};
  return kInfo;
}

void ClockApp::onRoute(const char *route, AppContext &ctx) {
  if (route && strcmp(route, "bedside") == 0 && !bedsideScreen_.isVisible()) ctx.push(bedsideScreen_);
  if (route && strcmp(route, "dashboard") == 0 && !dashboardScreen_.isVisible()) ctx.push(dashboardScreen_);
}

void ClockApp::ClockScreen::layout(const Rect &content, const Theme &t) {
  const Font &display = *t.font.display;
  Rect parts[6];
  column(content.inset(t.m.margin, t.m.gapS),
         {flex(), px(display.ascent + t.dp(4)), px(t.font.title->lineHeight), px(t.m.control), px(t.m.control),
          flex()},
         t.m.gapS, parts);
  time_ = parts[1];
  date_ = parts[2];
  chip_ = parts[3];
  bedside_.bounds = center(parts[4], t.dp(132), t.m.control - t.dp(2));
  bedside_.label = "Night clock";
  bedside_.icon = icons::moon;
  bedside_.style = ButtonStyle::Quiet;
}

void ClockApp::ClockScreen::refresh(AppContext &ctx) {
  const LocalTime now = ctx.localTime();
  char timeText[sizeof(timeText_)], suffix[sizeof(suffix_)] = "", date[sizeof(dateText_)], chip[sizeof(chipText_)];
  ctx.formatTime(now, timeText, sizeof(timeText));
  if (now.valid && !ctx.settings().clock24h) snprintf(suffix, sizeof(suffix), "%s", now.hour < 12 ? "AM" : "PM");
  formatLongDate(now, date, sizeof(date), ctx.settings().dateOrder);
  if (now.valid) {
    snprintf(chip, sizeof(chip), "%s \xc2\xb7 %s", moonPhase(now.epoch).name, phaseName(ctx.theme().phase));
  } else {
    snprintf(chip, sizeof(chip), "Connect to the network to set the time");
  }
  if (strcmp(timeText, timeText_) || strcmp(suffix, suffix_)) {
    memcpy(timeText_, timeText, sizeof(timeText));
    memcpy(suffix_, suffix, sizeof(suffix));
    ctx.invalidate(time_);
  }
  if (strcmp(date, dateText_)) {
    memcpy(dateText_, date, sizeof(date));
    ctx.invalidate(date_);
  }
  if (strcmp(chip, chipText_)) {
    memcpy(chipText_, chip, sizeof(chip));
    ctx.invalidate(chip_.inset(-glassOutset(ctx.theme())));
  }
  timeKnown_ = now.valid;
}

void ClockApp::ClockScreen::onShow(AppContext &ctx) { refresh(ctx); }

uint32_t ClockApp::ClockScreen::onTick(AppContext &ctx) {
  refresh(ctx);
  // Wake just after the next minute boundary rather than every second.
  const LocalTime now = ctx.localTime();
  return now.valid ? (uint32_t)(60 - now.second) * 1000u + 50u : 1000u;
}

void ClockApp::ClockScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  const Font &display = *t.font.display;
  const int baseline = time_.y + display.ascent;
  const int timeW = Canvas::textWidth(display, timeText_);
  const int suffixW = suffix_[0] ? Canvas::textWidth(*t.font.title, suffix_) + t.dp(6) : 0;
  const int x = time_.centerX() - (timeW + suffixW) / 2;
  c.drawText(display, timeText_, x, baseline, timeKnown_ ? p.text : p.textMuted);
  if (suffix_[0]) c.drawText(*t.font.title, suffix_, x + timeW + t.dp(6), baseline, p.textMuted);

  c.drawTextIn(*t.font.title, dateText_, date_, p.textMuted, Align::Center);

  const int iconSize = t.dp(14);
  const int textW = Canvas::textWidth(*t.font.caption, chipText_);
  int w = textW + iconSize + t.dp(8) + 2 * t.m.padding;
  if (w > chip_.w) w = chip_.w;
  const Rect pill = center(chip_, w, chip_.h);
  paintGlass(c, t, pill, pill.h / 2);
  Rect inner = pill.inset(t.m.padding, 0);
  icons::moon(c, leading(inner, iconSize), p.accent);
  inner = Rect(inner.x + iconSize + t.dp(8), inner.y, inner.w - iconSize - t.dp(8), inner.h);
  c.drawTextIn(*t.font.caption, chipText_, inner, p.text);
  bedside_.draw(c, t);
}

bool ClockApp::ClockScreen::onEvent(const Event &e, AppContext &ctx) {
  if (bedside_.handle(e, ctx)) {
    app_.onRoute("bedside", ctx);
    return true;
  }
  return false;
}

// ------------------------------------------------------------------ bedside

namespace {

// Star positions as 1/256 of the display (edge-weighted, like Nightstand's).
constexpr uint8_t kStarX[10] = {59, 196, 14, 242, 37, 225, 18, 240, 75, 180};
constexpr uint8_t kStarY[10] = {48, 48, 98, 97, 151, 153, 211, 215, 234, 230};

Point at256(const Rect &area, int fx, int fy) {
  return Point{(int16_t)(area.x + area.w * fx / 256), (int16_t)(area.y + area.h * fy / 256)};
}

}  // namespace

int bedsideFrameAt(uint32_t ms) {
  const uint32_t phase = ms % 16000u;
  return (int)((phase / 4000u) % 2u) * 2 + (phase >= 15000u ? 1 : 0);
}

uint32_t bedsideMsToNextFrame(uint32_t ms) {
  const uint32_t phase = ms % 16000u;
  static const uint32_t kEdges[] = {4000, 8000, 12000, 15000, 16000};
  for (uint32_t edge : kEdges)
    if (edge > phase) return edge - phase + 5;
  return 5;
}

int ClockApp::BedsideScreen::starLevel(int star, int frame) { return ((star + frame) & 3) == 0 ? 1 : 0; }

Rect ClockApp::BedsideScreen::starBox(int star) const {
  return Rect(stars_[star].x - 1, stars_[star].y - 1, starSize_ + 2, starSize_ + 2);
}

void ClockApp::BedsideScreen::layout(const Rect &area, const Theme &t) {
  area_ = area;
  const Rect inner = area.inset(t.m.margin, t.m.margin);
  // The largest numerals in which "88:88" and AM/PM still fit the width.
  int digit = 0;
  for (char d = '0'; d <= '9'; ++d) {
    const char one[2] = {d, 0};
    const int w = Canvas::textWidth(*t.font.bedside, one);
    if (w > digit) digit = w;
  }
  const int suffix = t.dp(6) + (Canvas::textWidth(*t.font.title, "AM") > Canvas::textWidth(*t.font.title, "PM")
                                    ? Canvas::textWidth(*t.font.title, "AM")
                                    : Canvas::textWidth(*t.font.title, "PM"));
  clockFont_ = 4 * digit + Canvas::textWidth(*t.font.bedside, ":") + suffix <= inner.w ? t.font.bedside : t.font.display;

  Rect r[9];
  column(inner,
         {px(t.font.body->lineHeight), flex(), px(clockFont_->ascent + t.dp(6)), px(t.dp(14)),
          px(t.font.title->lineHeight), px(t.font.body->lineHeight), px(t.font.caption->lineHeight), flex(),
          px(t.font.caption->lineHeight)},
         t.m.gapS, r);
  date_ = r[0];
  clock_ = r[2];
  divider_ = r[3];
  glanceRow_ = r[4];
  alarmRow_ = r[5];
  sunRow_ = r[6];
  footer_ = r[8];

  moonR_ = t.dp(12);
  moon_ = Point{(int16_t)(inner.x + moonR_), (int16_t)(inner.y + moonR_ + t.dp(1))};
  sparkleBig_ = t.dp(14);
  sparkleSmall_ = t.dp(9);
  bigSparkle_ = Point{(int16_t)(inner.right() - t.dp(14)), (int16_t)(inner.y + t.dp(12))};
  smallSparkle_ = Point{(int16_t)(bigSparkle_.x + t.dp(10)), (int16_t)(bigSparkle_.y + t.dp(18))};
  planet_ = at256(area, 244, 131);
  starSize_ = t.density == Density::Regular ? 2 : 1;
  for (int i = 0; i < kStars; ++i) stars_[i] = at256(area, kStarX[i], kStarY[i]);
}

void ClockApp::BedsideScreen::refresh(AppContext &ctx) {
  const LocalTime now = ctx.localTime();
  const SystemStatus status = ctx.status();
  char timeText[sizeof(timeText_)], suffix[sizeof(suffix_)] = "", date[sizeof(dateText_)], alarm[sizeof(alarmText_)] = "";
  ctx.formatTime(now, timeText, sizeof(timeText));
  if (now.valid && !ctx.settings().clock24h) snprintf(suffix, sizeof(suffix), "%s", now.hour < 12 ? "AM" : "PM");
  formatLongDate(now, date, sizeof(date), ctx.settings().dateOrder);
  bool hasAlarm = false;
  if (now.valid && status.nextAlarm > now.epoch) {
    const LocalTime at = ctx.localTimeAt(status.nextAlarm);
    char when[12], wait[24];
    ctx.formatTime(at, when, sizeof(when));
    describeWait((int)((status.nextAlarm - now.epoch + 59) / 60), wait, sizeof(wait));
    snprintf(alarm, sizeof(alarm), "%s%s \xc2\xb7 in %s", when,
             ctx.settings().clock24h ? "" : at.hour < 12 ? " AM" : " PM", wait);
    hasAlarm = true;
  }
  if (strcmp(timeText, timeText_) || strcmp(suffix, suffix_)) {
    memcpy(timeText_, timeText, sizeof(timeText));
    memcpy(suffix_, suffix, sizeof(suffix));
    ctx.invalidate(clock_);
  }
  if (strcmp(date, dateText_)) {
    memcpy(dateText_, date, sizeof(date));
    ctx.invalidate(date_);
  }
  if (strcmp(alarm, alarmText_) || hasAlarm != hasAlarm_) {
    memcpy(alarmText_, alarm, sizeof(alarm));
    hasAlarm_ = hasAlarm;
    ctx.invalidate(alarmRow_);
  }
  char sun[sizeof(sunText_)];
  describeNextSun(ctx, sun, sizeof(sun));
  if (strcmp(sun, sunText_)) {
    memcpy(sunText_, sun, sizeof(sun));
    ctx.invalidate(sunRow_);
  }
  if (strcmp(status.glance.text, glance_.text) || status.glance.icon != glance_.icon) {
    glance_ = status.glance;
    ctx.invalidate(glanceRow_);
  }
  timeKnown_ = now.valid;
}

// Repaint only what the new frame changes.
void ClockApp::BedsideScreen::animate(AppContext &ctx, int frame) {
  if (frame == frame_) return;
  const int old = frame_;
  const int r = moonR_;
  ctx.invalidate(Rect(moon_.x - r - 2, moon_.y - r - 2, 2 * r + 5, 2 * r + 6));
  if ((old ^ frame) & 2) {
    ctx.invalidate(Rect(bigSparkle_.x - sparkleBig_ / 2 - 1, bigSparkle_.y - sparkleBig_ / 2 - 1, sparkleBig_ + 2,
                        sparkleBig_ + 2));
  }
  ctx.invalidate(Rect(planet_.x - sparkleSmall_ - 1, planet_.y - 4, 2 * sparkleSmall_ + 3, 9));
  for (int i = 0; i < kStars; ++i)
    if (starLevel(i, old) != starLevel(i, frame)) ctx.invalidate(starBox(i));
  frame_ = frame;
}

void ClockApp::BedsideScreen::onShow(AppContext &ctx) {
  timeText_[0] = dateText_[0] = alarmText_[0] = sunText_[0] = 0;  // paint everything afresh
  glance_ = Glance{};
  frame_ = bedsideFrameAt(ctx.millis());
  refresh(ctx);
}

uint32_t ClockApp::BedsideScreen::onTick(AppContext &ctx) {
  const uint32_t ms = ctx.millis();
  animate(ctx, bedsideFrameAt(ms));
  refresh(ctx);
  const LocalTime now = ctx.localTime();
  const uint32_t minute = now.valid ? (uint32_t)(60 - now.second) * 1000u + 50u : 1000u;
  const uint32_t frame = bedsideMsToNextFrame(ms);
  return minute < frame ? minute : frame;
}

void ClockApp::BedsideScreen::drawMoon(Canvas &c, const Theme &t) const {
  const Palette &p = t.color;
  // A tiny sleepy moon: it bobs one pixel and blinks (the eye is a cut-out of
  // the crescent: a dot when open, a short line when closed).
  const int r = moonR_;
  const int cx = moon_.x, cy = moon_.y + (frame_ >> 1);
  c.fillCircle(cx, cy, r, p.bedMuted);
  c.fillCircle(cx + r * 4 / 7, cy - r * 3 / 7, r * 6 / 7, p.bedBg);
  const int ex = cx - r * 3 / 7, ey = cy + r / 7;
  if (frame_ & 1) c.fillRect(Rect(ex - r / 5 - 1, ey, r * 2 / 5 + 3, starSize_), p.bedBg);
  else c.fillRect(Rect(ex, ey, starSize_, starSize_), p.bedBg);
}

void ClockApp::BedsideScreen::drawSky(Canvas &c, const Theme &t) const {
  const Palette &p = t.color;
  const Color faint = blend(p.bedBg, p.bedStar, 150), soft = p.bedStar;
  for (int i = 0; i < kStars; ++i)
    c.fillRect(Rect(stars_[i].x, stars_[i].y, starSize_, starSize_), starLevel(i, frame_) ? soft : faint);
  const bool twinkle = (frame_ & 2) != 0;
  const int big = twinkle ? sparkleBig_ : sparkleBig_ * 3 / 4;
  icons::sparkle(c, Rect(bigSparkle_.x - big / 2, bigSparkle_.y - big / 2, big, big), twinkle ? p.bedText : p.bedStar);
  icons::sparkle(c, Rect(smallSparkle_.x - sparkleSmall_ / 2, smallSparkle_.y - sparkleSmall_ / 2, sparkleSmall_,
                         sparkleSmall_),
                 p.bedStar);
  // A ringed planet drifting a pixel.
  const int px = planet_.x - (frame_ >> 1), py = planet_.y + (frame_ & 1);
  c.fillCircle(px, py, t.dp(2), soft);
  c.hline(px - sparkleSmall_ / 2, py, sparkleSmall_ + 1, faint);
}

void ClockApp::BedsideScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  c.fill(p.bedBg);  // black: low backlight, nothing to dither
  drawSky(c, t);
  drawMoon(c, t);
  c.drawTextIn(*t.font.body, dateText_, date_, p.bedMuted, Align::Center);

  const Font &face = *clockFont_;
  const int baseline = clock_.y + face.ascent;
  const int timeW = Canvas::textWidth(face, timeText_);
  const int suffixW = suffix_[0] ? Canvas::textWidth(*t.font.title, suffix_) + t.dp(6) : 0;
  const int x = clock_.centerX() - (timeW + suffixW) / 2;
  c.drawText(face, timeText_, x, baseline, timeKnown_ ? p.bedClock : p.bedMuted);
  if (suffix_[0]) c.drawText(*t.font.title, suffix_, x + timeW + t.dp(6), baseline, p.bedMuted);

  // A thin rule with a star in the middle, under the clock.
  const int star = t.dp(10), half = divider_.w / 6;
  c.hline(divider_.centerX() - half - star, divider_.centerY(), half, p.bedStar);
  c.hline(divider_.centerX() + star + 1, divider_.centerY(), half, p.bedStar);
  icons::sparkle(c, center(divider_, star, star), p.bedText);

  if (!glance_.empty()) {
    const int icon = glance_.icon ? t.dp(20) : 0, gap = glance_.icon ? t.dp(6) : 0;
    const int w = icon + gap + Canvas::textWidth(*t.font.title, glance_.text);
    const Rect line = center(glanceRow_, w < glanceRow_.w ? w : glanceRow_.w, glanceRow_.h);
    if (glance_.icon) glance_.icon(c, leading(line, icon), p.bedText);
    c.drawTextIn(*t.font.title, glance_.text, Rect(line.x + icon + gap, line.y, line.w - icon - gap, line.h), p.bedText);
  }
  if (hasAlarm_) {
    const int icon = t.dp(13);
    const int w = icon + t.dp(6) + Canvas::textWidth(*t.font.body, alarmText_);
    const Rect line = center(alarmRow_, w < alarmRow_.w ? w : alarmRow_.w, alarmRow_.h);
    icons::bell(c, leading(line, icon), p.bedText);
    c.drawTextIn(*t.font.body, alarmText_, Rect(line.x + icon + t.dp(6), line.y, line.w - icon - t.dp(6), line.h),
                 p.bedMuted);
  }
  if (sunText_[0]) {
    const int icon = t.dp(11);
    const int w = icon + t.dp(5) + Canvas::textWidth(*t.font.caption, sunText_);
    const Rect line = center(sunRow_, w < sunRow_.w ? w : sunRow_.w, sunRow_.h);
    icons::sun(c, leading(line, icon), p.bedStar);
    c.drawTextIn(*t.font.caption, sunText_, Rect(line.x + icon + t.dp(5), line.y, line.w - icon - t.dp(5), line.h),
                 p.bedMuted);
  }
  c.drawTextIn(*t.font.caption, "Tap anywhere to return", footer_, p.bedStar, Align::Center);
}

bool ClockApp::BedsideScreen::onEvent(const Event &e, AppContext &ctx) {
  if (e.type == EventType::Tap) ctx.pop();  // any tap leaves the bedside clock
  return e.isPointer();
}

// ---------------------------------------------------------------- dashboard

void ClockApp::DashboardScreen::layout(const Rect &area, const Theme &t) {
  area_ = area;
  const Rect inner = area.inset(t.m.margin, t.m.margin);
  clockFont_ = t.font.hometime;
  Rect r[8];
  column(inner,
         {px(clockFont_->ascent + t.dp(6)), px(t.font.body->lineHeight), flex(), px(t.m.control), px(t.m.control),
          px(t.m.control), flex(), px(t.font.caption->lineHeight)},
         t.m.gapS, r);
  clock_ = r[0];
  date_ = r[1];
  for (int i = 0; i < kLines; ++i) lines_[i] = r[3 + i];
  footer_ = r[7];
}

void ClockApp::DashboardScreen::refresh(AppContext &ctx) {
  const LocalTime now = ctx.localTime();
  const SystemStatus status = ctx.status();
  char timeText[sizeof(timeText_)], suffix[sizeof(suffix_)] = "", date[sizeof(dateText_)];
  char text[kLines][48] = {};
  ctx.formatTime(now, timeText, sizeof(timeText));
  if (now.valid && !ctx.settings().clock24h) snprintf(suffix, sizeof(suffix), "%s", now.hour < 12 ? "AM" : "PM");
  formatLongDate(now, date, sizeof(date), ctx.settings().dateOrder);
  snprintf(text[0], sizeof(text[0]), "%s", status.glance.text);
  if (now.valid && status.nextAlarm > now.epoch) {
    const LocalTime at = ctx.localTimeAt(status.nextAlarm);
    char when[12], wait[24];
    ctx.formatTime(at, when, sizeof(when));
    describeWait((int)((status.nextAlarm - now.epoch + 59) / 60), wait, sizeof(wait));
    snprintf(text[1], sizeof(text[1]), "%s%s \xc2\xb7 in %s", when, ctx.settings().clock24h ? "" : at.hour < 12 ? " AM" : " PM",
             wait);
  }
  describeNextSun(ctx, text[2], sizeof(text[2]));
  if (strcmp(timeText, timeText_) || strcmp(suffix, suffix_)) {
    memcpy(timeText_, timeText, sizeof(timeText));
    memcpy(suffix_, suffix, sizeof(suffix));
    ctx.invalidate(clock_);
  }
  if (strcmp(date, dateText_)) {
    memcpy(dateText_, date, sizeof(date));
    ctx.invalidate(date_);
  }
  for (int i = 0; i < kLines; ++i) {
    if (strcmp(text[i], lineText_[i]) || (i == 0 && status.glance.icon != weatherIcon_)) {
      memcpy(lineText_[i], text[i], sizeof(text[i]));
      if (i == 0) weatherIcon_ = status.glance.icon;
      ctx.invalidate(lines_[i]);
    }
  }
  timeKnown_ = now.valid;
}

void ClockApp::DashboardScreen::onShow(AppContext &ctx) {
  timeText_[0] = dateText_[0] = 0;
  for (auto &l : lineText_) l[0] = 0;
  weatherIcon_ = nullptr;
  refresh(ctx);
}

uint32_t ClockApp::DashboardScreen::onTick(AppContext &ctx) {
  refresh(ctx);
  const LocalTime now = ctx.localTime();
  return now.valid ? (uint32_t)(60 - now.second) * 1000u + 50u : 1000u;
}

void ClockApp::DashboardScreen::draw(Canvas &c, const Theme &t) {
  const Palette &p = t.color;
  c.fill(p.bedBg);
  const Font &face = *clockFont_;
  const int baseline = clock_.y + face.ascent;
  const int timeW = Canvas::textWidth(face, timeText_);
  const int suffixW = suffix_[0] ? Canvas::textWidth(*t.font.title, suffix_) + t.dp(6) : 0;
  const int x = clock_.centerX() - (timeW + suffixW) / 2;
  c.drawText(face, timeText_, x, baseline, timeKnown_ ? p.bedClock : p.bedMuted);
  if (suffix_[0]) c.drawText(*t.font.title, suffix_, x + timeW + t.dp(6), baseline, p.bedMuted);
  c.drawTextIn(*t.font.body, dateText_, date_, p.bedMuted, Align::Center);

  for (int i = 0; i < kLines; ++i) {
    if (!lineText_[i][0]) continue;
    const Rect pill = lines_[i];
    c.fillRoundRect(pill, t.m.radiusControl, p.bedStar, 34);
    const Rect inner = pill.inset(t.m.padding, 0);
    const int icon = t.dp(i == 0 ? 20 : 14), gap = t.dp(8);
    const Rect iconBox = center(leading(inner, icon), icon, icon);
    if (i == 0 && weatherIcon_) weatherIcon_(c, iconBox, p.bedText);
    else if (i == 1) icons::bell(c, iconBox, p.bedText);
    else if (i == 2) icons::sun(c, iconBox, p.bedStar);
    const Rect text(inner.x + icon + gap, inner.y, inner.w - icon - gap, inner.h);
    c.drawTextIn(i == 0 ? *t.font.title : *t.font.body, lineText_[i], text, i == 0 ? p.bedText : p.bedMuted);
  }
  c.drawTextIn(*t.font.caption, "Tap anywhere to return", footer_, p.bedStar, Align::Center);
}

bool ClockApp::DashboardScreen::onEvent(const Event &e, AppContext &ctx) {
  if (e.type == EventType::Tap) ctx.pop();
  return e.isPointer();
}

}  // namespace apps
}  // namespace stella
