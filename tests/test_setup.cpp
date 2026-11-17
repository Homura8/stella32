// Phone-based Wi-Fi setup: the portal's parsing and page, the Wi-Fi service's
// setup mode, and the on-device screen, with the test playing the phone.
#include <string.h>

#include <string>

#include "sim_session.h"
#include "stella/net/setup_portal.h"
#include "stella/ui/layout.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {

// A stand-in for the Wi-Fi service, to test the portal alone.
struct FakeControl : SetupControl {
  WifiScan scan;
  NetworkStatus status;
  std::string joinedSsid, joinedPassword;
  int joins = 0;
  WifiScan setupScan() const override { return scan; }
  NetworkStatus setupStatus() const override { return status; }
  void setupJoin(const char *ssid, const char *password) override {
    joinedSsid = ssid;
    joinedPassword = password;
    ++joins;
  }
};

std::string serve(SetupPortal &portal, bool post, const char *path, const std::string &body, int &status) {
  static char page[SetupPortal::kPageBytes];
  SetupRequest req;
  req.post = post;
  req.path = path;
  req.body = body.c_str();
  req.bodyLen = body.size();
  SetupResponse resp;
  resp.body = page;
  resp.capacity = sizeof(page);
  portal.handleSetup(req, resp);
  status = resp.status;
  return std::string(page, resp.length);
}

bool contains(const std::string &haystack, const char *needle) { return haystack.find(needle) != std::string::npos; }

Rect phoneButton(SimSession &s) {
  const Theme &t = s.os.theme();
  Rect r[3];
  layout::column(s.os.contentRect().inset(t.m.margin, t.m.gapS),
                 {layout::px(t.m.row + t.dp(4)), layout::px(t.m.control - t.dp(8)), layout::flex()}, t.m.gapS, r);
  Rect h[3];
  layout::row(r[1], {layout::flex(), layout::px(t.dp(64)), layout::px(t.dp(64))}, t.m.gapS, h);
  return h[1];
}

Rect cancelButton(SimSession &s) {
  const Theme &t = s.os.theme();
  const int rows = t.font.title->lineHeight + t.font.headline->lineHeight + t.font.title->lineHeight + 2 * t.dp(4);
  Rect r[3];
  layout::column(s.os.contentRect().inset(t.m.margin, t.m.gapS),
                 {layout::px(rows + 2 * t.dp(8)), layout::flex(), layout::px(t.m.control)}, t.m.gapM, r);
  return layout::center(r[2], t.dp(120), t.m.control);
}

SimSession offline() { return SimSession(320, 240, kEveningEpoch, Appearance::Auto, false, /*online=*/false); }

}  // namespace

TEST(setup_form_parsing_is_bounded_and_decodes) {
  char out[16];
  CHECK_EQ(SetupPortal::urlDecode(out, sizeof(out), "a%20b+c%41", 10), 6);
  CHECK(strcmp(out, "a b cA") == 0);
  CHECK_EQ(SetupPortal::urlDecode(out, sizeof(out), "100%", 4), 4);  // a stray '%' stays
  CHECK(strcmp(out, "100%") == 0);
  CHECK_EQ(SetupPortal::urlDecode(out, sizeof(out), "%zz%4", 5), 5);
  CHECK_EQ(SetupPortal::urlDecode(out, 4, "abcdef", 6), -1);  // does not fit: truncated, NUL-terminated
  CHECK(strcmp(out, "abc") == 0);
  const std::string body = "ssid=My+Net%26Co&pw=p%40ss%3Dword&x=1";
  char ssid[40], pw[40];
  CHECK(SetupPortal::formField(body.c_str(), body.size(), "ssid", ssid, sizeof(ssid)) && strcmp(ssid, "My Net&Co") == 0);
  CHECK(SetupPortal::formField(body.c_str(), body.size(), "pw", pw, sizeof(pw)) && strcmp(pw, "p@ss=word") == 0);
  CHECK(!SetupPortal::formField(body.c_str(), body.size(), "missing", pw, sizeof(pw)));
  CHECK(!SetupPortal::formField(body.c_str(), body.size(), "ss", pw, sizeof(pw)));  // not a prefix match
  char tiny[4];
  CHECK(!SetupPortal::formField(body.c_str(), body.size(), "ssid", tiny, sizeof(tiny)));  // too long
  char esc[64];
  CHECK(SetupPortal::htmlEscape(esc, sizeof(esc), "<b a=\"x\">'&'</b>"));
  CHECK(strcmp(esc, "&lt;b a=&quot;x&quot;&gt;&#39;&amp;&#39;&lt;/b&gt;") == 0);
  CHECK(!SetupPortal::htmlEscape(esc, 8, "<<<<<<<<"));
}

