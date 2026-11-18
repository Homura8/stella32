// The remote page and the wallpaper: the file format, the streaming request
// reader, the PIN and its lock-out, uploads (whole, in odd pieces, damaged,
// dropped, two at once), and how the service starts and stops its server.
#include <string.h>

#include <string>
#include <vector>

#include "sim_session.h"
#include "stella/net/remote.h"
#include "stella/net/remote_page.h"
#include "stella/net/wallpaper.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {

// A wallpaper file: the left half colour `a`, the right half colour `b`.
std::string makeWallpaper(uint16_t a, uint16_t b, bool badCrc = false, bool badMagic = false) {
  std::string body(wallpaper::kPaletteBytes + wallpaper::kPixelBytes, '\0');
  for (int i = 0; i < wallpaper::kColors; ++i) {
    const uint16_t c = i == 1 ? a : i == 2 ? b : (uint16_t)(i * 257);
    body[2 * i] = (char)(c & 255);
    body[2 * i + 1] = (char)(c >> 8);
  }
  for (int y = 0; y < wallpaper::kHeight; ++y)
    for (int x = 0; x < wallpaper::kWidth; ++x)
      body[wallpaper::kPaletteBytes + (size_t)y * wallpaper::kWidth + x] = (char)(x < wallpaper::kWidth / 2 ? 1 : 2);
  wallpaper::Header h{};
  h.magic = badMagic ? 0x12345678u : wallpaper::kMagic;
  h.width = wallpaper::kWidth;
  h.height = wallpaper::kHeight;
  h.colors = wallpaper::kColors;
  h.crc = wallpaper::crc32(body.data(), body.size()) ^ (badCrc ? 1u : 0u);
  std::string file(sizeof(h), '\0');
  memcpy(&file[0], &h, sizeof(h));
  return file + body;
}

struct Reply {
  int status = 0;
  std::string body;
};

// One request, fed in pieces of `piece` bytes (0 = all at once).
Reply send(RemoteHandler &handler, const std::string &raw, size_t piece = 0) {
  RemoteConnection conn(handler);
  if (!piece) piece = raw.size();
  for (size_t i = 0; i < raw.size() && !conn.done(); i += piece)
    conn.feed((const uint8_t *)raw.data() + i, raw.size() - i < piece ? raw.size() - i : piece);
  Reply r;
  if (!conn.done()) return r;
  char head[256], scratch[1024];
  const uint8_t *body = nullptr;
  size_t n = 0;
  conn.respond(head, sizeof(head), scratch, sizeof(scratch), &body, &n);
  sscanf(head, "HTTP/1.1 %d", &r.status);
  r.body.assign((const char *)body, n);
  return r;
}

std::string get(const char *path) { return std::string("GET ") + path + " HTTP/1.1\r\nHost: x\r\n\r\n"; }
std::string post(const char *path, const std::string &pin, const std::string &body = "") {
  return std::string("POST ") + path + " HTTP/1.1\r\nX-Stella-Pin: " + pin + "\r\nContent-Length: " +
         std::to_string(body.size()) + "\r\n\r\n" + body;
}

// A service on its own devices: enough to drive it by hand.
struct Rig {
  SimRemoteServer server;
  SimWallpaperStore store;
  MemoryStore kv;
  Topic<NetworkStatus> net{topics::kNetwork};
  Topic<RemoteInfo> info{topics::kRemote};
  RemoteService service{info};
  CalendarSource calendar;
  uint32_t now = 1000;
  explicit Rig(bool connected = true) {
    NetworkStatus n;
    n.present = n.connected = connected;
    net.publish(n);
    calendar.configure(&kv, nullptr);
    service.configure(&server, &store, nullptr, &kv, nullptr, &net, &calendar);
  }
  void run(uint32_t ms = 0) {
    now += ms;
    service.run(now);
  }
  std::string pin() {
    char p[8];
    service.pin(p);
    return p;
  }
};

}  // namespace

