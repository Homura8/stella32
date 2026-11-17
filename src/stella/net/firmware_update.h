#pragma once
// Over-the-air updates, safe by construction. The device never trusts the
// network alone:
//   1. A manifest (HTTPS, JSON) names the new version, the image URL, its
//      size and its SHA-256 (and, if signing is on, an ECDSA P-256 signature:
//      r and s, hex).
//   2. The image streams into the *other* app slot while it is hashed. Size
//      and SHA-256 must match the manifest exactly.
//   3. Optionally (a public key built into this firmware), the manifest must
//      carry an ECDSA P-256 signature over the version *and* the image digest
//      (signedDigest()), so an old signed image cannot be relabelled as new.
//      Without a key the update relies on HTTPS to the update host and the
//      digest; with one, a compromised host cannot install anything.
//   4. Only then is the slot made the next boot image, *pending*: if the new
//      firmware does not run healthily long enough to be confirmed, the
//      bootloader returns to the previous one by itself.
// The update runs as a Job on the service thread. Installing is always the
// person's choice (Settings > About > Software update). Looking for a newer
// version can happen by itself, once a day (a setting, on by default): it only
// changes what Settings says ("Update 0.6.1 available"), never the firmware.
#include <atomic>
#include <stdint.h>

#include "../core/sha256.h"
#include "../hal/hal.h"
#include "../service/job_scheduler.h"
#include "../service/topic.h"
#include "http_service.h"
#include "json.h"

namespace stella {

namespace topics {
constexpr uint16_t kUpdate = 3;  // Topic<UpdateStatus>
}

enum class UpdateState : uint8_t {
  Unavailable,  // not set up: no second app slot or no manifest URL
  Idle,
  Checking,
  UpToDate,
  Available,    // `version` is newer and can be installed
  Downloading,  // `percent`
  Ready,        // installed as the next boot image: restart to run it
  Failed,       // `error`; the running firmware is untouched
};

struct UpdateStatus {
  UpdateState state = UpdateState::Unavailable;
  uint8_t percent = 0;
  uint32_t size = 0;      // bytes of the image on offer
  char version[16] = {0};  // the version on offer / installed
  char error[48] = {0};
};

class UpdateControl {
 public:
  virtual ~UpdateControl() = default;
  virtual UpdateStatus status() const = 0;
  virtual void check() = 0;    // ask the update server (commands return at once)
  virtual void install() = 0;  // download, verify and stage the offered version
  virtual void dismiss() = 0;  // acknowledge a result (Failed / UpToDate -> Idle)
};

class UpdateService : public Job, public UpdateControl, public JsonHandler {
 public:
  static constexpr uint32_t kMaxImageBytes = 2u * 1024u * 1024u;
  static constexpr uint32_t kAutoCheckIntervalMs = 24u * 60u * 60u * 1000u;

  explicit UpdateService(Topic<UpdateStatus> &topic) : topic_(topic) {}
  // Before the scheduler starts. `manifestUrl` must be HTTPS.
  void configure(HttpService *http, FirmwareSlot *slot, Platform *platform, JobScheduler *jobs,
                 const char *manifestUrl, const char *currentVersion);
  bool enabled() const { return enabled_; }

  // UpdateControl (any thread).
  UpdateStatus status() const override { return topic_.get(); }
  void check() override { post(Command::Check); }
  void install() override { post(Command::Install); }
  void dismiss() override { post(Command::Dismiss); }
  // The daily background check (any thread). Off: only the person's Check runs.
  void setAutoCheck(bool on) { autoCheck_.store(on); }
  bool autoCheck() const { return autoCheck_.load(); }

  // Job (service thread).
  const char *name() const override { return "update"; }
  JobPolicy policy() const override;
  JobResult run(uint32_t nowMs) override;
  void onValue(const JsonPath &path, const JsonValue &value) override;

  // What the signature covers: SHA-256("stella-update-v1|<version>|<hex of
  // the image's SHA-256>"). tools/update_tool.py builds the same message.
  static void signedDigest(const char *version, const uint8_t imageDigest[32], uint8_t out[32]);
  // Compares "a.b.c" versions (a leading 'v' is allowed): -1, 0, 1, or -2 if
  // either is malformed.
  static int compareVersions(const char *a, const char *b);
  // DER of an ECDSA signature (SEQUENCE of two INTEGERs) from r and s; returns
  // the length (at most 72).
  static size_t encodeDerSignature(const uint8_t r[32], const uint8_t s[32], uint8_t out[72]);

 private:
  enum class Command : uint8_t { None, Check, Install, Dismiss };
  struct Manifest {
    char version[16];
    char url[96];
    uint32_t size;
    uint8_t sha256[32], r[32], s[32];
    bool haveVersion, haveUrl, haveSize, haveSha, haveR, haveS;
  };
  class ImageSink;

  void post(Command c);
  void publish(UpdateState state, const char *error = "");
  // `quiet` (the daily check) leaves the shown state alone until it has an
  // answer and never turns a network problem into a visible error.
  JobResult doCheck(bool quiet);
  void doInstall();
  void fail(const char *reason);
  bool manifestComplete() const;
  bool signatureRequired() const { return slot_ && slot_->requiresSignature(); }

  Topic<UpdateStatus> &topic_;
  HttpService *http_ = nullptr;
  FirmwareSlot *slot_ = nullptr;
  Platform *platform_ = nullptr;
  JobScheduler *jobs_ = nullptr;
  char manifestUrl_[160] = {0};
  char current_[16] = {0};
  bool enabled_ = false;
  std::atomic<uint8_t> command_{0};
  std::atomic<bool> autoCheck_{false};
  UpdateStatus status_;
  Manifest manifest_{};
};

}  // namespace stella
