#include "remote.h"

#include <stdio.h>
#include <string.h>

#include "../core/deadline.h"
#include "../core/log.h"
#include "../core/text.h"
#include "remote_page.h"

namespace stella {
namespace {

constexpr const char *kTag = "remote";

const char *statusText(int status) {
  switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 422: return "Unprocessable Entity";
    case 429: return "Too Many Requests";
    case 503: return "Service Unavailable";
    default: return "Error";
  }
}

// Compares two strings without stopping at the first difference.
bool sameText(const char *a, const char *b) {
  const size_t la = strlen(a), lb = strlen(b);
  unsigned char diff = la == lb ? 0 : 1;
  for (size_t i = 0; i < lb; ++i) diff |= (unsigned char)(a[i < la ? i : 0] ^ b[i]);
  return diff == 0 && la == lb;
}

}  // namespace

// ---------------------------------------------------------------- connection

size_t RemoteConnection::respond(char *head, size_t headCapacity, char *scratch, size_t scratchCapacity,
                                 const uint8_t **body, size_t *bodyLength) {
  RemoteResponse response;
  response.body = scratch;
  response.capacity = scratchCapacity;
  if (parser_.state() == HttpRequestParser::State::Failed) {
    response.status = parser_.errorStatus();
    response.contentType = "text/plain";
    response.length = (size_t)snprintf(scratch, scratchCapacity, "%s", statusText(response.status));
  } else {
    RemoteRequest request;
    request.post = parser_.post();
    request.path = parser_.path();
    request.body = parser_.body();
    request.bodyLen = parser_.bodyLen();
    request.pin = parser_.pin();
    handler_->handleRemote(request, response);
  }
  const bool fixed = response.constBody != nullptr;
  const size_t length = fixed ? response.constLength : (response.length < scratchCapacity ? response.length : scratchCapacity - 1);
  if (!fixed) scratch[length] = '\0';
  *body = fixed ? reinterpret_cast<const uint8_t *>(response.constBody) : reinterpret_cast<const uint8_t *>(scratch);
  *bodyLength = length;
  const int n = snprintf(head, headCapacity,
                         "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\n"
                         "Connection: close\r\n\r\n",
                         response.status, statusText(response.status), response.contentType, (unsigned)length);
  return n > 0 && (size_t)n < headCapacity ? (size_t)n : 0;
}

// ---------------------------------------------------------------- service

void RemoteService::configure(RemoteServer *server, WallpaperStore *store, WifiDriver *rng, KeyValueStore *kv,
                              JobScheduler *jobs, const Topic<NetworkStatus> *network) {
  server_ = server;
  store_ = store;
  rng_ = rng;
  kv_ = kv;
  jobs_ = jobs;
  network_ = network;
  Record record{};
  const RecordStatus status = loadRecord(kv_, "sys", "remote", Record::kVersion, &record, sizeof(record));
  record.pin[sizeof(record.pin) - 1] = '\0';
  bool pinOk = status == RecordStatus::Ok && strlen(record.pin) == 6;
  for (int i = 0; pinOk && i < 6; ++i) pinOk = record.pin[i] >= '0' && record.pin[i] <= '9';
  enabled_ = status == RecordStatus::Ok && record.enabled != 0;
  veil_ = status == RecordStatus::Ok && record.veil <= 90 ? record.veil : 35;
  if (pinOk) memcpy(pin_, record.pin, sizeof(pin_));
  else makePin();
  info_.wallpaperSupported = store_ && store_->capacity() >= wallpaper::kTotalBytes;
  uint16_t palette[wallpaper::kColors];
  valid_.store(info_.wallpaperSupported && wallpaper::load(*store_, palette));
  info_.enabled = enabled_;
  info_.veil = veil_;
  info_.wallpaper = valid_.load();
  generation_ = 1;
  info_.generation = generation_;
  if (!pinOk) save();
  topic_.publish(info_);
}

void RemoteService::makePin() {
  uint32_t r = rng_ ? rng_->random32() : 4242u;
  for (int i = 5; i >= 0; --i) {
    pin_[i] = (char)('0' + r % 10);
    r /= 10;
  }
  pin_[6] = '\0';
}

bool RemoteService::pin(char out[8]) const {
  osal::LockGuard<osal::Mutex> guard(lock_);
  memcpy(out, pin_, 8);
  return pin_[0] != '\0';
}

void RemoteService::save() {
  Record record{};
  record.enabled = enabled_ ? 1 : 0;
  record.veil = veil_;
  {
    osal::LockGuard<osal::Mutex> guard(lock_);
    memcpy(record.pin, pin_, sizeof(record.pin));
  }
  saveRecord(kv_, "sys", "remote", Record::kVersion, &record, sizeof(record));
  volatile char *v = record.pin;  // a copy of the PIN
  for (size_t i = 0; i < sizeof(record.pin); ++i) v[i] = 0;
}

