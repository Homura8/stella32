// JSON streaming, HTTP service, Wi-Fi manager, settings migration and the
// asynchronous (DMA) rendering path.
#include <string.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "host_board.h"
#include "sim_session.h"
#include "stella/app/system_settings.h"
#include "stella/net/http_service.h"
#include "stella/net/json.h"
#include "stella/net/wifi_manager.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {

struct Collector : JsonHandler {
  std::vector<std::string> paths, values;
  void onValue(const JsonPath &path, const JsonValue &v) override {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", path.index());
    paths.push_back(std::string(path.is("a.b[]") ? "a.b[]" : path.is("s") ? "s" : path.is("n") ? "n" : "?") + "#" + buf);
    values.push_back(v.type == JsonType::Number ? std::to_string(v.number)
                     : v.type == JsonType::Bool ? (v.boolean ? "true" : "false")
                     : v.type == JsonType::Null ? "null"
                                                : std::string(v.text));
  }
};

bool parseAll(const std::string &text, JsonHandler &h, size_t piece = 3) {
  JsonParser p(h);
  for (size_t i = 0; i < text.size(); i += piece)
    if (!p.feed(text.data() + i, text.size() - i < piece ? text.size() - i : piece)) return false;
  return p.finish();
}

}  // namespace

TEST(json_paths_values_and_streaming) {
  Collector c;
  CHECK(parseAll(R"({"a":{"b":[1.5,-2e2,true]},"s":"h\u00e9\"llo","n":null,"skip":{"deep":[[{}]]}})", c));
  CHECK_EQ((int)c.values.size(), 5);  // "skip" holds no scalars
  CHECK(c.paths[0] == "a.b[]#0" && c.values[0] == std::to_string(1.5));
  CHECK(c.paths[1] == "a.b[]#1" && c.values[1] == std::to_string(-200.0));
  CHECK(c.paths[2] == "a.b[]#2" && c.values[2] == "true");
  CHECK(c.paths[3] == "s#-1" && c.values[3] == "h\xc3\xa9\"llo");
  CHECK(c.values[4] == "null");
  Collector top;
  CHECK(parseAll("  42 ", top, 1));  // bare top-level number, one byte at a time
  CHECK(top.values.size() == 1 && top.values[0] == std::to_string(42.0));
}

TEST(json_rejects_malformed_and_hostile_input) {
  const char *bad[] = {"{\"a\":}", "[1,]", "{\"a\" 1}", "[1 2]", "{\"a\":tru}", "\"unterminated", "{}x",
                       "[\"\\q\"]", "{\"a\":01x}", "[\"\x01\"]", "{\"a\":[1}", ""};
  for (const char *text : bad) {
    Collector c;
    CHECK(!parseAll(text, c));
  }
  // Nesting beyond the fixed depth is an error, not a stack overflow.
  Collector deep;
  CHECK(!parseAll(std::string(64, '[') + std::string(64, ']'), deep));
  // Oversized strings are truncated, never overflowed.
  struct Long : JsonHandler {
    bool truncated = false;
    void onValue(const JsonPath &, const JsonValue &v) override { truncated = v.truncated && strlen(v.text) < 96; }
  } longValue;
  CHECK(parseAll("{\"k\":\"" + std::string(500, 'x') + "\"}", longValue));
  CHECK(longValue.truncated);
}

