#pragma once
// Wi-Fi as an OS service: saved credentials, connection with backoff, scans,
// time sync, and status topics. Runs as a Job on the service thread; UI
// commands arrive through a small mutex-protected mailbox.
//
// Credentials are stored as a versioned record ("wifi"/"creds") in NVS. They
// are never logged. Note: NVS is not encrypted unless flash encryption is
// enabled on the device.
#include <atomic>
#include <stdint.h>

#include "../hal/hal.h"
#include "../osal/osal.h"
#include "../service/job_scheduler.h"
#include "../service/topic.h"
#include "network_status.h"
#include "setup_portal.h"

namespace stella {

struct WifiCredentials {
  static constexpr uint16_t kVersion = 1;
  char ssid[33] = {0};
  char password[65] = {0};
};

class WifiManager : public Job, public NetworkControl, public SetupControl {
 public:
  static constexpr uint32_t kConnectTimeoutMs = 20000;
  static constexpr uint32_t kMaxBackoffMs = 120000;
  static constexpr uint32_t kSetupTimeoutMs = 10u * 60u * 1000u;  // the setup access point closes by itself
  static constexpr const char *kSetupSsid = "Stella-Setup";

  WifiManager(Topic<NetworkStatus> &status, Topic<WifiScan> &scan) : status_(status), scan_(scan), portal_(*this) {}

  // Before the scheduler starts. `fallback*` (e.g. from secrets.h) are used
  // only when nothing is saved yet.
  void configure(WifiDriver *driver, KeyValueStore *store, JobScheduler *jobs, const char *fallbackSsid,
                 const char *fallbackPassword, SetupServer *setup = nullptr);
  bool available() const { return driver_ != nullptr; }

  // NetworkControl (any thread).
  NetworkStatus status() const override { return status_.get(); }
  WifiScan scan() const override { return scan_.get(); }
  void requestScan() override;
  void join(const char *ssid, const char *password) override;
  void forget() override;
  void beginSetup() override;
  void cancelSetup() override;
  bool setupInfo(SetupInfo &out) const override;

  // SetupControl (service thread, called by the portal).
  WifiScan setupScan() const override { return scan_.get(); }
  NetworkStatus setupStatus() const override { return status_.get(); }
  void setupJoin(const char *ssid, const char *password) override;

  // Job (service thread).
  const char *name() const override { return "wifi"; }
  JobPolicy policy() const override;
  JobResult run(uint32_t nowMs) override;

 private:
  enum class Command : uint8_t { None, Join, Forget, Setup, CancelSetup };
  enum class SetupPhase : uint8_t { Off, Scanning, Open };
  void startConnect(uint32_t now);
  void startSetupServer(uint32_t now);
  void endSetup();
  void pollScan();
  void publish();

  Topic<NetworkStatus> &status_;
  Topic<WifiScan> &scan_;
  WifiDriver *driver_ = nullptr;
  KeyValueStore *store_ = nullptr;
  JobScheduler *jobs_ = nullptr;

  // Mailbox (UI -> service thread).
  osal::Mutex mailbox_;
  Command command_ = Command::None;
  WifiCredentials pending_;
  std::atomic<bool> scanRequested_{false};
  mutable osal::Mutex setupLock_;
  SetupInfo setupInfo_;  // guarded by setupLock_

  // Service-thread state.
  WifiCredentials creds_;
  NetworkStatus current_;
  bool begun_ = false, connecting_ = false, scanning_ = false, syncStarted_ = false;
  uint32_t attemptMs_ = 0, nextAttemptMs_ = 0, backoffMs_ = 5000;
  uint16_t scanSequence_ = 0;

  // Phone setup (service thread).
  SetupServer *server_ = nullptr;
  SetupPortal portal_;
  SetupPhase setup_ = SetupPhase::Off;
  bool setupJoining_ = false;  // a join started from the phone page is in progress
  uint32_t setupStartMs_ = 0;
};

}  // namespace stella