TEST(setup_page_lists_networks_escaped_and_joins_only_valid_forms) {
  FakeControl control;
  control.scan.count = 2;
  strcpy(control.scan.networks[0].ssid, "Home <b>Net</b>");
  strcpy(control.scan.networks[1].ssid, "Cafe \"Wi-Fi\"");
  SetupPortal portal(control);
  int status = 0;
  const std::string page = serve(portal, false, "/generate_204", "", status);  // a phone's connectivity probe
  CHECK_EQ(status, 200);
  CHECK(contains(page, "<form method=post action=/join>") && contains(page, "Stella Wi-Fi"));
  CHECK(contains(page, "Home &lt;b&gt;Net&lt;/b&gt;") && !contains(page, "<b>Net</b>"));
  CHECK(contains(page, "Cafe &quot;Wi-Fi&quot;"));
  CHECK(page.size() < SetupPortal::kPageBytes);
  // A valid form joins; the page tells the person to watch the device.
  const std::string ok = serve(portal, true, "/join", "ssid=Home+Net&pw=correct%20horse", status);
  CHECK_EQ(status, 200);
  CHECK(control.joins == 1 && control.joinedSsid == "Home Net" && control.joinedPassword == "correct horse");
  CHECK(contains(ok, "Watch the Stella screen"));
  // An open network: no password field at all.
  serve(portal, true, "/join", "ssid=Cafe", status);
  CHECK(control.joins == 2 && control.joinedSsid == "Cafe" && control.joinedPassword.empty());
  // Anything malformed is refused and joins nothing.
  const int before = control.joins;
  serve(portal, true, "/join", "pw=only", status);
  CHECK_EQ(status, 400);
  serve(portal, true, "/join", "ssid=", status);
  CHECK_EQ(status, 400);
  serve(portal, true, "/join", "ssid=" + std::string(33, 'a') + "&pw=x", status);  // longer than 32 bytes
  CHECK_EQ(status, 400);
  serve(portal, true, "/join", "ssid=ok&pw=" + std::string(64, 'p'), status);  // longer than a WPA2 passphrase
  CHECK_EQ(status, 400);
  serve(portal, true, "/elsewhere", "ssid=ok&pw=x", status);
  CHECK_EQ(status, 404);
  CHECK_EQ(control.joins, before);
  // The page shows a failed attempt and an attempt in progress.
  control.status.state = WifiState::Disconnected;
  strcpy(control.status.error, "Couldn't join Home");
  CHECK(contains(serve(portal, false, "/", "", status), "Couldn&#39;t join Home"));  // escaped
  control.status.error[0] = '\0';
  control.status.state = WifiState::Connecting;
  strcpy(control.status.ssid, "Home");
  CHECK(contains(serve(portal, false, "/", "", status), "Connecting to Home"));
  // Hostile SSIDs cannot overflow the page buffer.
  control.scan.count = WifiScan::kMax;
  for (int i = 0; i < WifiScan::kMax; ++i) memset(control.scan.networks[i].ssid, '<', 32);
  const std::string big = serve(portal, false, "/", "", status);
  CHECK(big.size() < SetupPortal::kPageBytes);
}

