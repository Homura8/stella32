#pragma once
// The page the device serves on the home network ("remote page"): reachable by
// name (mDNS, stella-xxxx.local) and by IP, so a phone or PC does not have to
// join the setup access point. It starts only when the person turns it on
// (Settings > Remote page), and everything that changes anything needs the PIN
// shown on the device's own screen:
//
//   GET  /                     the page (public: it contains nothing private)
//   GET  /api/status           what is set (public)
//   POST /api/check            is this PIN right?
//   POST /api/wallpaper        the picture, streamed to flash (wallpaper.h)
//   POST /api/wallpaper/remove forget the picture
//   POST /api/veil             v=NN, how much to wash the picture back
//   POST /api/calendar         the body is the https address of an iCalendar feed
//   POST /api/calendar/remove  forget it
//   POST /api/calendar/status  {"set":true,"host":"..."} (never the address itself)
//
// Plain HTTP on a home LAN: the PIN and the picture cross it unencrypted, so
// this is for a network you trust. Wrong PINs lock the page for five minutes
// after five tries; the PIN is never in a topic, a log or a URL.
#include <atomic>
#include <stddef.h>
#include <stdint.h>

#include "../hal/hal.h"
#include "../osal/osal.h"
#include "../service/job_scheduler.h"
#include "../service/topic.h"
#include "../storage/record.h"
#include "calendar_source.h"
#include "http_request.h"
#include "network_status.h"
#include "wallpaper.h"

namespace stella {

namespace topics {
constexpr uint16_t kRemote = 4;  // Topic<RemoteInfo>
}

struct RemoteInfo {
  bool enabled = false;
  bool running = false;           // listening on the home network now
  bool wallpaperSupported = false;
  bool wallpaper = false;         // a valid picture is stored
  bool lockedOut = false;         // too many wrong PINs
  uint8_t veil = 35;              // 0..90 % wash over the picture
  uint32_t generation = 0;        // changes whenever the picture does
  char host[24] = {0};            // "stella-ab12"
  char ip[16] = {0};
};

// What the UI may do (any thread; the work happens on the service thread).
class RemoteControl {
 public:
  virtual ~RemoteControl() = default;
  virtual RemoteInfo status() const = 0;
  virtual bool pin(char out[8]) const = 0;  // for the device's own screen only
  virtual void setEnabled(bool on) = 0;
  virtual void newPin() = 0;
  virtual void removeWallpaper() = 0;
  virtual void setVeil(uint8_t percent) = 0;
};

struct RemoteRequest {
  bool post = false;
  const char *path = "/";
  const char *body = "";  // small bodies only (the form of /api/veil)
  size_t bodyLen = 0;
  const char *pin = "";   // the X-Stella-Pin header
};

struct RemoteResponse {
  int status = 200;
  const char *contentType = "application/json";
  const char *constBody = nullptr;  // a fixed page in flash (not copied)
  size_t constLength = 0;
  char *body = nullptr;  // else supplied by the server, filled by the handler
  size_t capacity = 0;
  size_t length = 0;
};

class RemoteHandler : public HttpBodySink {
 public:
  virtual void handleRemote(const RemoteRequest &request, RemoteResponse &response) = 0;
};

// One connection's request and its answer, shared by the board's server and the
// tests: bytes in, a complete HTTP response out. A fixed-size object.
class RemoteConnection {
 public:
  RemoteConnection() = default;  // bind() before use (a server keeps a few of these)
  explicit RemoteConnection(RemoteHandler &handler) { bind(handler); }
  void bind(RemoteHandler &handler) {
    handler_ = &handler;
    parser_.setStreaming(&handler, wallpaper::kTotalBytes);
  }
  void reset() { parser_.reset(); }
  bool feed(const uint8_t *data, size_t size) {
    parser_.feed(data, size);
    return parser_.done();
  }
  bool done() const { return parser_.done(); }
  // Runs the handler and writes the status line and headers into `head`; the
  // body is `*body` / `*bodyLength` (it points into `scratch` or at a fixed page).
  // Returns the length of the head. `scratch` is the server's response buffer.
  size_t respond(char *head, size_t headCapacity, char *scratch, size_t scratchCapacity, const uint8_t **body,
                 size_t *bodyLength);
  HttpRequestParser &parser() { return parser_; }

 private:
  RemoteHandler *handler_ = nullptr;
  HttpRequestParser parser_;
};

class RemoteService : public Job, public RemoteHandler, public RemoteControl {
 public:
  static constexpr uint32_t kLockMs = 5u * 60u * 1000u;
  static constexpr int kMaxFailures = 5;

  explicit RemoteService(Topic<RemoteInfo> &topic) : topic_(topic) {}
  // Before the scheduler starts. Any of the devices may be null.
  void configure(RemoteServer *server, WallpaperStore *store, WifiDriver *rng, KeyValueStore *kv, JobScheduler *jobs,
                 const Topic<NetworkStatus> *network, CalendarSourceControl *calendar = nullptr);
  bool wallpaperValid() const { return valid_.load(); }

  // RemoteControl.
  RemoteInfo status() const override { return topic_.get(); }
  bool pin(char out[8]) const override;
  void setEnabled(bool on) override;
  void newPin() override;
  void removeWallpaper() override;
  void setVeil(uint8_t percent) override;

  // Job.
  const char *name() const override { return "remote"; }
  JobPolicy policy() const override;
  JobResult run(uint32_t nowMs) override;

  // RemoteHandler.
  void handleRemote(const RemoteRequest &request, RemoteResponse &response) override;
  int begin(const HttpRequestParser &request, size_t length) override;
  bool write(const uint8_t *data, size_t size) override;
  void end(bool complete) override;

 private:
  struct Record {
    static constexpr uint16_t kVersion = 1;
    uint8_t enabled;
    uint8_t veil;
    char pin[8];
  };
  struct Pending {
    bool setEnabled = false, enabled = false, newPin = false, removeWallpaper = false, setVeil = false;
    uint8_t veil = 0;
  };

  void post(const Pending &p);
  void applyPending();
  void save();
  void makePin();
  int authorize(const char *pin);  // 0 ok, 401 wrong, 429 locked
  void handleCalendar(const char *path, const RemoteRequest &req, RemoteResponse &res);
  void publish();
  void json(RemoteResponse &r, const char *text);

  Topic<RemoteInfo> &topic_;
  RemoteServer *server_ = nullptr;
  WallpaperStore *store_ = nullptr;
  WifiDriver *rng_ = nullptr;
  KeyValueStore *kv_ = nullptr;
  JobScheduler *jobs_ = nullptr;
  const Topic<NetworkStatus> *network_ = nullptr;
  CalendarSourceControl *calendar_ = nullptr;

  mutable osal::Mutex lock_;  // guards pending_ and pin_
  Pending pending_;
  char pin_[8] = {0};

  // Service thread only.
  RemoteInfo info_;
  bool enabled_ = false;
  uint8_t veil_ = 35;
  uint32_t now_ = 0, lockedUntil_ = 0;
  int failures_ = 0;
  bool locked_ = false;
  // The upload in progress.
  bool uploading_ = false;
  size_t received_ = 0;
  uint32_t crc_ = 0;
  uint8_t header_[wallpaper::kHeaderBytes] = {};
  wallpaper::Header parsedHeader_{};
  int uploadResult_ = 0;  // what the request that finished an upload gets
  std::atomic<bool> valid_{false};
  std::atomic<bool> running_{false};
  uint32_t generation_ = 0;
};

}  // namespace stella