void RemoteService::post(const Pending &p) {
  {
    osal::LockGuard<osal::Mutex> guard(lock_);
    if (p.setEnabled) pending_.setEnabled = true, pending_.enabled = p.enabled;
    if (p.newPin) pending_.newPin = true;
    if (p.removeWallpaper) pending_.removeWallpaper = true;
    if (p.setVeil) pending_.setVeil = true, pending_.veil = p.veil;
  }
  if (jobs_) jobs_->requestRun(*this);
}

void RemoteService::setEnabled(bool on) {
  Pending p;
  p.setEnabled = true;
  p.enabled = on;
  post(p);
}
void RemoteService::newPin() {
  Pending p;
  p.newPin = true;
  post(p);
}
void RemoteService::removeWallpaper() {
  Pending p;
  p.removeWallpaper = true;
  post(p);
}
void RemoteService::setVeil(uint8_t percent) {
  Pending p;
  p.setVeil = true;
  p.veil = percent > 90 ? 90 : percent;
  post(p);
}

void RemoteService::applyPending() {
  Pending p;
  {
    osal::LockGuard<osal::Mutex> guard(lock_);
    p = pending_;
    pending_ = Pending{};
  }
  bool changed = false;
  if (p.setEnabled && p.enabled != enabled_) {
    enabled_ = p.enabled;
    changed = true;
  }
  if (p.newPin) {
    osal::LockGuard<osal::Mutex> guard(lock_);
    makePin();
    failures_ = 0;
    locked_ = false;
    changed = true;
  }
  if (p.setVeil && p.veil != veil_) {
    veil_ = p.veil;
    changed = true;
  }
  if (p.removeWallpaper && store_ && !uploading_) {
    store_->erase();
    valid_.store(false);
    ++generation_;
    changed = true;
  }
  if (changed) save();
}

JobPolicy RemoteService::policy() const {
  JobPolicy p;
  p.intervalMs = running_.load() ? 20 : 1000;  // answer a request promptly; idle gently
  p.retryMs = 2000;
  p.maxBackoffMs = 10000;
  return p;
}

JobResult RemoteService::run(uint32_t nowMs) {
  now_ = nowMs;
  applyPending();
  if (locked_ && reached(now_, lockedUntil_)) {
    locked_ = false;
    failures_ = 0;
  }
  const NetworkStatus net = network_ ? network_->get() : NetworkStatus{};
  // Not while phone setup owns port 80 and the radio's other interface.
  const bool want = server_ && enabled_ && net.connected && !net.setup;
  if (want && !server_->running()) {
    if (server_->begin(*this)) LOGI(kTag, "remote page is up");
    else LOGW(kTag, "remote page could not start");
  } else if (!want && server_ && server_->running()) {
    server_->end();
    LOGI(kTag, "remote page is off");
  }
  const bool running = server_ && server_->running();
  running_.store(running);
  if (running) server_->poll();
  RemoteInfo next = info_;
  next.enabled = enabled_;
  next.running = running;
  next.veil = veil_;
  next.wallpaper = valid_.load();
  next.lockedOut = locked_;
  next.generation = generation_;
  if (running) {
    server_->hostname(next.host, sizeof(next.host));
    server_->address(next.ip, sizeof(next.ip));
  } else {
    next.host[0] = next.ip[0] = '\0';
  }
  if (memcmp(&next, &info_, sizeof(next)) != 0) {
    info_ = next;
    topic_.publish(info_);
  }
  return JobResult::Ok;
}

int RemoteService::authorize(const char *given) {
  if (locked_) return 429;
  char expected[8];
  {
    osal::LockGuard<osal::Mutex> guard(lock_);
    memcpy(expected, pin_, sizeof(expected));
  }
  const bool ok = expected[0] && sameText(given, expected);
  volatile char *v = expected;
  for (size_t i = 0; i < sizeof(expected); ++i) v[i] = 0;
  if (ok) {
    failures_ = 0;
    return 0;
  }
  if (++failures_ >= kMaxFailures) {
    locked_ = true;
    lockedUntil_ = now_ + kLockMs;
    LOGW(kTag, "too many wrong PINs: locked for five minutes");
  }
  return 401;
}

void RemoteService::json(RemoteResponse &r, const char *text) {
  r.length = (size_t)snprintf(r.body, r.capacity, "%s", text);
}