TEST(crc32_matches_the_standard_check_value_and_continues) {
  CHECK_EQ(wallpaper::crc32("123456789", 9), 0xCBF43926u);
  const uint32_t a = wallpaper::crc32("12345", 5);
  CHECK_EQ(wallpaper::crc32("6789", 4, a), 0xCBF43926u);
  CHECK_EQ(wallpaper::crc32("", 0), 0u);
}

TEST(a_wallpaper_file_loads_only_when_whole_and_undamaged) {
  SimWallpaperStore store;
  const std::string good = makeWallpaper(0xF800, 0x001F);
  CHECK_EQ(good.size(), wallpaper::kTotalBytes);
  store.write(0, (const uint8_t *)good.data(), good.size());
  uint16_t palette[wallpaper::kColors];
  CHECK(wallpaper::load(store, palette) && palette[1] == 0xF800 && palette[2] == 0x001F);
  store.bytes[wallpaper::kTotalBytes - 1] ^= 1;  // one flipped bit in the last pixel
  CHECK(!wallpaper::load(store, palette));
  store.bytes[wallpaper::kTotalBytes - 1] ^= 1;
  CHECK(wallpaper::load(store, palette));
  store.bytes[0] = 'X';  // not our file
  CHECK(!wallpaper::load(store, palette));
  store.bytes[0] = 'S';
  store.size = wallpaper::kTotalBytes - 1;  // a flash area that is too small
  CHECK(!wallpaper::load(store, palette));
}

TEST(the_request_reader_streams_long_bodies_in_any_split_and_refuses_the_rest) {
  struct Sink : HttpBodySink {
    std::string got;
    int refuse = 0;
    int ends = 0;
    bool complete = false;
    int begin(const HttpRequestParser &r, size_t length) override {
      got.clear();
      return strcmp(r.path(), "/up") == 0 && length == 2000 ? refuse : 413;
    }
    bool write(const uint8_t *d, size_t n) override {
      got.append((const char *)d, n);
      return true;
    }
    void end(bool c) override {
      ++ends;
      complete = c;
    }
  } sink;
  const std::string body(2000, 'q');
  const std::string req = "POST /up HTTP/1.1\r\nContent-Length: 2000\r\n\r\n" + body;
  for (size_t piece : {size_t(1), size_t(7), size_t(333), req.size()}) {
    HttpRequestParser p;
    p.setStreaming(&sink, 4096);
    sink.ends = 0;
    for (size_t i = 0; i < req.size(); i += piece) p.feed((const uint8_t *)req.data() + i, req.size() - i < piece ? req.size() - i : piece);
    CHECK(p.state() == HttpRequestParser::State::Complete && sink.got == body && sink.ends == 1 && sink.complete);
  }
  // A sink that says no: the request fails with its status and nothing is written.
  sink.refuse = 409;
  HttpRequestParser p;
  p.setStreaming(&sink, 4096);
  p.feed((const uint8_t *)req.data(), req.size());
  CHECK(p.state() == HttpRequestParser::State::Failed && p.errorStatus() == 409 && sink.got.empty());
  // Too long for the limit, and long with no sink at all.
  HttpRequestParser big;
  big.setStreaming(&sink, 1000);
  const std::string tooBig = "POST /up HTTP/1.1\r\nContent-Length: 2000\r\n\r\n";
  big.feed((const uint8_t *)tooBig.data(), tooBig.size());
  CHECK(big.errorStatus() == 413);
  HttpRequestParser none;
  none.feed((const uint8_t *)req.data(), 60);
  CHECK(none.errorStatus() == 413);
  // The connection drops half way: the sink hears it did not finish.
  sink.refuse = 0;
  sink.ends = 0;
  HttpRequestParser cut;
  cut.setStreaming(&sink, 4096);
  cut.feed((const uint8_t *)req.data(), 100);
  cut.reset();
  CHECK(sink.ends == 1 && !sink.complete);
  // The pin header is read.
  HttpRequestParser withPin;
  const std::string head = "POST /a HTTP/1.1\r\nX-Stella-Pin: 482913\r\nContent-Length: 0\r\n\r\n";
  withPin.feed((const uint8_t *)head.data(), head.size());
  CHECK(strcmp(withPin.pin(), "482913") == 0);
}

