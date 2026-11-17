#include "firmware_update.h"

#include <math.h>
#include <string.h>

#include "../core/log.h"
#include "../core/text.h"

namespace stella {
namespace {

constexpr const char *kTag = "update";

}  // namespace

// Streams the image into the slot while hashing it; refuses more than the
// manifest promised.
class UpdateService::ImageSink : public HttpSink {
 public:
  ImageSink(FirmwareSlot &slot, uint32_t expected) : slot_(slot), expected_(expected) {}
  bool onData(const uint8_t *data, size_t size) override {
    if (received_ + size > expected_) {
      tooMuch_ = true;
      return false;
    }
    hash_.update(data, size);
    if (!slot_.write(data, size)) {
      writeFailed_ = true;
      return false;
    }
    received_ += (uint32_t)size;
    return true;
  }
  uint32_t received() const { return received_; }
  bool tooMuch() const { return tooMuch_; }
  bool writeFailed() const { return writeFailed_; }
  void digest(uint8_t out[32]) { hash_.finish(out); }

 private:
  FirmwareSlot &slot_;
  uint32_t expected_, received_ = 0;
  bool tooMuch_ = false, writeFailed_ = false;
  Sha256 hash_;
};

void UpdateService::configure(HttpService *http, FirmwareSlot *slot, Platform *platform, JobScheduler *jobs,
                              const char *manifestUrl, const char *currentVersion) {
  jobs_ = jobs;
  http_ = http;
  slot_ = slot;
  platform_ = platform;
  copyText(current_, sizeof(current_), currentVersion ? currentVersion : "");
  copyText(manifestUrl_, sizeof(manifestUrl_), manifestUrl ? manifestUrl : "");
  enabled_ = http && http->available() && slot && slot->available() && strncmp(manifestUrl_, "https://", 8) == 0;
  status_ = UpdateStatus{};
  if (enabled_) {
    status_.state = UpdateState::Idle;
  } else {
    status_.state = UpdateState::Unavailable;
    copyText(status_.error, sizeof(status_.error),
             !slot || !slot->available() ? "Needs the two-slot layout (flash by USB once)" : "No update server set");
  }
  topic_.publish(status_);
}

void UpdateService::post(Command c) {
  command_.store((uint8_t)c);
  if (jobs_) jobs_->requestRun(*this);  // its own schedule is a day: commands wake it now
}

JobPolicy UpdateService::policy() const {
  JobPolicy p;
  p.intervalMs = 24u * 60u * 60u * 1000u;  // nothing runs by itself: commands wake it
  p.retryMs = p.intervalMs;
  p.maxBackoffMs = p.intervalMs;
  return p;
}

void UpdateService::publish(UpdateState state, const char *error) {
  status_.state = state;
  copyText(status_.error, sizeof(status_.error), error);
  topic_.publish(status_);
}

void UpdateService::fail(const char *reason) {
  LOGW(kTag, "update failed: %s", reason);
  status_.percent = 0;
  publish(UpdateState::Failed, reason);
}

JobResult UpdateService::run(uint32_t) {
  const Command c = (Command)command_.exchange((uint8_t)Command::None);
  if (!enabled_ || c == Command::None) return JobResult::Ok;
  const UpdateState s = status_.state;
  if (c == Command::Dismiss) {
    if (s == UpdateState::Failed || s == UpdateState::UpToDate) publish(UpdateState::Idle);
  } else if (c == Command::Check) {
    if (s == UpdateState::Idle || s == UpdateState::UpToDate || s == UpdateState::Failed ||
        s == UpdateState::Available)
      doCheck();
  } else if (c == Command::Install) {
    if (s == UpdateState::Available) doInstall();
  }
  return JobResult::Ok;
}

void UpdateService::onValue(const JsonPath &path, const JsonValue &value) {
  if (path.depth() != 1) return;
  if (value.type == JsonType::String && !value.truncated) {
    if (path.is("version")) manifest_.haveVersion = true, copyText(manifest_.version, sizeof(manifest_.version), value.text);
    else if (path.is("url")) manifest_.haveUrl = true, copyText(manifest_.url, sizeof(manifest_.url), value.text);
    else if (path.is("sha256")) manifest_.haveSha = fromHex(value.text, manifest_.sha256, 32);
    else if (path.is("sig_r")) manifest_.haveR = fromHex(value.text, manifest_.r, 32);
    else if (path.is("sig_s")) manifest_.haveS = fromHex(value.text, manifest_.s, 32);
  } else if (value.type == JsonType::Number && path.is("size")) {
    if (value.number >= 1 && value.number <= (double)kMaxImageBytes && value.number == floor(value.number)) {
      manifest_.size = (uint32_t)value.number;
      manifest_.haveSize = true;
    }
  }
}

bool UpdateService::manifestComplete() const {
  const bool signedOk = !signatureRequired() || (manifest_.haveR && manifest_.haveS);
  return manifest_.haveVersion && manifest_.haveUrl && manifest_.haveSize && manifest_.haveSha && signedOk &&
         compareVersions(manifest_.version, "0.0.0") != -2 && strncmp(manifest_.url, "https://", 8) == 0;
}

void UpdateService::doCheck() {
  publish(UpdateState::Checking);
  manifest_ = Manifest{};
  FetchLimits limits;
  limits.maxBytes = 4096;
  limits.timeoutMs = 15000;
  const FetchResult r = http_->getJson(manifestUrl_, *this, limits);
  if (!r.ok()) {
    fail(r.kind == FetchResult::Busy ? "Network busy, try again" : "Couldn't reach the update server");
    return;
  }
  if (!manifestComplete()) {
    fail("The update information is not valid");
    return;
  }
  copyText(status_.version, sizeof(status_.version), manifest_.version);
  status_.size = manifest_.size;
  const int order = compareVersions(manifest_.version, current_);
  if (order <= 0) {
    publish(UpdateState::UpToDate);
    return;
  }
  if (manifest_.size > slot_->capacity()) {
    fail("The update is too big for this device");
    return;
  }
  publish(UpdateState::Available);
}

void UpdateService::doInstall() {
  if (platform_ && platform_->updatePending()) {
    fail("Finish confirming the last update first");
    return;
  }
  status_.percent = 0;
  publish(UpdateState::Downloading);
  if (!slot_->begin(manifest_.size)) {
    fail("Can't write the update slot");
    return;
  }
  ImageSink sink(*slot_, manifest_.size);
  // Report progress as it streams (every 2 %), without a topic update per chunk.
  class Progress : public HttpSink {
   public:
    Progress(UpdateService &s, ImageSink &inner) : s_(s), inner_(inner) {}
    bool onData(const uint8_t *d, size_t n) override {
      if (!inner_.onData(d, n)) return false;
      const uint8_t pct = (uint8_t)((uint64_t)inner_.received() * 100u / s_.manifest_.size);
      if (pct >= s_.status_.percent + 2) {
        s_.status_.percent = pct;
        s_.topic_.publish(s_.status_);
      }
      return true;
    }

   private:
    UpdateService &s_;
    ImageSink &inner_;
  } progress(*this, sink);
  FetchLimits limits;
  limits.maxBytes = manifest_.size;
  limits.timeoutMs = 30000;
  limits.leaseWaitMs = 60000;
  const FetchResult r = http_->get(manifest_.url, progress, limits);
  if (!r.ok() || sink.tooMuch() || sink.writeFailed()) {
    slot_->abort();
    fail(sink.writeFailed()   ? "Writing the update failed"
         : sink.tooMuch() || r.kind == FetchResult::TooLarge ? "The download is larger than announced"
         : r.kind == FetchResult::Busy ? "Network busy, try again"
                                       : "The download failed");
    return;
  }
  if (sink.received() != manifest_.size) {
    slot_->abort();
    fail("The download was incomplete");
    return;
  }
  uint8_t digest[32], message[32];
  sink.digest(digest);
  if (memcmp(digest, manifest_.sha256, 32) != 0) {
    slot_->abort();
    fail("The download is damaged (checksum)");
    return;
  }
  if (signatureRequired()) {
    signedDigest(manifest_.version, digest, message);
    if (!slot_->verify(message, manifest_.r, manifest_.s)) {
      slot_->abort();
      fail("The update's signature was rejected");
      return;
    }
  }
  if (!slot_->finish()) {
    fail("That is not a valid firmware image");
    return;
  }
  LOGI(kTag, "update %s staged; restart to run it", manifest_.version);
  status_.percent = 100;
  publish(UpdateState::Ready);
}

void UpdateService::signedDigest(const char *version, const uint8_t imageDigest[32], uint8_t out[32]) {
  Sha256 h;
  h.update("stella-update-v1|", 17);
  h.update(version, strlen(version));
  h.update("|", 1);
  char hex[65];
  toHex(imageDigest, 32, hex);
  h.update(hex, 64);
  h.finish(out);
}

int UpdateService::compareVersions(const char *a, const char *b) {
  auto parse = [](const char *s, int out[3]) {
    if (*s == 'v' || *s == 'V') ++s;
    for (int i = 0; i < 3; ++i) {
      if (*s < '0' || *s > '9') return false;
      long v = 0;
      while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s++ - '0');
        if (v > 99999) return false;
      }
      out[i] = (int)v;
      if (i < 2) {
        if (*s != '.') return false;
        ++s;
      }
    }
    return *s == '\0';
  };
  int x[3], y[3];
  if (!a || !b || !parse(a, x) || !parse(b, y)) return -2;
  for (int i = 0; i < 3; ++i)
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}

size_t UpdateService::encodeDerSignature(const uint8_t r[32], const uint8_t s[32], uint8_t out[72]) {
  size_t n = 2;  // after SEQUENCE tag and length
  auto integer = [&](const uint8_t *v) {
    size_t skip = 0;
    while (skip < 31 && v[skip] == 0) ++skip;  // minimal encoding, at least one byte
    const bool pad = v[skip] & 0x80;           // keep it positive
    out[n++] = 0x02;
    out[n++] = (uint8_t)(32 - skip + (pad ? 1 : 0));
    if (pad) out[n++] = 0x00;
    for (size_t i = skip; i < 32; ++i) out[n++] = v[i];
  };
  integer(r);
  integer(s);
  out[0] = 0x30;
  out[1] = (uint8_t)(n - 2);
  return n;
}

}  // namespace stella