void RemoteService::handleRemote(const RemoteRequest &req, RemoteResponse &res) {
  const char *path = req.path;
  if (!req.post) {
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
      res.contentType = "text/html; charset=utf-8";
      res.constBody = kRemotePage;
      res.constLength = strlen(kRemotePage);
      return;
    }
    if (strcmp(path, "/api/status") == 0) {
      res.length = (size_t)snprintf(res.body, res.capacity,
                                    "{\"name\":\"StellaOS\",\"supported\":%s,\"wallpaper\":%s,\"veil\":%u,\"locked\":%s}",
                                    info_.wallpaperSupported ? "true" : "false", valid_.load() ? "true" : "false",
                                    (unsigned)veil_, locked_ ? "true" : "false");
      return;
    }
    res.status = 404;
    json(res, "{\"error\":\"not found\"}");
    return;
  }
  if (strcmp(path, "/api/wallpaper") == 0) {  // the body was streamed; this is its answer
    res.status = uploadResult_ ? uploadResult_ : 500;
    json(res, res.status == 200 ? "{\"ok\":true}" : "{\"error\":\"the picture was not accepted\"}");
    uploadResult_ = 0;
    return;
  }
  const bool known = strcmp(path, "/api/check") == 0 || strcmp(path, "/api/wallpaper/remove") == 0 ||
                     strcmp(path, "/api/veil") == 0 || strcmp(path, "/api/push") == 0 || strcmp(path, "/api/push/clear") == 0;
  if (!known) {
    res.status = 404;
    json(res, "{\"error\":\"not found\"}");
    return;
  }
  const int auth = authorize(req.pin);
  if (auth != 0) {
    res.status = auth;
    json(res, auth == 429 ? "{\"error\":\"too many tries; wait five minutes\"}" : "{\"error\":\"wrong PIN\"}");
    return;
  }
  if (strcmp(path, "/api/check") == 0) {
    json(res, "{\"ok\":true}");
  } else if (strcmp(path, "/api/wallpaper/remove") == 0) {
    removeWallpaper();
    json(res, "{\"ok\":true}");
  } else if (strncmp(path, "/api/push", 9) == 0) {
    handlePush(path, req, res);
  } else {  // /api/veil
    char value[8];
    const char *eq = strchr(req.body, '=');
    unsigned v = 0;
    if (!eq || req.bodyLen > 8 || sscanf(eq + 1, "%7s", value) != 1 || sscanf(value, "%u", &v) != 1 || v > 90) {
      res.status = 400;
      json(res, "{\"error\":\"v=0..90\"}");
      return;
    }
    setVeil((uint8_t)v);
    json(res, "{\"ok\":true}");
  }
}

// Live values from a script: they go into the Values app, in RAM only.
void RemoteService::handlePush(const char *path, const RemoteRequest &req, RemoteResponse &res) {
  if (!values_) {
    res.status = 503;
    json(res, "{\"error\":\"no values screen on this device\"}");
    return;
  }
  if (strcmp(path, "/api/push/clear") == 0) {
    pushed_ = PushedValues{};
    ++pushed_.generation;
    values_->publish(pushed_);
    json(res, "{\"ok\":true}");
    return;
  }
  const PushResult r = applyPush(pushed_, req.body, req.bodyLen, now_);
  if (r.accepted == 0) {
    res.status = 400;
    json(res, "{\"error\":\"send name=value pairs, e.g. cpu=37%\"}");
    return;
  }
  values_->publish(pushed_);
  res.length = (size_t)snprintf(res.body, res.capacity, "{\"ok\":true,\"accepted\":%d,\"rejected\":%d}", r.accepted, r.rejected);
}

// ---------------------------------------------------------------- upload

int RemoteService::begin(const HttpRequestParser &request, size_t length) {
  if (strcmp(request.path(), "/api/wallpaper") != 0) return 413;  // only the picture may be long
  if (!store_ || !info_.wallpaperSupported) return 503;
  if (length != wallpaper::kTotalBytes) return 413;
  const int auth = authorize(request.pin());
  if (auth != 0) return auth;
  if (uploading_) return 409;
  uploading_ = true;
  received_ = 0;
  crc_ = 0;
  uploadResult_ = 0;
  valid_.store(false);  // the screen stops using the old picture while it is overwritten
  ++generation_;
  return 0;
}

bool RemoteService::write(const uint8_t *data, size_t size) {
  if (!uploading_) return false;
  size_t i = 0;
  while (i < size) {
    if (received_ < wallpaper::kHeaderBytes) {
      const size_t n = size - i < wallpaper::kHeaderBytes - received_ ? size - i : wallpaper::kHeaderBytes - received_;
      memcpy(header_ + received_, data + i, n);
      if (!store_->write(received_, data + i, n)) return false;
      received_ += n;
      i += n;
      if (received_ == wallpaper::kHeaderBytes && !wallpaper::parseHeader(header_, parsedHeader_)) {
        uploadResult_ = 422;
        return false;
      }
    } else {
      const size_t n = size - i;
      if (!store_->write(received_, data + i, n)) return false;
      crc_ = wallpaper::crc32(data + i, n, crc_);
      received_ += n;
      i += n;
    }
  }
  return true;
}

void RemoteService::end(bool complete) {
  if (!uploading_) return;
  uploading_ = false;
  if (complete && received_ == wallpaper::kTotalBytes && crc_ == parsedHeader_.crc) {
    valid_.store(true);
    uploadResult_ = 200;
    LOGI(kTag, "a wallpaper was uploaded");
  } else {
    if (complete) uploadResult_ = 422;  // the file was damaged on the way
    else if (uploadResult_ == 0) uploadResult_ = 500;
    if (store_) store_->erase();
    valid_.store(false);
  }
  ++generation_;
}

}  // namespace stella