TEST(the_page_and_status_are_public_everything_else_wants_the_pin) {
  Rig r;
  const Reply page = send(r.service, get("/"));
  CHECK(page.status == 200 && page.body.find("StellaOS") != std::string::npos && page.body.size() > 3000);
  const Reply status = send(r.service, get("/api/status"));
  CHECK(status.status == 200 && status.body.find("\"wallpaper\":false") != std::string::npos);
  CHECK(status.body.find(r.pin()) == std::string::npos);  // the PIN is never offered
  CHECK_EQ(send(r.service, get("/nope")).status, 404);
  CHECK_EQ(send(r.service, post("/api/check", "000000")).status, r.pin() == "000000" ? 200 : 401);
  CHECK_EQ(send(r.service, post("/api/check", r.pin())).status, 200);
  CHECK_EQ(send(r.service, post("/api/veil", "", "v=40")).status, 401);
  CHECK_EQ(send(r.service, post("/api/wallpaper/remove", "")).status, 401);
  CHECK_EQ(send(r.service, post("/api/nothing", r.pin())).status, 404);
}

TEST(wrong_pins_lock_the_page_for_five_minutes_even_for_the_right_pin) {
  Rig r;
  const std::string right = r.pin();
  for (int i = 0; i < RemoteService::kMaxFailures; ++i) CHECK_EQ(send(r.service, post("/api/check", "999999x")).status, 401);
  CHECK_EQ(send(r.service, post("/api/check", right)).status, 429);  // locked
  r.run(60 * 1000);
  CHECK_EQ(send(r.service, post("/api/check", right)).status, 429);
  r.run(RemoteService::kLockMs);
  CHECK_EQ(send(r.service, post("/api/check", right)).status, 200);  // five minutes later
  // A right PIN in between wipes the count.
  for (int i = 0; i < RemoteService::kMaxFailures - 1; ++i) send(r.service, post("/api/check", "bad"));
  CHECK_EQ(send(r.service, post("/api/check", right)).status, 200);
  for (int i = 0; i < RemoteService::kMaxFailures - 1; ++i) send(r.service, post("/api/check", "bad"));
  CHECK_EQ(send(r.service, post("/api/check", right)).status, 200);
}

TEST(a_wallpaper_uploads_whole_or_in_odd_pieces_and_is_kept) {
  for (size_t piece : {size_t(0), size_t(1460), size_t(97)}) {
    Rig r;
    const std::string file = makeWallpaper(0x07E0, 0xFFE0);
    const Reply up = send(r.service, post("/api/wallpaper", r.pin(), file), piece);
    CHECK(up.status == 200);
    CHECK(r.service.wallpaperValid());
    CHECK(memcmp(r.store.bytes.data(), file.data(), file.size()) == 0);
    r.run();
    CHECK(r.service.status().wallpaper && r.service.status().generation > 1);
    // It survives a restart: a service started on the same flash sees it.
    Rig again(true);
    again.store.bytes = r.store.bytes;
    again.service.configure(&again.server, &again.store, nullptr, &again.kv, nullptr, &again.net);
    CHECK(again.service.wallpaperValid());
  }
}

