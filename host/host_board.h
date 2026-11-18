#pragma once
// Host (Linux) board: deterministic stand-ins for every HAL interface, used by
// the unit tests and the screenshot simulator. Time only moves when told to.
#include <atomic>
#include <functional>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "stella/hal/hal.h"
#include "stella/net/remote.h"

namespace stella {
namespace host {

class SimDisplay : public Display {
 public:
  SimDisplay(int width, int height) : w_(width), h_(height), pixels_((size_t)width * height, 0) {}
  int16_t width() const override { return (int16_t)w_; }
  int16_t height() const override { return (int16_t)h_; }
  void writePixels(const Rect &area, const Color *pixels) override;
  void setBacklight(uint8_t level) override { backlight_ = level; }
  // Panel trim is recorded so tests can check calibration reaches the panel.
  bool supportsPanelTrim() const override { return trimmable; }
  void setPanelTrim(const PanelTrim &trim) override {
    lastTrim = trim;
    ++trims;
  }
  bool trimmable = true;
  PanelTrim lastTrim;
  int trims = 0;

  Color pixel(int x, int y) const { return pixels_[(size_t)y * w_ + x]; }
  const std::vector<Color> &framebuffer() const { return pixels_; }
  uint8_t backlight() const { return backlight_; }
  uint64_t pixelsWritten() const { return written_; }
  uint32_t writes() const { return writes_; }
  void resetCounters() { written_ = 0; writes_ = 0; }
  bool writePpm(const std::string &path) const;

 private:
  int w_, h_;
  std::vector<Color> pixels_;
  uint8_t backlight_ = 0;
  uint64_t written_ = 0;
  uint32_t writes_ = 0;
};

class ScriptedTouch : public TouchInput {
 public:
  bool sample(TouchSample &out) override {
    out = current_;
    return true;
  }
  void down(int x, int y) {
    current_.pressed = true;
    current_.x = (int16_t)x;
    current_.y = (int16_t)y;
    current_.pressure = 900;
  }
  void up() { current_.pressed = false; }

 private:
  TouchSample current_;
};

class SimPlatform : public Platform {
 public:
  const char *boardName() const override { return "Host simulator"; }
  uint32_t millis() const override { return ms_; }
  bool wallClock(int64_t &epoch) const override {
    if (!timeKnown_) return false;
    epoch = epochBase_ + (int64_t)((ms_ - epochAtMs_) / 1000u);
    return true;
  }
  MemoryStats memory() const override { return MemoryStats{142000, 118000, 98000}; }
  ResetCause resetCause() const override { return ResetCause::PowerOn; }
  void restart() override { restarts_.fetch_add(1); }
  void *retainedMemory(size_t &size) override {
    size = sizeof(retained_);
    return retained_;
  }

  bool updatePending() const override { return pending_; }
  void confirmUpdate() override {
    pending_ = false;
    ++confirms_;
  }
  void setUpdatePending(bool pending) { pending_ = pending; }
  int confirms() const { return confirms_; }

  void advance(uint32_t ms) { ms_.fetch_add(ms); }
  void setMillis(uint32_t ms) { ms_ = ms; }
  void setEpoch(int64_t epoch) {
    timeKnown_ = true;
    epochBase_ = epoch;
    epochAtMs_ = ms_;
  }
  void clearTime() { timeKnown_ = false; }
  int restarts() const { return restarts_.load(); }

 private:
  std::atomic<uint32_t> ms_{1000};  // read by the service thread
  bool timeKnown_ = false;
  int64_t epochBase_ = 0;
  uint32_t epochAtMs_ = 0;
  std::atomic<int> restarts_{0};
  bool pending_ = false;
  int confirms_ = 0;
  alignas(8) unsigned char retained_[64] = {};
};

class MemoryStore : public KeyValueStore {
 public:
  int read(const char *ns, const char *key, void *out, size_t capacity) override;
  bool write(const char *ns, const char *key, const void *data, size_t size) override;
  bool erase(const char *ns, const char *key) override;
  bool eraseAll() override {
    values_.clear();
    ++eraseAlls_;
    return true;
  }
  size_t count() const { return values_.size(); }
  int eraseAlls() const { return eraseAlls_; }
  size_t writes() const { return writes_; }
  std::vector<uint8_t> *raw(const char *ns, const char *key);  // for corruption tests