TEST(setup_mode_opens_the_access_point_and_joins_from_the_phone) {
  SimSession s = offline();
  CHECK(!s.dev.setup.up);
  s.os.network().beginSetup();
  s.run(3000);
  SetupInfo info;
  CHECK(s.os.network().setupInfo(info) && info.up && s.dev.setup.up);
  CHECK(strcmp(info.ssid, "Stella-Setup") == 0 && strcmp(info.address, "192.168.4.1") == 0);
  CHECK_EQ(strlen(info.password), 8);
  for (const char *p = info.password; *p; ++p) CHECK(strchr("ABCDEFGHJKMNPQRSTUVWXYZ23456789", *p) != nullptr);
  CHECK(s.dev.setup.password == info.password && s.os.network().status().setup);
  // The page lists what the device found while it was looking around.
  int status = 0;
  std::string page = s.dev.setup.request(false, "/", "", &status);
  CHECK(status == 200 && contains(page, "Starlight") && contains(page, "Observatory"));
  // The wrong password: the access point stays open and the page says why.
  s.dev.setup.request(true, "/join", "ssid=Starlight&pw=wrongpass", &status);
  s.run(30 * 1000);
  CHECK(s.dev.setup.up && s.os.network().status().setup && !s.os.network().status().connected);
  page = s.dev.setup.request(false, "/", "", &status);
  CHECK(contains(page, "join Starlight") && contains(page, "Check the password"));
  // The right one: connected, the access point closes by itself, credentials are saved.
  s.dev.setup.request(true, "/join", "ssid=Starlight&pw=moonbeam", &status);
  s.run(10 * 1000);
  const NetworkStatus net = s.os.network().status();
  CHECK(net.connected && !net.setup && net.state == WifiState::Connected && strcmp(net.ssid, "Starlight") == 0);
  CHECK(!s.dev.setup.up && !s.os.network().setupInfo(info));
  WifiCredentials saved;
  CHECK(loadRecord(&s.dev.storage, "wifi", "creds", WifiCredentials::kVersion, &saved, sizeof(saved)) ==
            RecordStatus::Ok &&
        strcmp(saved.ssid, "Starlight") == 0);
  CHECK(s.dev.setup.request(false, "/").empty());  // nothing is listening any more
}

TEST(setup_closes_on_cancel_on_timeout_and_when_it_cannot_start) {
  SimSession s = offline();
  s.os.network().beginSetup();
  s.run(3000);
  CHECK(s.dev.setup.up);
  s.os.network().cancelSetup();
  s.run(500);
  SetupInfo info;
  CHECK(!s.dev.setup.up && !s.os.network().status().setup && !s.os.network().setupInfo(info));
  CHECK(info.password[0] == '\0');  // nothing of the password is left to read
  // Unused for ten minutes: it closes by itself.
  s.os.network().beginSetup();
  s.run(3000);
  CHECK(s.dev.setup.up);
  s.run(WifiManager::kSetupTimeoutMs + 5000);
  CHECK(!s.dev.setup.up && !s.os.network().status().setup);
  // Each opening makes a new password.
  const std::string first = s.dev.setup.password;
  s.os.network().beginSetup();
  s.run(3000);
  CHECK(s.dev.setup.starts == 3 && s.dev.setup.password != first);
  s.os.network().cancelSetup();
  s.run(500);
  // A server that cannot start: a message, and nothing left open.
  s.dev.setup.failToStart = true;
  s.os.network().beginSetup();
  s.run(3000);
  CHECK(!s.dev.setup.up && !s.os.network().status().setup &&
        strcmp(s.os.network().status().error, "Couldn't start setup") == 0);
}

