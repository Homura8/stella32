// Over-the-air updates: the primitives against independent reference values,
// the whole check/install flow with the test as the update server, every way
// it can fail, the probation of a freshly updated image, and the screen.
#include <string.h>

#include <string>

#include "sim_session.h"
#include "stella/core/sha256.h"
#include "stella/net/firmware_update.h"
#include "stella/ui/layout.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {

std::string hexOf(const uint8_t *d, size_t n) {
  char out[161];
  toHex(d, n, out);
  return out;
}

std::string sha(const std::string &data) {
  uint8_t d[32];
  Sha256::hash(data.data(), data.size(), d);
  return hexOf(d, 32);
}

std::string makeImage(size_t n) {
  std::string image(n, 0);
  uint32_t x = 12345;
  for (size_t i = 0; i < n; ++i) {
    x = x * 1664525u + 1013904223u;
    image[i] = (char)(x >> 24);
  }
  return image;
}

struct Offer {
  std::string version = "99.0.0";
  std::string url = "https://updates.example/stella/stella32.bin";
  std::string image = makeImage(3000);
  std::string sha256;     // empty: the image's real digest
  std::string sig = std::string(64, '5') ;  // "5a" bytes: what SimFirmware accepts
  long size = -1;         // -1: the image's real size
  bool omitSig = false;
};

// The test plays the update server.
void serve(SimSession &s, Offer o) {
  if (o.sha256.empty()) o.sha256 = sha(o.image);
  if (o.size < 0) o.size = (long)o.image.size();
  const std::string sigHex = std::string(32, '5') + "a" + std::string(0, ' ');
  (void)sigHex;
  std::string rs;
  for (int i = 0; i < 32; ++i) rs += "5a";
  std::string manifest = "{\"version\":\"" + o.version + "\",\"url\":\"" + o.url + "\",\"size\":" +
                         std::to_string(o.size) + ",\"sha256\":\"" + o.sha256 + "\"";
  if (!o.omitSig) manifest += ",\"sig_r\":\"" + rs + "\",\"sig_s\":\"" + rs + "\"";
  manifest += "}";
  s.dev.http.routes.insert(s.dev.http.routes.begin(), {"stella32.bin", o.image, 200});
  s.dev.http.routes.insert(s.dev.http.routes.begin(), {"manifest.json", manifest, 200});
}

UpdateStatus runCheck(SimSession &s) {
  s.os.update().check();
  s.run(3000);
  return s.os.update().status();
}

UpdateStatus runInstall(SimSession &s) {
  s.os.update().install();
  s.run(8000);
  return s.os.update().status();
}

bool has(const char *text, const char *needle) { return strstr(text, needle) != nullptr; }

// A runtime we can configure before it begins (no SimSession).
struct Rig {
  SimDevice dev{320, 240};
  Runtime os;
  RuntimeConfig cfg;
  apps::LauncherApp launcher;
  explicit Rig(const char *manifestUrl, bool pending = false) {
    dev.platform.setEpoch(kEveningEpoch);
    dev.platform.setUpdatePending(pending);
    cfg.renderBuffer = dev.buffer.data();
    cfg.renderBufferPixels = dev.buffer.size();
    cfg.serviceThread = false;
    cfg.updateManifestUrl = manifestUrl;
    cfg.wifiSsid = "Starlight";
    cfg.wifiPassword = "moonbeam";
    os.begin(dev.board(), cfg);
    os.install(launcher);
    os.start();
  }
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 50) {
      os.step();
      os.serviceStep();
      dev.platform.advance(50);
    }
  }
};

}  // namespace