 private:
  std::map<std::string, std::vector<uint8_t>> values_;
  size_t writes_ = 0;
  int eraseAlls_ = 0;
};

// Simulated access points: joining succeeds with the right password after a
// few polls; wrong passwords and unknown networks fail.
class SimWifi : public WifiDriver {
 public:
  struct Ap {
    std::string ssid, password;
    int8_t rssi;
  };
  std::vector<Ap> aps{{"Starlight", "moonbeam", -52}, {"Observatory", "", -63}, {"Neighbour 5G", "x", -81},
                      {"Starlight", "moonbeam", -70}};
  int connectPolls = 3;  // link() calls before an attempt resolves
  int scanPolls = 2;
  bool timeSyncStarted = false;
  int connects = 0;

  bool begin() override { return true; }
  bool connect(const char *ssid, const char *password) override;
  void disconnect() override { link_ = WifiLink::Idle; }
  WifiLink link() override;
  int8_t rssi() override { return -55; }
  void address(char *out, size_t capacity) override;
  bool startScan() override {
    scanLeft_ = scanPolls;
    return true;
  }
  int scanResults(WifiNetwork *out, int capacity) override;
  void startTimeSync() override { timeSyncStarted = true; }
  void dropLink() { link_ = WifiLink::Idle; }

 private:
  WifiLink link_ = WifiLink::Idle;
  bool willSucceed_ = false;
  int pollsLeft_ = 0, scanLeft_ = -1;
};

// Canned HTTP responses keyed by a URL substring.
class SimHttp : public HttpClient {
 public:
  struct Route {
    std::string match, body;
    int status;
    size_t cutAfter = (size_t)-1;  // deliver this many bytes, then fail with a timeout
  };
  std::vector<Route> routes;
  int requests = 0;
  int failWith = 0;  // non-zero: every request returns this
  std::string lastUrl;
  std::string lastRejection;  // why the last request was refused with 400 (see validateUrl)
  // The simulated servers are strict: a request a real server would refuse
  // (cut off, misspelt, out of range) is answered 400 here too. Empty = fine.
  static std::string validateUrl(const char *url);
  int manifestRequests = 0;  // requests whose URL mentions manifest.json
  int get(const char *url, uint32_t timeoutMs, size_t maxBodyBytes, HttpSink &sink) override;
};

// The other app slot: records the image written to it and can be told to fail
// at each step. The "signature" check passes when r and s are all 0x5a.
class SimFirmware : public FirmwareSlot {
 public:
  bool availableFlag = true, failBegin = false, failFinish = false;
  bool keyed = true;  // a signing key is built in: signatures are required
  int failWriteAfter = -1;  // fail the write that would pass this many bytes (-1: never)
  size_t slotSize = 1u << 20;
  std::vector<uint8_t> image;
  bool begun = false, finished = false, aborted = false, signatureOk = true;
  int verifies = 0;
  std::function<void(const SimFirmware &)> onEvent;  // tests: look around at each begin / write / finish

  bool available() override { return availableFlag; }
  bool requiresSignature() override { return keyed; }
  size_t capacity() override { return slotSize; }
  bool begin(size_t size) override {
    if (failBegin || size > slotSize) return false;
    image.clear();
    begun = true;
    finished = aborted = false;
    if (onEvent) onEvent(*this);
    return true;
  }
  bool write(const uint8_t *data, size_t size) override {
    if (!begun) return false;
    if (onEvent) onEvent(*this);
    if (failWriteAfter >= 0 && image.size() + size > (size_t)failWriteAfter) return false;
    image.insert(image.end(), data, data + size);
    return true;
  }
  bool finish() override {
    if (onEvent) onEvent(*this);
    if (failFinish) return false;
    finished = true;
    return true;
  }
  void abort() override {
    aborted = true;
    begun = false;
  }
  bool verify(const uint8_t[32], const uint8_t r[32], const uint8_t s[32]) override {
    ++verifies;
    for (int i = 0; i < 32; ++i)
      if (r[i] != 0x5a || s[i] != 0x5a) return false;
    return signatureOk;
  }
};

// The phone-setup access point: tests play the phone with request().
class SimSetupServer : public SetupServer {
 public:
  bool failToStart = false;
  bool up = false;
  std::string ssid, password;
  int polls = 0, starts = 0;