TEST(uploads_are_refused_when_wrong_short_damaged_or_concurrent) {
  Rig r;
  const std::string file = makeWallpaper(0x07E0, 0xFFE0);
  CHECK_EQ(send(r.service, post("/api/wallpaper", "111111", file)).status, 401);  // wrong PIN: nothing written
  CHECK(!r.service.wallpaperValid() && r.store.writes == 0);
  CHECK_EQ(send(r.service, post("/api/wallpaper", r.pin(), file.substr(0, 5000))).status, 413);  // the wrong size
  CHECK_EQ(send(r.service, post("/api/wallpaper", r.pin(), makeWallpaper(1, 2, /*badCrc=*/true))).status, 422);
  CHECK(!r.service.wallpaperValid());
  CHECK_EQ(send(r.service, post("/api/wallpaper", r.pin(), makeWallpaper(1, 2, false, /*badMagic=*/true))).status, 500);
  CHECK(!r.service.wallpaperValid());
  // A good one after the bad ones.
  CHECK_EQ(send(r.service, post("/api/wallpaper", r.pin(), file)).status, 200);
  CHECK(r.service.wallpaperValid());
  // Two uploads at once: the second is told to wait; the first still completes.
  const std::string head = post("/api/wallpaper", r.pin(), "").substr(0, 0);
  RemoteConnection first(r.service), second(r.service);
  const std::string req = post("/api/wallpaper", r.pin(), file);
  first.feed((const uint8_t *)req.data(), req.size() / 2);
  second.feed((const uint8_t *)req.data(), req.size() / 2);
  CHECK(second.parser().errorStatus() == 409);
  first.feed((const uint8_t *)req.data() + req.size() / 2, req.size() - req.size() / 2);
  CHECK(first.done() && first.parser().state() == HttpRequestParser::State::Complete);
  CHECK(r.service.wallpaperValid());
  (void)head;
}

TEST(a_dropped_upload_leaves_no_picture_and_the_next_one_works) {
  Rig r;
  const std::string file = makeWallpaper(0x07E0, 0xFFE0);
  {
    RemoteConnection c(r.service);
    const std::string req = post("/api/wallpaper", r.pin(), file);
    c.feed((const uint8_t *)req.data(), req.size() * 2 / 3);
    CHECK(!r.service.wallpaperValid());  // while it arrives the old picture is not used
    c.reset();                           // the phone went out of range
  }
  CHECK(!r.service.wallpaperValid());
  CHECK_EQ(send(r.service, post("/api/wallpaper", r.pin(), file)).status, 200);
  CHECK(r.service.wallpaperValid());
  // Flash failing under the write: refused, not valid.
  r.store.failWrites = true;
  CHECK(send(r.service, post("/api/wallpaper", r.pin(), file)).status == 500);
  CHECK(!r.service.wallpaperValid());
}

TEST(removing_the_wallpaper_and_the_veil_need_the_pin_and_are_remembered) {
  Rig r;
  const std::string file = makeWallpaper(0x07E0, 0xFFE0);
  send(r.service, post("/api/wallpaper", r.pin(), file));
  CHECK_EQ(send(r.service, post("/api/veil", r.pin(), "v=120")).status, 400);
  CHECK_EQ(send(r.service, post("/api/veil", r.pin(), "v=abc")).status, 400);
  CHECK_EQ(send(r.service, post("/api/veil", r.pin(), "v=60")).status, 200);
  r.run();
  CHECK_EQ(r.service.status().veil, 60);
  CHECK_EQ(send(r.service, post("/api/wallpaper/remove", r.pin())).status, 200);
  r.run();
  CHECK(!r.service.wallpaperValid() && !r.service.status().wallpaper);
  // Veil, enabled and the PIN are kept across a restart.
  r.service.setEnabled(true);
  r.run();
  const std::string pin = r.pin();
  Rig again;
  again.kv = r.kv;
  again.service.configure(&again.server, &again.store, nullptr, &again.kv, nullptr, &again.net);
  CHECK(again.pin() == pin && again.service.status().veil == 60 && again.service.status().enabled);
}

