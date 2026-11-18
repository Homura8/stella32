// Stella screenshot simulator: renders every built-in screen with the
// production renderer at several display sizes, in light and dark skies.
//
//   stella_sim <out-dir>      writes <out-dir>/<W>x<H>/<NN>-<name>.ppm
//
// tools/screenshots.py converts the PPMs to PNGs under docs/screenshots and
// checks them against the committed images in CI.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <string>

#include "gift.example.h"
#include "sim_session.h"
#include "stella/app/text_entry.h"
#include "stella/core/sha256.h"
#include "stella/net/wallpaper.h"
#include "stella/ui/layout.h"

using namespace stella;
using namespace stella::host;

namespace {

int failures = 0;

void tapRect(SimSession &s, const Rect &r) { s.tap(r.centerX(), r.centerY()); }

// A pretend update server: version 0.9.0, a 1.3 MB image (the simulated
// firmware accepts the "signature" of all 0x5a bytes).
void serveUpdate(SimSession &s) {
  std::string image(1300000, 'x');
  for (size_t i = 0; i < image.size(); ++i) image[i] = (char)((i * 2654435761u) >> 24);
  uint8_t digest[32];
  Sha256::hash(image.data(), image.size(), digest);
  char hex[65];
  toHex(digest, 32, hex);
  std::string rs;
  for (int i = 0; i < 32; ++i) rs += "5a";
  const std::string manifest = std::string("{\"version\":\"0.9.0\",\"url\":\"https://updates.example/stella32.bin\",\"size\":") +
                               std::to_string(image.size()) + ",\"sha256\":\"" + hex + "\",\"sig_r\":\"" + rs +
                               "\",\"sig_s\":\"" + rs + "\"}";
  s.dev.firmware.slotSize = 1966080;  // the 1.9 MB app slot of the OTA layout
  s.dev.http.routes.insert(s.dev.http.routes.begin(), {"stella32.bin", image, 200});
  s.dev.http.routes.insert(s.dev.http.routes.begin(), {"manifest.json", manifest, 200});
}

void shot(SimSession &s, const std::string &dir, const char *name) {
  const std::string path = dir + "/" + name + ".ppm";
  if (!s.dev.display.writePpm(path)) {
    fprintf(stderr, "cannot write %s\n", path.c_str());
    ++failures;
  }
}

void capture(int width, int height, const std::string &root) {
  char sizeName[32];
  snprintf(sizeName, sizeof(sizeName), "%dx%d", width, height);
  const std::string dir = root + "/" + sizeName;
  mkdir(dir.c_str(), 0755);

  for (int variant = 0; variant < 2; ++variant) {
    const bool evening = variant == 0;
    const char *suffix = evening ? "night" : "day";
    SimSession s(width, height, evening ? kEveningEpoch : kMorningEpoch);
    char name[64];
    auto save = [&](int index, const char *screen) {
      snprintf(name, sizeof(name), "%02d-%s-%s", index, screen, suffix);
      shot(s, dir, name);
    };
    save(1, "home");
    s.os.launch("clock");
    s.run(200);
    save(2, "clock");
    s.os.launch("weather");
    s.run(200);
    save(3, "weather");
    tapRect(s, s.weather.hourlyChip());  // the next hours
    s.run(300);
    save(30, "weather-hours");
    s.os.pop();
    s.run(100);
    s.os.launch("settings");
    s.run(200);
    save(4, "settings");
    const char *pages[] = {"wifi", "display", "sound", "appearance", "time", "about"};
    for (int i = 0; i < 6; ++i) {
      const Point row = s.settingsRow(i);
      s.tap(row.x, row.y);
      s.run(600);  // Wi-Fi: let the scan finish
      save(5 + i, pages[i]);
      const Point back = s.backButton();
      s.tap(back.x, back.y);
    }
    s.os.launch("touch");
    s.run(100);
    const Rect c = s.os.contentRect();
    s.drag(c.x + c.w / 4, c.y + c.h / 3, c.x + c.w * 3 / 4, c.y + c.h / 2);
    s.os.toast("Settings saved");
    s.run(100);
    save(10, "touch-toast");

    // First run: no network saved yet; then the password keyboard.
    SimSession fresh(width, height, evening ? kEveningEpoch : kMorningEpoch, Appearance::Auto, false, false);
    auto saveFresh = [&](int index, const char *screen) {
      snprintf(name, sizeof(name), "%02d-%s-%s", index, screen, suffix);
      shot(fresh, dir, name);
    };
    saveFresh(11, "first-run");
    fresh.os.launch("settings", "wifi");
    fresh.run(600);
    const Theme &t = fresh.os.theme();
    const Rect fc = fresh.os.contentRect();
    const int listTop = fc.y + t.m.gapS + (t.m.row + t.dp(4)) + t.m.gapS + (t.m.control - t.dp(8)) + t.m.gapS + t.dp(4);
    fresh.tap(fc.centerX(), listTop + t.dp(16));
    for (const char *p = "moon"; *p; ++p) {
      const Keyboard &kb = static_cast<TextEntryScreen *>(fresh.os.topScreen())->keyboard();
      const Rect key = kb.keyFor(*p);
      fresh.tap(key.centerX(), key.centerY());
    }
    saveFresh(12, "password");

    // Phone setup: the access point's name, password and address.
    fresh.os.goHome();
    fresh.os.launch("settings", "wifi");
    fresh.run(600);
    {
      const Theme &ft = fresh.os.theme();
      Rect fr[3];
      layout::column(fresh.os.contentRect().inset(ft.m.margin, ft.m.gapS),
                     {layout::px(ft.m.row + ft.dp(4)), layout::px(ft.m.control - ft.dp(8)), layout::flex()}, ft.m.gapS, fr);
      Rect fh[3];
      layout::row(fr[1], {layout::flex(), layout::px(ft.dp(64)), layout::px(ft.dp(64))}, ft.m.gapS, fh);
      fresh.tap(fh[1].centerX(), fh[1].centerY());
      fresh.run(3000);
    }
    saveFresh(22, "phone-setup");

    // Alarms, the alarm editor, a running timer and the alert it raises.
    s.os.launch("alarms");
    s.run(100);
    apps::AlarmEntry weekday;
    weekday.hour = 6;
    weekday.minute = 45;
    weekday.days = apps::kWeekdays;
    s.alarms.setAlarm(-1, weekday, s.os);
    apps::AlarmEntry weekend;
    weekend.hour = 9;
    weekend.minute = 30;
    weekend.days = apps::kWeekend;
    weekend.enabled = 0;
    s.alarms.setAlarm(-1, weekend, s.os);
    s.run(2500);  // let the earlier toast fade
    save(13, "alarms");
    const Theme &st = s.os.theme();
    const Rect sc = s.os.contentRect();
    const int firstRow = sc.y + st.m.gapS + st.m.control + st.m.gapM + st.dp(4) + (st.m.row + st.dp(8)) / 2;
    s.tap(sc.x + sc.w / 3, firstRow);
    s.run(2500);
    save(14, "alarm-edit");
    s.os.goHome();
    s.os.launch("timer");
    s.run(100);
    const int bottom = sc.bottom() - st.m.gapS;
    s.tap(sc.right() - st.m.margin - sc.w / 6, bottom - st.m.control / 2);  // Start (5 min)
    s.run(83 * 1000);
    save(15, "timer");
    s.os.launch("clock");
    s.run(5 * 60 * 1000);
    save(16, "alert");
    tapRect(s, s.os.alertButtonRect(0));

    // The stopwatch, a few laps in.
    s.os.goHome();
    s.os.launch("stopwatch");
    s.run(100);
    {
      const Rect area = sc.inset(st.m.margin, st.m.gapS);
      const Rect buttons(area.x, area.bottom() - st.m.control, area.w, st.m.control);
      Rect b[2];
      layout::row(buttons, {layout::flex(), layout::flex(2)}, st.m.gapM, b);
      tapRect(s, b[1]);  // Start
      const int lapMs[] = {21300, 19800, 23100, 20400};
      for (int ms : lapMs) {
        s.run((uint32_t)ms);
        tapRect(s, b[0]);  // Lap
      }
      s.run(7300);
      save(26, "stopwatch");
      tapRect(s, b[1]);  // Stop
    }

    // Display calibration (both tabs) and touch calibration.
    s.os.launch("settings");
    s.run(100);
    const Point display = s.settingsRow(1);
    s.tap(display.x, display.y);
    const Rect area = sc.inset(st.m.margin, st.m.gapS);
    const int want = st.m.control + st.dp(4), fit = (area.h - 2 * st.dp(4)) / 6;
    const int rowH = want <= fit ? want : fit < st.dp(32) ? st.dp(32) : fit;
    s.tap(area.centerX(), area.y + st.dp(4) + 5 * rowH + rowH / 2);
    s.run(2500);
    save(17, "calibrate-tone");
    const int tabsW = area.w - st.dp(72) - st.m.gapM;
    s.tap(area.x + tabsW * 3 / 6, area.y + st.m.control / 2);
    s.run(100);
    save(18, "calibrate-colour");
    s.tap(area.x + tabsW * 5 / 6, area.y + st.m.control / 2);
    s.run(100);
    save(19, "calibrate-panel");
    s.os.goHome();
    s.os.launch("touch");
    s.run(100);
    s.tap(area.right() - st.dp(52), area.bottom() - st.m.control / 2);
    s.run(100);
    save(20, "touch-calibrate");

    // Software update: offered, then installed and waiting for a restart.
    serveUpdate(s);
    s.os.goHome();
    s.os.launch("settings");
    while (s.os.topScreen() != &s.settings.root()) s.os.pop();  // earlier scenes left screens on its stack
    s.run(100);
    {
      const Point aboutRow = s.settingsRow(5);
      s.tap(aboutRow.x, aboutRow.y);
      Rect ar[2];
      layout::column(sc.inset(st.m.margin, st.m.gapS), {layout::flex(), layout::px(st.m.control)}, st.m.gapS, ar);
      save(23, "about-update");
      Rect aboutButtons[2];
      layout::row(ar[1], {layout::flex(), layout::flex()}, st.m.gapM, aboutButtons);
      tapRect(s, aboutButtons[1]);  // Software update
      const int lh = st.font.body->lineHeight;
      Rect ur[3];
      layout::column(sc.inset(st.m.margin, st.m.gapS),
                     {layout::px(2 * lh + st.dp(32) + st.dp(6) + st.dp(9) + 2 * st.m.padding), layout::flex(), layout::px(st.m.control)},
                     st.m.gapM, ur);
      const Rect action = layout::center(ur[2], st.dp(200), st.m.control);
      tapRect(s, action);
      s.run(3000);
      save(24, "update-available");
      tapRect(s, action);
      s.run(9000);
      save(25, "update-ready");
    }

    // Sound & light and its two helpers: asking which colour the LED is, and
    // the light sensor's calibration waiting for the room light.
    s.os.launch("settings");
    while (s.os.topScreen() != &s.settings.root()) s.os.pop();
    {
      const Point soundRow = s.settingsRow(2);
      s.tap(soundRow.x, soundRow.y);
      const Rect soundArea = sc.inset(st.m.margin, st.m.gapS);
      const int soundWant = st.m.control + st.dp(4), soundFit = (soundArea.h - 2 * st.dp(4)) / 6, soundMin = st.dp(32);
      const int soundRowH = soundWant <= soundFit ? soundWant : soundFit < soundMin ? soundMin : soundFit;
      auto soundCell = [&](int i) { return Rect(soundArea.x, soundArea.y + st.dp(4) + i * soundRowH, soundArea.w, soundRowH); };
      const Rect wizardRow(soundArea.x, soundArea.bottom() - st.m.control, soundArea.w, st.m.control);
      tapRect(s, soundCell(3));  // Find my LED
      tapRect(s, wizardRow);     // Start (one wide button)
      s.run(300);
      save(28, "find-led");
      s.os.pop();
      tapRect(s, soundCell(5));  // Light sensor
      tapRect(s, wizardRow);     // Start
      s.dev.light.ambient = 0.f;
      s.run(7200);
      save(29, "light-sensor");
      s.os.pop();
      s.run(100);
      s.os.pop();
    }

    // The remote page's Settings screen, and Home over a wallpaper (a sunset
    // gradient made here, sent the way the phone page sends one).
    s.os.goHome();
    s.os.launch("settings", "remote");
    s.run(200);
    s.os.remote().setEnabled(true);
    s.run(800);
    save(33, "remote-page");
    {
      std::string body(wallpaper::kPaletteBytes + wallpaper::kPixelBytes, '\0');
      for (int i = 0; i < wallpaper::kColors; ++i) {  // deep violet -> rose -> amber, top to bottom
        const int r = i < 128 ? 70 + i : 198 + (i - 128) * 57 / 127, g = i < 128 ? 40 + i * 70 / 127 : 110 + (i - 128) * 80 / 127,
                  b = i < 128 ? 120 + i * 20 / 127 : 140 - (i - 128) * 80 / 127;
        const uint16_t rgb = (uint16_t)(((r & 248) << 8) | ((g & 252) << 3) | (b >> 3));
        body[2 * i] = (char)(rgb & 255);
        body[2 * i + 1] = (char)(rgb >> 8);
      }
      for (int y = 0; y < wallpaper::kHeight; ++y)
        for (int x = 0; x < wallpaper::kWidth; ++x) {
          const int dx = x - 230, dy = y - 170;  // a soft glow low on the right
          int v = y * 255 / (wallpaper::kHeight - 1) + (dx * dx + dy * dy < 4000 ? 40 : 0);
          body[wallpaper::kPaletteBytes + (size_t)y * wallpaper::kWidth + x] = (char)(v > 255 ? 255 : v);
        }
      wallpaper::Header h{wallpaper::kMagic, wallpaper::kWidth, wallpaper::kHeight, wallpaper::kColors, 0,
                          wallpaper::crc32(body.data(), body.size())};
      std::string file(reinterpret_cast<const char *>(&h), sizeof(h));
      file += body;
      char pin[8];
      s.os.remote().pin(pin);
      const std::string req = std::string("POST /api/wallpaper HTTP/1.1\r\nX-Stella-Pin: ") + pin +
                              "\r\nContent-Length: " + std::to_string(file.size()) + "\r\n\r\n" + file;
      RemoteConnection conn(*s.dev.remote.handler);
      conn.feed(reinterpret_cast<const uint8_t *>(req.data()), req.size());
    }
    s.os.goHome();
    s.run(800);
    save(34, "home-wallpaper");
    s.os.remote().removeWallpaper();
    s.os.remote().setEnabled(false);
    s.run(600);

    // What's new, and the Home screen editor.
    s.os.goHome();
    s.os.launch("settings", "whatsnew");
    s.run(200);
    save(31, "whats-new");
    s.os.goHome();
    s.os.launch("home", "customise");
    s.run(200);
    save(32, "home-editor");
    s.os.goHome();
    s.os.launch("settings", "units");
    s.run(200);
    save(35, "units");
    s.os.goHome();
    s.os.launch("settings", "idle");
    s.run(200);
    save(36, "idle-face");
    s.os.goHome();
    s.run(100);

    // The bedside clock, with the alarm set earlier.
    s.os.goHome();
    s.os.launch("clock", "bedside");
    s.run(300);
    save(21, "bedside");
    // The dashboard idle face (Settings > Idle face > Dashboard), same state.
    s.os.goHome();
    s.os.launch("clock", "dashboard");
    s.run(300);
    save(37, "dashboard");

    // The Deck, on a board with Bluetooth: waiting for the computer to pair, then connected.
    SimSession deck(width, height, evening ? kEveningEpoch : kMorningEpoch, Appearance::Auto, false, true, true);
    auto saveDeck = [&](int index, const char *screen) {
      snprintf(name, sizeof(name), "%02d-%s-%s", index, screen, suffix);
      shot(deck, dir, name);
    };
    deck.os.launch("deck");
    deck.run(400);
    saveDeck(41, "deck-pairing");
    deck.dev.hid.connect();
    deck.run(900);
    saveDeck(42, "deck");
    // The Values app: the instructions first, then a script's numbers.
    s.os.goHome();
    s.os.launch("values");
    s.run(300);
    save(43, "values-empty");
    s.os.remote().setEnabled(true);
    s.run(1500);
    char pin[8];
    s.os.remote().pin(pin);
    RemoteRequest push;
    push.post = true;
    push.path = "/api/push";
    push.pin = pin;
    const char *body = "CPU=37%&GPU temp=61\xc2\xb0" "C&RAM=12.3 GB&Fan=1180 rpm&Build=passing&Net down=42 Mb/s";
    push.body = body;
    push.bodyLen = strlen(body);
    char reply[256];
    RemoteResponse response;
    response.body = reply;
    response.capacity = sizeof(reply);
    s.dev.remote.handler->handleRemote(push, response);
    s.run(400);
    save(44, "values");
    // The Calendar: first with no address, then with the simulator's feed.
    s.os.goHome();
    s.os.launch("calendar");
    s.run(300);
    save(38, "calendar-empty");
    s.os.goHome();
    s.os.launch("settings", "calendar");
    s.run(200);
    save(40, "settings-calendar");
    s.os.calendar().setAddress("https://calendar.example/feeds/home/basic.ics");
    s.run(3000);
    s.os.goHome();
    s.os.launch("calendar");
    s.run(300);
    save(39, "calendar");
    // Settings > Reset, the question before everything saved is erased.
    SimSession resetSession(width, height, evening ? kEveningEpoch : kMorningEpoch);  // a clean Settings stack
    resetSession.os.launch("settings", "reset");
    resetSession.run(200);
    snprintf(name, sizeof(name), "45-settings-reset-%s", suffix);
    shot(resetSession, dir, name);
    // The gift build: the message that opens first, and its pages.
    SimOptions giftOptions;
    giftOptions.epoch = evening ? kEveningEpoch : kMorningEpoch;
    giftOptions.gift = &giftContent();
    SimSession gift(width, height, giftOptions);
    auto saveGift = [&](int index, const char *screen) {
      snprintf(name, sizeof(name), "%02d-%s-%s", index, screen, suffix);
      shot(gift, dir, name);
    };
    gift.run(100);
    saveGift(46, "greeting");
    tapRect(gift, gift.greeting.continueRect());
    gift.run(100);
    saveGift(47, "greeting-page");
    for (int i = 0; i < 3; ++i) {
      tapRect(gift, gift.greeting.nextRect());
      gift.run(100);
    }
    saveGift(48, "greeting-address");
    tapRect(gift, gift.greeting.nextRect());
    gift.run(100);
    saveGift(49, "greeting-last");
  }
}

}  // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : "screenshots";
  mkdir(root.c_str(), 0755);
  capture(320, 240, root);
  capture(480, 320, root);
  if (failures) return 1;
  printf("screenshots written to %s\n", root.c_str());
  return 0;
}