  bool begin(const char *apSsid, const char *apPassword, SetupHandler &h) override {
    if (failToStart) return false;
    up = true;
    ++starts;
    ssid = apSsid;
    password = apPassword;
    handler_ = &h;
    return true;
  }
  void poll() override { ++polls; }
  void end() override {
    up = false;
    handler_ = nullptr;
  }
  void address(char *out, size_t capacity) override { snprintf(out, capacity, "192.168.4.1"); }

  // What a phone gets for a request ("" if the access point is down).
  std::string request(bool post, const char *path, const std::string &body = "", int *status = nullptr);

 private:
  SetupHandler *handler_ = nullptr;
};

// Records the tone sequence so tests can check alert chimes.
class SimSound : public Sound {
 public:
  void tone(uint16_t hz) override {
    if (hz == current_) return;
    current_ = hz;
    if (hz) ++tones_;
  }
  void setVolume(uint8_t percent) override { volume_ = percent; }
  uint16_t current() const { return current_; }
  int tones() const { return tones_; }  // tones started so far
  uint8_t volume() const { return volume_; }

 private:
  uint16_t current_ = 0;
  int tones_ = 0;
  uint8_t volume_ = 255;  // until the runtime sets it
};

// A status LED with a ground truth: how it is really wired. What the person
// would see lit is computed from that, so tests can play the person in
// "Find my LED" and check what normal operation shows.
class SimLed : public Led {
 public:
  LedWiring truth{{4, 16, 17}, true};  // what is soldered on the board
  LedWiring wiring;                    // what the firmware configured
  int configures = 0, sets = 0, probes = 0;
  uint8_t probePin = kNoPin;
  bool probeActiveLow = true;
  uint8_t r = 0, g = 0, b = 0;  // the last colour set

  bool configure(const LedWiring &w) override {
    wiring = w;
    ++configures;
    r = g = b = 0;
    return true;
  }
  void set(uint8_t red, uint8_t green, uint8_t blue) override {
    r = red;
    g = green;
    b = blue;
    ++sets;
  }
  int candidatePins(uint8_t *out, int capacity) const override {
    static const uint8_t kPins[] = {4, 16, 17};
    int n = 0;
    for (uint8_t p : kPins)
      if (n < capacity) out[n++] = p;
    return n;
  }
  void probe(uint8_t pin, bool activeLow) override {
    probePin = pin;
    probeActiveLow = activeLow;
    ++probes;
  }
  bool probing() const { return probePin != kNoPin; }
  // Does real channel c (0 red, 1 green, 2 blue) light during the current probe?
  // Only the probed pin is driven (low or high); a low pin lights an active-low
  // LED and a high pin an active-high one.
  bool probeLit(int c) const {
    if (probePin == kNoPin || truth.pin[c] == kNoPin || truth.pin[c] != probePin) return false;
    return probeActiveLow == truth.inverted;
  }
  // The brightness (0..255) real channel c shows in normal operation, given the
  // wiring the firmware configured.
  int shown(int c) const {
    if (truth.pin[c] == kNoPin) return 0;
    for (int i = 0; i < 3; ++i) {
      if (wiring.pin[i] != truth.pin[c]) continue;
      if (wiring.inverted != truth.inverted) return 0;  // wrong polarity: it would be lit when it should not
      return i == 0 ? r : i == 1 ? g : b;
    }
    return 0;
  }
};

// An LDR beside the screen: the room's light, the backlight's own glow and a
// polarity, all under the test's control.
class SimLight : public LightSensor {
 public:
  const SimDisplay *display = nullptr;
  float ambient = 0.f;    // 0 dark room .. 1 bright room
  int dark = 700;         // reading in the dark, backlight off
  int range = 2200;       // change from dark to a bright room
  int self = 420;         // change with the backlight at full
  bool inverted = false;  // reads LOWER in light
  int reads = 0;
  uint16_t read() override {
    ++reads;
    const float screen = display ? self * (display->backlight() / 255.f) : 0.f;
    const float v = dark + (inverted ? -1.f : 1.f) * (range * ambient + screen);
    return (uint16_t)(v < 0.f ? 0.f : v > 4095.f ? 4095.f : v);
  }
};

// A Bluetooth keyboard that records what was sent. A test plays the computer:
// connect() / disconnect().
class SimHid : public HidDevice {
 public:
  bool present = true;
  bool failStart = false;
  int starts = 0, stops = 0, bonds = 0;
  std::string name;
  struct Sent {
    bool consumer;
    uint8_t modifiers;
    uint16_t code;  // a consumer usage, or a keyboard usage
  };
  std::vector<Sent> sent;