TEST(the_server_runs_only_when_asked_connected_and_not_during_phone_setup) {
  Rig r;
  r.run();
  CHECK(!r.server.up && r.server.begins == 0);  // off until the person turns it on
  r.service.setEnabled(true);
  r.run();
  CHECK(r.server.up && r.service.status().running);
  CHECK(strcmp(r.service.status().host, "stella-sim1") == 0 && strcmp(r.service.status().ip, "192.168.1.50") == 0);
  CHECK(r.server.polls >= 1);
  NetworkStatus n;
  n.present = true;
  n.connected = true;
  n.setup = true;  // the setup access point wants port 80
  r.net.publish(n);
  r.run();
  CHECK(!r.server.up && !r.service.status().running);
  n.setup = false;
  r.net.publish(n);
  r.run();
  CHECK(r.server.up);
  n.connected = false;  // Wi-Fi dropped
  r.net.publish(n);
  r.run();
  CHECK(!r.server.up);
  r.service.setEnabled(false);
  n.connected = true;
  r.net.publish(n);
  r.run();
  CHECK(!r.server.up);
  // A server that cannot start does not break the service.
  r.server.refuse = true;
  r.service.setEnabled(true);
  r.run();
  r.run();
  CHECK(!r.server.up);
}

TEST(a_new_pin_replaces_the_old_one_and_clears_a_lock) {
  Rig r;
  const std::string before = r.pin();
  CHECK(before.size() == 6 && before.find_first_not_of("0123456789") == std::string::npos);
  for (int i = 0; i < RemoteService::kMaxFailures; ++i) send(r.service, post("/api/check", "bad"));
  CHECK_EQ(send(r.service, post("/api/check", before)).status, 429);
  r.service.newPin();
  r.run();
  CHECK(r.service.status().lockedOut == false);
  CHECK_EQ(send(r.service, post("/api/check", r.pin())).status, 200);
}

// ------------------------------------------------------------------ on a whole device

namespace {

// The rows of Settings > Remote page, as RemoteScreen::layout() lays them out.
Rect remoteRow(SimSession &s, int i) {
  const Theme &t = s.os.theme();
  const Rect area = s.os.contentRect().inset(t.m.margin, t.m.gapS);
  const int hintH = t.font.caption->lineHeight * 2 + t.dp(4);
  const int avail = area.h - hintH - t.m.gapS;
  const int want = t.m.control + t.dp(4), fit = (avail - 2 * t.dp(4)) / 5, minimum = t.dp(32);
  const int rowH = want <= fit ? want : fit < minimum ? minimum : fit;
  const Rect inner(area.x + t.m.padding, area.y + t.dp(4), area.w - 2 * t.m.padding, 5 * rowH);
  return Rect(inner.x, inner.y + i * rowH, inner.w, rowH);
}

void tapRect(SimSession &s, const Rect &r) { s.tap(r.centerX(), r.centerY()); }

std::string pinOf(SimSession &s) {
  char p[8];
  s.os.remote().pin(p);
  return p;
}

bool sameAsFullRedraw(SimSession &s) {
  const std::vector<Color> incremental = s.dev.display.framebuffer();
  s.os.invalidateAll();
  s.run(20);
  return incremental == s.dev.display.framebuffer();
}

}  // namespace

TEST(a_wallpaper_replaces_the_sky_washed_toward_the_theme_and_goes_away_again) {
  SimSession s(320, 240);  // night: dark mode washes toward black
  s.os.remote().setEnabled(true);
  s.run(600);
  RemoteHandler *handler = s.dev.remote.handler;
  CHECK(s.dev.remote.up && handler != nullptr);
  const Color skyLeft = s.dev.display.pixel(3, 100);
  const std::string file = makeWallpaper(0xF800, 0x001F);
  CHECK_EQ(send(*handler, post("/api/wallpaper", pinOf(s), file)).status, 200);
  s.run(600);
  const uint8_t veil = kDefaultWallpaperVeil * 255 / 100;
  CHECK(s.dev.display.pixel(3, 100) == blend((Color)0xF800, (Color)0x0000, veil));  // left half red, washed dark
  CHECK(s.dev.display.pixel(316, 100) == blend((Color)0x001F, (Color)0x0000, veil));
  CHECK(s.dev.display.pixel(3, 100) != skyLeft);
  CHECK(sameAsFullRedraw(s));
  // The wash is the person's: none at all shows the picture as it is.
  CHECK_EQ(send(*handler, post("/api/veil", pinOf(s), "v=0")).status, 200);
  s.run(600);
  CHECK(s.dev.display.pixel(3, 100) == (Color)0xF800 && s.dev.display.pixel(316, 100) == (Color)0x001F);
  CHECK(sameAsFullRedraw(s));
  // Another screen over it, still consistent.
  s.os.launch("weather");
  s.run(300);
  CHECK(sameAsFullRedraw(s));
  s.os.goHome();
  // Gone: the sky is back.
  CHECK_EQ(send(*handler, post("/api/wallpaper/remove", pinOf(s))).status, 200);
  s.run(600);
  CHECK(s.dev.display.pixel(3, 100) != (Color)0xF800);
  CHECK(sameAsFullRedraw(s));
}