TEST(http_service_maps_outcomes) {
  SimHttp http;
  SharedResource lease;
  HttpService svc;
  Collector c;
  CHECK(svc.getJson("https://x/a", c).kind == FetchResult::Unavailable);
  svc.configure(&http, &lease);
  http.routes.push_back({"/ok", "{\"s\":\"fine\"}", 200});
  http.routes.push_back({"/broken", "{\"s\":", 200});
  http.routes.push_back({"/down", "", 503});
  http.routes.push_back({"/big", std::string(40000, ' '), 200});
  FetchResult r = svc.getJson("https://x/ok", c);
  CHECK(r.ok() && c.values.back() == "fine" && r.jobResult() == JobResult::Ok);
  CHECK(svc.getJson("https://x/broken", c).kind == FetchResult::Invalid);
  r = svc.getJson("https://x/down", c);
  CHECK(r.kind == FetchResult::Http && r.status == 503 && strstr(r.message, "503"));
  CHECK(svc.getJson("https://x/big", c).kind == FetchResult::TooLarge);
  http.failWith = kHttpErrTimeout;
  r = svc.getJson("https://x/ok", c);
  CHECK(r.kind == FetchResult::Transport && r.jobResult() == JobResult::Failed);
  // Lease held by another thread for longer than the wait: Busy -> Deferred.
  http.failWith = 0;
  std::atomic<bool> held{false}, release{false};
  std::thread other([&] {
    Lease l(lease, LeasePriority::Primary);
    held = true;
    while (!release) std::this_thread::yield();
  });
  while (!held) std::this_thread::yield();
  FetchLimits quick;
  quick.leaseWaitMs = 10;
  r = svc.getJson("https://x/ok", c, quick);
  CHECK(r.kind == FetchResult::Busy && r.jobResult() == JobResult::Deferred);
  release = true;
  other.join();
}

namespace {

struct WifiRig {
  SimWifi driver;
  MemoryStore store;
  JobScheduler jobs;
  Topic<NetworkStatus> status{1};
  Topic<WifiScan> scan{2};
  WifiManager wifi{status, scan};
  uint32_t now = 1000;
  void poll(int times) {
    for (int i = 0; i < times; ++i) {
      wifi.run(now);
      now += 250;
    }
  }
};

}  // namespace

TEST(wifi_join_save_and_reconnect) {
  WifiRig r;
  r.wifi.configure(&r.driver, &r.store, &r.jobs, nullptr, nullptr);
  CHECK(r.wifi.status().state == WifiState::NotConfigured && r.wifi.status().present);
  r.wifi.join("Starlight", "moonbeam");
  r.poll(5);
  NetworkStatus s = r.wifi.status();
  CHECK(s.state == WifiState::Connected && s.connected && strcmp(s.ip, "192.168.1.42") == 0);
  CHECK(r.driver.timeSyncStarted);
  // Credentials survive a restart (a new manager on the same storage).
  WifiRig again;
  again.store = r.store;
  again.wifi.configure(&again.driver, &again.store, &again.jobs, "fallback", "ignored");
  again.poll(5);
  CHECK(again.wifi.status().connected && strcmp(again.wifi.status().ssid, "Starlight") == 0);
  // A dropped link reconnects after a backoff, without user action.
  const int connects = r.driver.connects;
  r.driver.dropLink();
  r.poll(1);
  CHECK(r.wifi.status().state == WifiState::Disconnected && strstr(r.wifi.status().error, "lost"));
  r.poll(40);  // 10 s of polls: past the 5 s backoff
  CHECK(r.driver.connects > connects && r.wifi.status().connected);
}

TEST(wifi_wrong_password_backs_off_and_forget_clears) {
  WifiRig r;
  r.wifi.configure(&r.driver, &r.store, &r.jobs, nullptr, nullptr);
  r.wifi.join("Starlight", "wrong");
  r.poll(5);
  CHECK(r.wifi.status().state == WifiState::Disconnected && strstr(r.wifi.status().error, "Couldn't join"));
  // Retries continue with growing gaps (driver keeps reporting Failed).
  r.poll(400);  // 100 s
  const int attempts = r.driver.connects;
  CHECK(attempts >= 3 && attempts <= 6);
  r.wifi.forget();
  r.poll(1);
  CHECK(r.wifi.status().state == WifiState::NotConfigured);
  WifiCredentials creds;
  CHECK(loadRecord(&r.store, "wifi", "creds", WifiCredentials::kVersion, &creds, sizeof(creds)) ==
        RecordStatus::Missing);
}

TEST(wifi_scan_dedupes_and_sorts) {
  WifiRig r;
  r.wifi.configure(&r.driver, &r.store, &r.jobs, nullptr, nullptr);
  r.wifi.requestScan();
  r.poll(1);
  CHECK(r.wifi.scan().scanning);
  r.poll(4);
  const WifiScan s = r.wifi.scan();
  CHECK(!s.scanning && s.count == 3 && s.sequence == 1);
  CHECK(strcmp(s.networks[0].ssid, "Starlight") == 0 && s.networks[0].rssi == -52);  // strongest duplicate kept
  CHECK(!s.networks[1].secured && s.networks[2].rssi == -81);
  CHECK_EQ(signalBars(-52), 4);
  CHECK_EQ(signalBars(-90), 0);
}