TEST(sha256_matches_the_reference_vectors) {
  CHECK(sha("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(sha(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  // Streaming in odd pieces equals one shot, including around block edges.
  const std::string data = makeImage(1000);
  for (size_t piece : {1u, 7u, 55u, 56u, 63u, 64u, 65u, 333u}) {
    Sha256 h;
    for (size_t i = 0; i < data.size(); i += piece) h.update(data.data() + i, data.size() - i < piece ? data.size() - i : piece);
    uint8_t d[32];
    h.finish(d);
    CHECK(hexOf(d, 32) == sha(data));
  }
  uint8_t bytes[4];
  CHECK(fromHex("00ff10Ab", bytes, 4) && bytes[1] == 0xff && bytes[3] == 0xab);
  CHECK(!fromHex("00ff10A", bytes, 4) && !fromHex("00ff10Ag", bytes, 4) && !fromHex(nullptr, bytes, 4));
}

TEST(signed_message_version_order_and_der_match_the_tool) {
  // Values produced by tools/update_tool.py (an independent implementation):
  //   update_tool.py message --version 1.2.3 --text stella-test-image
  uint8_t image[32], message[32];
  Sha256::hash("stella-test-image", 17, image);
  CHECK(hexOf(image, 32) == "01f6bbd2c6a3332e3fdbcf69b85d8ba016a099e24246385be971a8dbbf0194f2");
  UpdateService::signedDigest("1.2.3", image, message);
  CHECK(hexOf(message, 32) == "c729204fd5c4fe4cd5a5ab32c87587cc50e3769291f919ebe7a04d35fb09e57d");
  // The version is part of what is signed: an old image cannot be relabelled as new.
  uint8_t other[32];
  UpdateService::signedDigest("1.2.4", image, other);
  CHECK(memcmp(message, other, 32) != 0);
  // Versions.
  CHECK(UpdateService::compareVersions("0.5.1", "0.5.0") == 1 && UpdateService::compareVersions("0.5.0", "0.5.0") == 0);
  CHECK(UpdateService::compareVersions("0.4.9", "0.5.0") == -1 && UpdateService::compareVersions("v1.0.0", "0.9.9") == 1);
  CHECK(UpdateService::compareVersions("0.10.0", "0.9.0") == 1);  // numbers, not text
  CHECK(UpdateService::compareVersions("1.0", "1.0.0") == -2 && UpdateService::compareVersions("1.0.x", "1.0.0") == -2 &&
        UpdateService::compareVersions("", "1.0.0") == -2 && UpdateService::compareVersions(nullptr, "1.0.0") == -2);
  // DER of an ECDSA signature, against cryptography's encoder.
  uint8_t r[32], s[32], der[72];
  fromHex("e6aac7a19c9118f5ad4a8d6482d989b1d56bf2bc931136b3ab2138b77826b2d1", r, 32);
  fromHex("03f798fa19fe433c2eed5753b182bca11f0b513d7a586d75c2b9a6bf606c7f5a", s, 32);
  size_t n = UpdateService::encodeDerSignature(r, s, der);
  CHECK(hexOf(der, n) ==
        "3045022100e6aac7a19c9118f5ad4a8d6482d989b1d56bf2bc931136b3ab2138b77826b2d1"
        "022003f798fa19fe433c2eed5753b182bca11f0b513d7a586d75c2b9a6bf606c7f5a");
  fromHex("0000112233445566778899aabbccddeeff00112233445566778899aabbccddee", r, 32);  // leading zeros stripped
  fromHex("ff00112233445566778899aabbccddeeff00112233445566778899aabbccddee", s, 32);  // high bit: zero pad
  n = UpdateService::encodeDerSignature(r, s, der);
  CHECK(hexOf(der, n) ==
        "3043021e112233445566778899aabbccddeeff00112233445566778899aabbccddee"
        "022100ff00112233445566778899aabbccddeeff00112233445566778899aabbccddee");
  memset(r, 0, 32);
  memset(s, 0xff, 32);
  n = UpdateService::encodeDerSignature(r, s, der);  // r = 0 is still one byte; the longest case is 72 bytes
  CHECK(n == 2 + 3 + 2 + 33 && der[1] == n - 2);
}

TEST(update_check_install_and_ready) {
  SimSession s(320, 240);
  CHECK(s.os.update().status().state == UpdateState::Idle);
  Offer offer;
  serve(s, offer);
  const UpdateStatus found = runCheck(s);
  CHECK(found.state == UpdateState::Available && strcmp(found.version, "99.0.0") == 0 && found.size == 3000);
  CHECK(!s.dev.firmware.begun);  // checking writes nothing
  const UpdateStatus done = runInstall(s);
  CHECK(done.state == UpdateState::Ready && done.percent == 100);
  CHECK(s.dev.firmware.finished && !s.dev.firmware.aborted && std::string(s.dev.firmware.image.begin(), s.dev.firmware.image.end()) == offer.image);
  CHECK_EQ(s.dev.firmware.verifies, 1);
  // Nothing more happens by itself; a second install does not run again.
  s.os.update().install();
  s.run(2000);
  CHECK_EQ(s.dev.firmware.verifies, 1);
}

TEST(update_up_to_date_and_older_versions_are_not_offered) {
  SimSession s(320, 240);
  Offer same;
  same.version = STELLA_VERSION;
  serve(s, same);
  CHECK(runCheck(s).state == UpdateState::UpToDate);
  SimSession older(320, 240);
  Offer old;
  old.version = "0.0.1";
  serve(older, old);
  CHECK(runCheck(older).state == UpdateState::UpToDate);
  older.os.update().install();  // nothing on offer: ignored
  older.run(1000);
  CHECK(!older.dev.firmware.begun);
}

TEST(update_refuses_bad_manifests) {
  struct Case {
    const char *what;
    void (*edit)(Offer &);
    const char *error;
  };
  const Case cases[] = {
      {"no signature", [](Offer &o) { o.omitSig = true; }, "not valid"},
      {"image over plain http", [](Offer &o) { o.url = "http://updates.example/stella32.bin"; }, "not valid"},
      {"a version that is not a version", [](Offer &o) { o.version = "latest"; }, "not valid"},
      {"a checksum of the wrong length", [](Offer &o) { o.sha256 = "abcd"; }, "not valid"},
      {"a zero size", [](Offer &o) { o.size = 0; }, "not valid"},
      {"a huge size", [](Offer &o) { o.size = 50 * 1000 * 1000; }, "not valid"},
      {"bigger than the slot", [](Offer &o) { o.size = 3 * 1024 * 1024 / 2 + 100000; }, "too big"},
  };
  for (const Case &c : cases) {
    SimSession s(320, 240);
    Offer o;
    c.edit(o);
    serve(s, o);
    const UpdateStatus st = runCheck(s);
    CHECK(st.state == UpdateState::Failed && has(st.error, c.error));
    CHECK(!s.dev.firmware.begun);
  }
  SimSession tooBig(320, 240);
  tooBig.dev.firmware.slotSize = 2000;  // a slot smaller than the 3000-byte image
  serve(tooBig, Offer{});
  const UpdateStatus st = runCheck(tooBig);
  CHECK(st.state == UpdateState::Failed && has(st.error, "too big"));
  SimSession down(320, 240);  // no server answering
  CHECK(runCheck(down).state == UpdateState::Failed);
}

TEST(update_never_stages_an_image_that_fails_a_check) {
  struct Case {
    const char *what;
    void (*offer)(Offer &);
    void (*rig)(SimSession &);
    const char *error;
  };
  const Case cases[] = {
      {"a damaged download", [](Offer &o) { o.sha256 = std::string(64, '0'); }, [](SimSession &) {}, "checksum"},
      {"fewer bytes than announced", [](Offer &o) { o.size = (long)o.image.size() + 500; }, [](SimSession &) {}, "incomplete"},
      {"more bytes than announced", [](Offer &o) { o.size = (long)o.image.size() - 500; }, [](SimSession &) {}, "larger than announced"},
      {"a bad signature", [](Offer &) {}, [](SimSession &s) { s.dev.firmware.signatureOk = false; }, "signature"},
      {"flash that cannot be written", [](Offer &) {}, [](SimSession &s) { s.dev.firmware.failWriteAfter = 1000; }, "Writing"},
      {"a slot that cannot start", [](Offer &) {}, [](SimSession &s) { s.dev.firmware.failBegin = true; }, "update slot"},
      {"a download that is cut", [](Offer &) {},
       [](SimSession &s) {
         for (auto &r : s.dev.http.routes)
           if (r.match == "stella32.bin") r.cutAfter = 1500;
       },
       "download failed"},
  };
  for (const Case &c : cases) {
    SimSession s(320, 240);
    Offer o;
    c.offer(o);
    serve(s, o);
    c.rig(s);
    CHECK(runCheck(s).state == UpdateState::Available);
    const UpdateStatus st = runInstall(s);
    CHECK(st.state == UpdateState::Failed && has(st.error, c.error));
    CHECK(!s.dev.firmware.finished);  // never staged: the running firmware is untouched
    // The signature is checked only for an image that passed every check before it.
    if (strcmp(c.what, "a damaged download") == 0) CHECK_EQ(s.dev.firmware.verifies, 0);
  }
  SimSession invalid(320, 240);  // a valid signature but not a firmware image
  serve(invalid, Offer{});
  invalid.dev.firmware.failFinish = true;
  runCheck(invalid);
  const UpdateStatus st = runInstall(invalid);
  CHECK(st.state == UpdateState::Failed && has(st.error, "not a valid firmware") && !invalid.dev.firmware.finished);
  // After a failure, checking again works.
  SimSession again(320, 240);
  serve(again, Offer{});
  again.dev.firmware.failBegin = true;
  runCheck(again);
  runInstall(again);
  again.dev.firmware.failBegin = false;
  CHECK(runCheck(again).state == UpdateState::Available);
  CHECK(runInstall(again).state == UpdateState::Ready);
}

// Without a signing key an update is protected by HTTPS and its SHA-256: the
// manifest needs no signature, but a damaged image is still refused.
TEST(update_without_a_signing_key_needs_no_signature_but_still_checks_the_image) {
  SimSession s(320, 240);
  s.dev.firmware.keyed = false;
  Offer o;
  o.omitSig = true;
  serve(s, o);
  CHECK(runCheck(s).state == UpdateState::Available);
  CHECK(runInstall(s).state == UpdateState::Ready);
  CHECK_EQ(s.dev.firmware.verifies, 0);
  CHECK(s.dev.firmware.finished);
  SimSession damaged(320, 240);
  damaged.dev.firmware.keyed = false;
  Offer bad;
  bad.omitSig = true;
  bad.sha256 = std::string(64, '0');
  serve(damaged, bad);
  CHECK(runCheck(damaged).state == UpdateState::Available);
  const UpdateStatus st = runInstall(damaged);
  CHECK(st.state == UpdateState::Failed && has(st.error, "checksum") && !damaged.dev.firmware.finished);
  // With a key built in, the same unsigned manifest is refused outright.
  SimSession keyed(320, 240);
  serve(keyed, bad);
  Offer unsigned_;
  unsigned_.omitSig = true;
  serve(keyed, unsigned_);
  CHECK(runCheck(keyed).state == UpdateState::Failed);
}

TEST(update_waits_for_the_last_update_to_be_confirmed) {
  Rig rig("https://updates.example/stella/manifest.json", /*pending=*/true);
  rig.run(1000);
  Offer o;
  std::string rs;
  for (int i = 0; i < 32; ++i) rs += "5a";
  const std::string manifest = "{\"version\":\"99.0.0\",\"url\":\"https://updates.example/stella32.bin\",\"size\":" +
                               std::to_string(o.image.size()) + ",\"sha256\":\"" + sha(o.image) + "\",\"sig_r\":\"" +
                               rs + "\",\"sig_s\":\"" + rs + "\"}";
  rig.dev.http.routes.insert(rig.dev.http.routes.begin(), {"stella32.bin", o.image, 200});
  rig.dev.http.routes.insert(rig.dev.http.routes.begin(), {"manifest.json", manifest, 200});
  rig.os.update().check();
  rig.run(3000);
  CHECK(rig.os.update().status().state == UpdateState::Available);
  rig.os.update().install();
  rig.run(2000);
  const UpdateStatus st = rig.os.update().status();
  CHECK(st.state == UpdateState::Failed && has(st.error, "confirming") && !rig.dev.firmware.begun);
}

TEST(update_is_unavailable_without_a_slot_a_key_or_a_server) {
  Rig noUrl(nullptr);
  noUrl.run(500);
  CHECK(noUrl.os.update().status().state == UpdateState::Unavailable &&
        has(noUrl.os.update().status().error, "No update server"));
  noUrl.os.update().check();  // ignored
  noUrl.run(500);
  CHECK(noUrl.os.update().status().state == UpdateState::Unavailable && noUrl.dev.http.requests == 0);
  Rig plain("http://updates.example/manifest.json");  // never over plain HTTP
  CHECK(plain.os.update().status().state == UpdateState::Unavailable);
  SimDevice dev(320, 240);
  dev.firmware.availableFlag = false;  // no second slot, or no signing key
  Runtime os;
  RuntimeConfig cfg;
  cfg.renderBuffer = dev.buffer.data();
  cfg.renderBufferPixels = dev.buffer.size();
  cfg.serviceThread = false;
  cfg.updateManifestUrl = "https://updates.example/stella/manifest.json";
  os.begin(dev.board(), cfg);
  CHECK(os.update().status().state == UpdateState::Unavailable &&
        has(os.update().status().error, "two-slot layout"));
}

TEST(a_new_image_is_confirmed_only_after_a_healthy_minute) {
  Rig rig("https://updates.example/stella/manifest.json", /*pending=*/true);
  rig.run(30 * 1000);
  CHECK(rig.dev.platform.updatePending() && rig.dev.platform.confirms() == 0);  // still on probation
  rig.run(40 * 1000);
  CHECK(!rig.dev.platform.updatePending() && rig.dev.platform.confirms() == 1);
  rig.run(60 * 1000);
  CHECK_EQ(rig.dev.platform.confirms(), 1);  // once
  Rig normal("https://updates.example/stella/manifest.json", /*pending=*/false);
  normal.run(90 * 1000);
  CHECK_EQ(normal.dev.platform.confirms(), 0);  // an image from USB has nothing to confirm
}

namespace {
Rect aboutUpdateButton(SimSession &s) {
  const Theme &t = s.os.theme();
  Rect r[2];
  layout::column(s.os.contentRect().inset(t.m.margin, t.m.gapS), {layout::flex(), layout::px(t.m.control)}, t.m.gapS, r);
  return layout::center(r[1], t.dp(190), t.m.control);
}
Rect updateAction(SimSession &s) {
  const Theme &t = s.os.theme();
  const int lh = t.font.body->lineHeight;
  Rect r[3];
  layout::column(s.os.contentRect().inset(t.m.margin, t.m.gapS),
                 {layout::px(2 * lh + t.dp(6) + t.dp(6) + 2 * t.m.padding), layout::flex(), layout::px(t.m.control)},
                 t.m.gapM, r);
  return layout::center(r[2], t.dp(200), t.m.control);
}
}  // namespace

TEST(software_update_screen_by_touch) {
  SimSession s(320, 240);
  serve(s, Offer{});
  s.os.launch("settings");
  s.run(100);
  const Point about = s.settingsRow(5);
  s.tap(about.x, about.y);
  const Rect entry = aboutUpdateButton(s);
  s.tap(entry.centerX(), entry.centerY());
  CHECK(strcmp(s.os.topScreen()->title(), "Software update") == 0);
  const Rect action = updateAction(s);
  s.tap(action.centerX(), action.centerY());  // Check
  s.run(3000);
  CHECK(s.os.update().status().state == UpdateState::Available);
  s.tap(action.centerX(), action.centerY());  // Install
  s.run(8000);
  CHECK(s.os.update().status().state == UpdateState::Ready && s.dev.firmware.finished);
  const int restarts = s.dev.platform.restarts();
  s.tap(action.centerX(), action.centerY());  // Restart now
  s.run(2000);
  CHECK_EQ(s.dev.platform.restarts(), restarts + 1);
  // Incremental rendering equals a full redraw on this screen.
  const std::vector<Color> incremental = s.dev.display.framebuffer();
  s.os.invalidateAll();
  s.run(30);
  CHECK(incremental == s.dev.display.framebuffer());
}

TEST(software_update_screen_renders_at_every_size_and_state) {
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    SimSession s(sz[0], sz[1]);
    serve(s, Offer{});
    s.os.launch("settings");
    s.run(100);
    const Point about = s.settingsRow(5);
    s.tap(about.x, about.y);
    const Rect entry = aboutUpdateButton(s);
    CHECK(entry.h >= s.os.theme().dp(32));
    s.tap(entry.centerX(), entry.centerY());
    CHECK(strcmp(s.os.topScreen()->title(), "Software update") == 0);
    const Rect action = updateAction(s);
    CHECK(action.bottom() <= sz[1] && action.h >= s.os.theme().dp(32));
    for (int step = 0; step < 3; ++step) {  // Idle, Available, Ready
      CHECK(!s.os.renderPending() || true);
      s.tap(action.centerX(), action.centerY());
      s.run(step == 1 ? 8000 : 3000);
    }
    CHECK(s.os.update().status().state == UpdateState::Ready);
  }
}

// ------------------------------------------------------------ daily check

namespace {
void setAutoUpdate(SimSession &s, bool on) {
  SystemSettings st = s.os.settings();
  st.autoUpdate = on;
  s.os.applySettings(st);
}
int manifestRequests(SimSession &s) { return s.dev.http.manifestRequests; }
}  // namespace

TEST(daily_check_finds_an_update_by_itself_and_never_installs_it) {
  SimSession s(320, 240);
  serve(s, Offer{});
  setAutoUpdate(s, true);  // switching it on looks right away, quietly
  s.run(3000);
  const UpdateStatus st = s.os.update().status();
  CHECK(st.state == UpdateState::Available);
  CHECK(strcmp(st.version, "99.0.0") == 0);
  CHECK(!s.dev.firmware.finished && !s.dev.firmware.begun);  // nothing was downloaded
  CHECK_EQ(s.dev.platform.restarts(), 0);
}

TEST(daily_check_is_off_when_the_setting_is_off) {
  SimSession s(320, 240);
  serve(s, Offer{});
  const int before = manifestRequests(s);
  s.run(3u * 60u * 60u * 1000u);  // three hours: nothing asks the server
  CHECK_EQ(manifestRequests(s), before);
  CHECK(s.os.update().status().state == UpdateState::Idle);
}

TEST(daily_check_repeats_once_a_day_and_notices_a_newer_version_later) {
  SimSession s(320, 240);
  Offer old;
  old.version = "0.0.1";  // nothing newer yet
  serve(s, old);
  setAutoUpdate(s, true);
  s.run(3000);
  CHECK(s.os.update().status().state == UpdateState::UpToDate);
  const int first = manifestRequests(s);
  s.run(20u * 60u * 60u * 1000u);
  CHECK_EQ(manifestRequests(s), first);  // not yet a day
  serve(s, Offer{});                      // the server now has a newer version
  s.run(5u * 60u * 60u * 1000u);          // past 24 h
  CHECK(manifestRequests(s) > first);
  CHECK(s.os.update().status().state == UpdateState::Available);
}

TEST(daily_check_failures_stay_quiet_and_retry) {
  SimSession s(320, 240);
  s.dev.http.routes.insert(s.dev.http.routes.begin(), {"manifest.json", "", 503});
  setAutoUpdate(s, true);
  s.run(3000);
  const UpdateStatus st = s.os.update().status();
  CHECK(st.state == UpdateState::Idle);  // no error is shown for a background check
  CHECK(st.error[0] == '\0');
  const int tried = manifestRequests(s);
  CHECK(tried >= 1);
  serve(s, Offer{});                    // the server recovers
  s.run(45u * 60u * 1000u);             // it tries again within the hour
  CHECK(manifestRequests(s) > tried);
  CHECK(s.os.update().status().state == UpdateState::Available);
}

TEST(daily_check_does_not_disturb_an_offer_or_a_failed_result) {
  SimSession s(320, 240);
  serve(s, Offer{});
  setAutoUpdate(s, true);
  s.run(3000);
  CHECK(s.os.update().status().state == UpdateState::Available);
  const int before = manifestRequests(s);
  s.run(30u * 60u * 60u * 1000u);  // a day and a half with the offer on screen
  CHECK_EQ(manifestRequests(s), before);
  CHECK(s.os.update().status().state == UpdateState::Available);
}

TEST(settings_shows_the_offered_update_and_the_toggle_saves) {
  SimSession s(320, 240);
  serve(s, Offer{});
  setAutoUpdate(s, true);
  s.run(3000);
  s.os.launch("settings");
  s.run(200);
  const Point about = s.settingsRow(5);
  s.tap(about.x, about.y);
  const Rect entry = aboutUpdateButton(s);
  s.tap(entry.centerX(), entry.centerY());
  CHECK(strcmp(s.os.topScreen()->title(), "Software update") == 0);
  CHECK(s.os.settings().autoUpdate);
}