TEST(a_wallpaper_is_ignored_on_a_screen_it_was_not_made_for) {
  SimSession s(480, 320);
  s.os.remote().setEnabled(true);
  s.run(600);
  const Color sky = s.dev.display.pixel(3, 100);
  CHECK_EQ(send(*s.dev.remote.handler, post("/api/wallpaper", pinOf(s), makeWallpaper(0xF800, 0x001F))).status, 200);
  s.run(600);
  CHECK(s.dev.display.pixel(3, 100) == sky);  // 320x240 only, for now
  CHECK(sameAsFullRedraw(s));
}

TEST(the_remote_screen_enables_the_server_shows_the_pin_and_controls_the_wash) {
  SimSession s(320, 240);
  s.os.launch("settings", "remote");
  s.run(300);
  CHECK(s.os.topScreen()->title() && strcmp(s.os.topScreen()->title(), "Remote page") == 0);
  CHECK(!s.os.remote().status().enabled && pinOf(s).size() == 6);
  tapRect(s, remoteRow(s, 0));  // the switch
  s.run(600);
  CHECK(s.os.remote().status().enabled && s.os.remote().status().running && s.dev.remote.up);
  CHECK(sameAsFullRedraw(s));
  const std::string before = pinOf(s);
  const Theme &t = s.os.theme();
  const Rect pinRow = remoteRow(s, 2);
  s.tap(pinRow.right() - t.dp(40), pinRow.centerY());  // Change
  s.run(300);
  CHECK(pinOf(s) != before);
  // The wash slider is the last row: drag it left.
  const Rect washRow = remoteRow(s, 4);
  s.drag(washRow.right() - 20, washRow.centerY(), washRow.x + t.dp(84) + 30, washRow.centerY());
  s.run(600);
  CHECK(s.os.remote().status().veil < 20);
  // Remove does nothing without a picture; with one it removes it.
  const Rect rmRow = remoteRow(s, 3);
  s.tap(rmRow.right() - t.dp(40), rmRow.centerY());
  CHECK(!s.os.remote().status().wallpaper);
  send(*s.dev.remote.handler, post("/api/wallpaper", pinOf(s), makeWallpaper(0xF800, 0x001F)));
  s.run(600);
  CHECK(s.os.remote().status().wallpaper);
  s.tap(rmRow.right() - t.dp(40), rmRow.centerY());
  s.run(600);
  CHECK(!s.os.remote().status().wallpaper);
}

TEST(the_remote_screen_and_settings_row_render_at_every_size) {
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    SimSession s(sz[0], sz[1], kMorningEpoch);
    s.os.launch("settings", "remote");
    s.run(300);
    CHECK(remoteRow(s, 4).bottom() <= sz[1] && remoteRow(s, 0).h >= s.os.theme().dp(28));
    CHECK(sameAsFullRedraw(s));
    s.os.remote().setEnabled(true);
    s.run(600);
    CHECK(sameAsFullRedraw(s));
    s.os.pop();
    s.run(100);
    CHECK(sameAsFullRedraw(s));  // Settings' list with its new row
  }
}

// ------------------------------------------------------------------ calendar address