TEST(setup_without_a_server_says_so) {
  SimDevice dev(320, 240);
  dev.platform.setEpoch(kEveningEpoch);
  Board board = dev.board();
  board.setup = nullptr;  // a board without phone setup
  Runtime os;
  RuntimeConfig cfg;
  cfg.renderBuffer = dev.buffer.data();
  cfg.renderBufferPixels = dev.buffer.size();
  cfg.serviceThread = false;
  CHECK(os.begin(board, cfg));
  apps::LauncherApp launcher;
  os.install(launcher);
  os.start();
  os.network().beginSetup();
  for (int i = 0; i < 20; ++i) {
    os.step();
    os.serviceStep();
    dev.platform.advance(300);
  }
  CHECK(!os.network().status().setup && strcmp(os.network().status().error, "Setup isn't available here") == 0);
}

TEST(phone_setup_screen_by_touch) {
  SimSession s = offline();
  s.os.launch("settings", "wifi");
  s.run(600);
  const Rect phone = phoneButton(s);
  s.tap(phone.centerX(), phone.centerY());
  CHECK(strcmp(s.os.topScreen()->title(), "Phone setup") == 0);
  s.run(3000);
  SetupInfo info;
  CHECK(s.os.network().setupInfo(info) && s.dev.setup.up);
  const std::vector<Color> showing = s.dev.display.framebuffer();
  // The screen is incremental-safe.
  s.os.invalidateAll();
  s.run(30);
  CHECK(showing == s.dev.display.framebuffer());
  // The phone joins: the screen closes itself and the device says so.
  int status = 0;
  s.dev.setup.request(true, "/join", "ssid=Starlight&pw=moonbeam", &status);
  s.run(10 * 1000);
  CHECK(s.os.network().status().connected && !s.dev.setup.up);
  CHECK(strcmp(s.os.topScreen()->title(), "Phone setup") != 0);
  // Cancel closes the access point; so does leaving with the back button.
  s.os.launch("settings", "wifi");
  s.run(300);
  s.tap(phone.centerX(), phone.centerY());
  s.run(3000);
  CHECK(s.dev.setup.up);
  const Rect cancel = cancelButton(s);
  s.tap(cancel.centerX(), cancel.centerY());
  s.run(500);
  CHECK(!s.dev.setup.up && strcmp(s.os.topScreen()->title(), "Phone setup") != 0);
  s.tap(phone.centerX(), phone.centerY());
  s.run(3000);
  CHECK(s.dev.setup.up);
  const Point back = s.backButton();
  s.tap(back.x, back.y);
  s.run(500);
  CHECK(!s.dev.setup.up);
}

TEST(phone_setup_screen_renders_at_every_size) {
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    SimSession s(sz[0], sz[1], kEveningEpoch, Appearance::Auto, false, false);
    s.os.launch("settings", "wifi");
    s.run(600);
    const Theme &t = s.os.theme();
    Rect r[3];
    layout::column(s.os.contentRect().inset(t.m.margin, t.m.gapS),
                   {layout::px(t.m.row + t.dp(4)), layout::px(t.m.control - t.dp(8)), layout::flex()}, t.m.gapS, r);
    Rect h[3];
    layout::row(r[1], {layout::flex(), layout::px(t.dp(64)), layout::px(t.dp(64))}, t.m.gapS, h);
    s.tap(h[1].centerX(), h[1].centerY());
    s.run(3000);
    CHECK(strcmp(s.os.topScreen()->title(), "Phone setup") == 0 && !s.os.renderPending());
    // The cancel button is on the display and at least 32 dp tall.
    const int rows = t.font.title->lineHeight + t.font.headline->lineHeight + t.font.title->lineHeight + 2 * t.dp(4);
    Rect c[3];
    layout::column(s.os.contentRect().inset(t.m.margin, t.m.gapS),
                   {layout::px(rows + 2 * t.dp(8)), layout::flex(), layout::px(t.m.control)}, t.m.gapM, c);
    CHECK(c[2].bottom() <= sz[1] && c[2].h >= t.dp(32) && c[1].h >= t.font.body->lineHeight * 2);
  }
}