  bool available() const override { return present; }
  bool start(const char *n) override {
    ++starts;
    name = n ? n : "";
    state_ = failStart ? HidState::Failed : HidState::Advertising;
    return !failStart;
  }
  void stop() override {
    ++stops;
    state_ = HidState::Off;
  }
  HidState state() const override { return state_; }
  const char *error() const override { return state_ == HidState::Failed ? "Bluetooth did not start" : ""; }
  bool sendKey(uint8_t modifiers, uint8_t keycode) override {
    if (state_ != HidState::Connected) return false;
    sent.push_back({false, modifiers, keycode});
    return true;
  }
  bool sendConsumer(uint16_t usage) override {
    if (state_ != HidState::Connected) return false;
    sent.push_back({true, 0, usage});
    return true;
  }
  int bondCount() const override { return bonds; }
  void forgetBonds() override { bonds = 0; }
  // The computer pairs and connects / goes away.
  void connect() {
    if (state_ == HidState::Advertising) {
      state_ = HidState::Connected;
      bonds = bonds ? bonds : 1;
    }
  }
  void disconnect() {
    if (state_ == HidState::Connected) state_ = HidState::Advertising;
  }

 private:
  HidState state_ = HidState::Off;
};

// The wallpaper flash: a plain buffer that counts what is done to it.
class SimWallpaperStore : public WallpaperStore {
 public:
  size_t size = 128 * 1024;
  std::vector<uint8_t> bytes = std::vector<uint8_t>(128 * 1024, 0xFF);
  int writes = 0, erases = 0;
  bool failWrites = false;
  size_t capacity() override { return size; }
  bool write(size_t offset, const uint8_t *data, size_t n) override {
    if (failWrites || offset + n > bytes.size()) return false;
    memcpy(bytes.data() + offset, data, n);
    ++writes;
    return true;
  }
  bool read(size_t offset, uint8_t *out, size_t n) override {
    if (offset + n > bytes.size()) return false;
    memcpy(out, bytes.data() + offset, n);
    return true;
  }
  bool erase() override {
    memset(bytes.data(), 0xFF, 4096);
    ++erases;
    return true;
  }
};

// The server that is not there: it remembers the handler so tests can speak to
// it through RemoteConnection, exactly as a board's server would.
class SimRemoteServer : public RemoteServer {
 public:
  RemoteHandler *handler = nullptr;
  bool up = false;
  int begins = 0, ends = 0, polls = 0;
  bool refuse = false;
  bool begin(RemoteHandler &h) override {
    if (refuse) return false;
    handler = &h;
    up = true;
    ++begins;
    return true;
  }
  void poll() override { ++polls; }
  void end() override {
    up = false;
    ++ends;
  }
  bool running() const override { return up; }
  void hostname(char *out, size_t capacity) const override { snprintf(out, capacity, "stella-sim1"); }
  void address(char *out, size_t capacity) const override { snprintf(out, capacity, "192.168.1.50"); }
};

// A complete simulated device.
struct SimDevice {
  SimDevice(int width, int height) : display(width, height), buffer((size_t)width * 20) { light.display = &display; }
  SimDisplay display;
  ScriptedTouch touch;
  SimPlatform platform;
  MemoryStore storage;
  SimWifi wifi;
  SimHttp http;
  SimSound sound;
  SimSetupServer setup;
  SimFirmware firmware;
  SimLed led;
  SimLight light;
  SimRemoteServer remote;
  SimWallpaperStore wallpaper;
  SimHid hid;
  bool withHid = false;  // a board with Bluetooth (the Deck tests and screenshots)
  std::vector<Color> buffer;
  Board board() {
    Board b{&display, &touch, &platform, &storage, &wifi, &http, &sound, &setup, &firmware, &led, &light, &remote, &wallpaper};
    if (withHid) b.hid = &hid;
    return b;
  }
};

}  // namespace host
}  // namespace stella