TEST(wifi_uses_fallback_credentials_once) {
  WifiRig r;
  r.wifi.configure(&r.driver, &r.store, &r.jobs, "Starlight", "moonbeam");
  r.poll(5);
  CHECK(r.wifi.status().connected);
  WifiRig none;
  none.wifi.configure(nullptr, &none.store, &none.jobs, "Starlight", "moonbeam");
  CHECK(none.wifi.status().state == WifiState::Unavailable && !none.wifi.status().present);
}

TEST(settings_v1_records_are_migrated) {
  MemoryStore store;
  struct V1 {
    Appearance appearance;
    uint8_t brightness;
    uint16_t dimAfterSec, sleepAfterSec;
    bool clock24h;
    char timezone[48];
  } v1{Appearance::Dark, 77, 30, 300, false, "JST-9"};
  CHECK(saveRecord(&store, "sys", "settings", 1, &v1, sizeof(v1)));
  SystemSettings s;
  bool migrated = false;
  CHECK(loadSettings(&store, s, &migrated) == RecordStatus::Ok && migrated);
  CHECK(s.appearance == Appearance::Dark && s.brightness == 77 && s.sleepAfterSec == 300 && !s.clock24h);
  CHECK(strcmp(s.timezone, "JST-9") == 0 && s.nightDim);  // new field gets its default
  // The runtime writes the migrated record back as v2.
  SimDevice dev(320, 240);
  dev.storage = store;
  Runtime os;
  RuntimeConfig cfg;
  cfg.renderBuffer = dev.buffer.data();
  cfg.renderBufferPixels = dev.buffer.size();
  cfg.serviceThread = false;
  CHECK(os.begin(dev.board(), cfg));
  CHECK(os.settings().brightness == 77);
  migrated = true;
  CHECK(loadSettings(&dev.storage, s, &migrated) == RecordStatus::Ok && !migrated);
}

namespace {

// Async display double that reads each buffer only when the *next* transfer
// starts (like DMA), so a renderer that reuses an in-flight buffer is caught.
class LateDisplay : public SimDisplay {
 public:
  using SimDisplay::SimDisplay;
  bool supportsAsync() const override { return true; }
  void writePixelsAsync(const Rect &area, Color *pixels) override {
    waitIdle();
    pending_ = area;
    pendingPixels_ = pixels;
    ++asyncWrites;
  }
  void waitIdle() override {
    if (pendingPixels_) writePixels(pending_, pendingPixels_);
    pendingPixels_ = nullptr;
  }
  int asyncWrites = 0;

 private:
  Rect pending_;
  Color *pendingPixels_ = nullptr;
};

}  // namespace

TEST(async_double_buffered_render_matches_sync) {
  SimSession reference(320, 240, kEveningEpoch, Appearance::Auto, false, /*online=*/false);
  reference.os.launch("settings");
  reference.run(200);
  LateDisplay late(320, 240);
  SimDevice dev(320, 240);
  std::vector<Color> second(320 * 20);
  dev.platform.setEpoch(kEveningEpoch);
  Board board = dev.board();
  board.display = &late;
  RuntimeConfig cfg = reference.config;
  cfg.renderBuffer = dev.buffer.data();
  cfg.renderBufferPixels = dev.buffer.size();
  cfg.renderBuffer2 = second.data();
  Runtime os;
  apps::LauncherApp launcher;
  apps::ClockApp clock;
  apps::SettingsApp settings;
  apps::TouchTestApp touch;
  apps::WeatherApp weather;
  CHECK(os.begin(board, cfg));
  os.install(launcher);
  os.install(clock);
  os.install(weather);
  os.install(settings);
  os.install(touch);
  os.start();
  os.launch("settings");
  for (int i = 0; i < 10; ++i) {
    os.step();
    os.serviceStep();
    dev.platform.advance(20);
  }
  CHECK(late.asyncWrites >= 12);  // a full frame (12 chunks) went through the async path
  CHECK(late.framebuffer() == reference.dev.display.framebuffer());
}