TEST(the_calendar_address_is_set_checked_remembered_and_never_sent_back) {
  Rig r;
  const std::string url = "https://calendar.example/feeds/private-6f1c2d3e4a5b/basic.ics";
  // Needs the PIN, like everything else.
  CHECK_EQ(send(r.service, post("/api/calendar", "", url)).status, 401);
  CHECK(!r.calendar.hasAddress());
  CHECK_EQ(send(r.service, post("/api/calendar/status", "")).status, 401);
  // Not acceptable: not https, a space, no host, too long.
  const std::string bad[] = {"http://calendar.example/a.ics", "https://calendar.example/a b.ics", "https:///a.ics", "calendar.example",
                             "", std::string("https://calendar.example/") + std::string(300, 'x')};
  for (const std::string &b : bad) {
    const Reply reply = send(r.service, post("/api/calendar", r.pin(), b));
    CHECK_EQ(reply.status, 400);
    CHECK(reply.body.find("error") != std::string::npos);
  }
  CHECK(!r.calendar.hasAddress());
  // A good one, with the trailing newline a paste often brings.
  CHECK_EQ(send(r.service, post("/api/calendar", r.pin(), url + "\n")).status, 200);
  CHECK(r.calendar.hasAddress());
  char copy[kCalendarUrlBytes];
  CHECK(r.calendar.copyAddress(copy) && url == copy);
  // The status names the host and nothing else: no path, no secret.
  const Reply status = send(r.service, post("/api/calendar/status", r.pin()));
  CHECK_EQ(status.status, 200);
  CHECK(status.body.find("\"set\":true") != std::string::npos && status.body.find("calendar.example") != std::string::npos);
  CHECK(status.body.find("private") == std::string::npos && status.body.find("basic.ics") == std::string::npos);
  CHECK(send(r.service, get("/api/status")).body.find("calendar") == std::string::npos);  // nothing without the PIN
  // It is remembered: a new source on the same store has it.
  CalendarSource again;
  again.configure(&r.kv, nullptr);
  CHECK(again.hasAddress() && again.generation() == 0);
  char host[64];
  again.host(host, sizeof(host));
  CHECK(strcmp(host, "calendar.example") == 0);
  // Remove.
  CHECK_EQ(send(r.service, post("/api/calendar/remove", r.pin())).status, 200);
  CHECK(!r.calendar.hasAddress());
  CalendarSource gone;
  gone.configure(&r.kv, nullptr);
  CHECK(!gone.hasAddress());
  CHECK(send(r.service, post("/api/calendar/status", r.pin())).body.find("\"set\":false") != std::string::npos);
}

TEST(the_page_has_a_calendar_section_that_never_displays_the_address) {
  const std::string page(kRemotePage);
  CHECK(page.find("id=cal ") != std::string::npos && page.find("/api/calendar/status") != std::string::npos);
  CHECK(page.find("/api/calendar/remove") != std::string::npos);
  // The field is cleared after saving, and only the host is shown back.
  CHECK(page.find("$('cal').value='';") != std::string::npos && page.find("s.host") != std::string::npos);
}

// Regression (owner, on the CYD): a wrong PIN left "Wrong PIN" at the bottom of the
// page even after the right one unlocked it. The page now clears its message first.
TEST(the_page_clears_an_old_error_when_it_unlocks) {
  const std::string page(kRemotePage);
  const size_t go = page.find("$('go').onclick");
  CHECK(go != std::string::npos);
  const std::string handler = page.substr(go, 260);
  CHECK(handler.find("say('')") != std::string::npos);                 // cleared on every try
  CHECK(handler.find("say('');$('top').textContent='Unlocked.'") != std::string::npos);  // and again on success
}

// The first wallpaper looked washed out and pixelated with the first defaults
// (35 % wash, the picture softened to a quarter size): they are lighter now.
TEST(wallpaper_defaults_are_light) {
  CHECK_EQ(kDefaultWallpaperVeil, 15);
  const std::string page(kRemotePage);
  CHECK(page.find("id=veil min=0 max=90 value=15") != std::string::npos);
  CHECK(page.find("id=soft min=0 max=4 value=1") != std::string::npos);
  SimSession s(320, 240);
  CHECK_EQ(s.os.remote().status().veil, kDefaultWallpaperVeil);  // before anything was saved
}
