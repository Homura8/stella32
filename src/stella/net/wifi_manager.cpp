#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include "../core/deadline.h"
#include "../core/log.h"
#include "../core/text.h"
#include "../storage/record.h"

namespace stella {
namespace {

constexpr const char *kTag = "wifi";
constexpr const char *kNamespace = "wifi";
constexpr const char *kKey = "creds";

// Clears credentials in a way the optimiser may not elide.
void wipe(WifiCredentials &c) {
  volatile unsigned char *p = reinterpret_cast<volatile unsigned char *>(&c);
  for (size_t i = 0; i < sizeof(c); ++i) p[i] = 0;
}

}  // namespace

void WifiManager::configure(WifiDriver *driver, KeyValueStore *store, JobScheduler *jobs, const char *fallbackSsid,
                            const char *fallbackPassword) {
  driver_ = driver;
  store_ = store;
  jobs_ = jobs;
  current_ = NetworkStatus{};
  current_.present = driver != nullptr;
  current_.state = driver ? WifiState::NotConfigured : WifiState::Unavailable;
  if (driver) {
    WifiCredentials saved;
    if (loadRecord(store, kNamespace, kKey, WifiCredentials::kVersion, &saved, sizeof(saved)) == RecordStatus::Ok &&
        saved.ssid[0]) {
      creds_ = saved;
    } else if (fallbackSsid && fallbackSsid[0]) {
      copyText(creds_.ssid, sizeof(creds_.ssid), fallbackSsid);
      copyText(creds_.password, sizeof(creds_.password), fallbackPassword);
    }
    if (creds_.ssid[0]) {
      current_.state = WifiState::Connecting;
      copyText(current_.ssid, sizeof(current_.ssid), creds_.ssid);
    }
  }
  status_.publish(current_);
}

void WifiManager::requestScan() {
  scanRequested_.store(true);
  if (jobs_) jobs_->requestRun(*this);
}

void WifiManager::join(const char *ssid, const char *password) {
  {
    osal::LockGuard<osal::Mutex> g(mailbox_);
    command_ = Command::Join;
    copyText(pending_.ssid, sizeof(pending_.ssid), ssid);
    copyText(pending_.password, sizeof(pending_.password), password ? password : "");
  }
  if (jobs_) jobs_->requestRun(*this);
}

void WifiManager::forget() {
  {
    osal::LockGuard<osal::Mutex> g(mailbox_);
    command_ = Command::Forget;
    wipe(pending_);
  }
  if (jobs_) jobs_->requestRun(*this);
}

JobPolicy WifiManager::policy() const {
  JobPolicy p;
  // Poll quickly while something is in progress, slowly when settled.
  p.intervalMs = (connecting_ || scanning_) ? 250 : 5000;
  p.retryMs = 5000;
  p.maxBackoffMs = kMaxBackoffMs;
  return p;
}

void WifiManager::startConnect(uint32_t now) {
  if (!creds_.ssid[0]) return;
  connecting_ = true;
  attemptMs_ = now;
  current_.state = WifiState::Connecting;
  current_.connected = false;
  copyText(current_.ssid, sizeof(current_.ssid), creds_.ssid);
  driver_->connect(creds_.ssid, creds_.password);
  LOGI(kTag, "connecting (ssid length %u)", (unsigned)strlen(creds_.ssid));
}

void WifiManager::pollScan() {
  WifiNetwork raw[24];
  const int n = driver_->scanResults(raw, 24);
  if (n == -1) return;  // still scanning
  scanning_ = false;
  WifiScan result;
  result.sequence = ++scanSequence_;
  if (n < 0) {
    result.failed = true;
  } else {
    // Strongest first, one entry per SSID, hidden networks skipped.
    for (int i = 0; i < n; ++i) {
      if (!raw[i].ssid[0]) continue;
      int existing = -1;
      for (int j = 0; j < result.count; ++j)
        if (strcmp(result.networks[j].ssid, raw[i].ssid) == 0) existing = j;
      if (existing >= 0) {
        if (raw[i].rssi > result.networks[existing].rssi) result.networks[existing] = raw[i];
        continue;
      }
      if (result.count < WifiScan::kMax) {
        result.networks[result.count++] = raw[i];
      } else if (raw[i].rssi > result.networks[WifiScan::kMax - 1].rssi) {
        result.networks[WifiScan::kMax - 1] = raw[i];
      } else {
        continue;
      }
      for (int j = result.count - 1; j > 0 && result.networks[j].rssi > result.networks[j - 1].rssi; --j) {
        const WifiNetwork t = result.networks[j];
        result.networks[j] = result.networks[j - 1];
        result.networks[j - 1] = t;
      }
    }
  }
  scan_.publish(result);
}

void WifiManager::publish() {
  const NetworkStatus old = status_.get();
  const bool changed = old.state != current_.state || old.connected != current_.connected ||
                       old.timeSynced != current_.timeSynced || strcmp(old.ssid, current_.ssid) != 0 ||
                       strcmp(old.ip, current_.ip) != 0 || strcmp(old.error, current_.error) != 0 ||
                       signalBars(old.rssi) != signalBars(current_.rssi);
  if (changed) status_.publish(current_);
}

JobResult WifiManager::run(uint32_t now) {
  if (!driver_) return JobResult::Ok;
  if (!begun_) {
    begun_ = driver_->begin();
    if (!begun_) {
      copyText(current_.error, sizeof(current_.error), "Wi-Fi radio unavailable");
      publish();
      return JobResult::Failed;
    }
    if (creds_.ssid[0]) startConnect(now);
  }

  Command command;
  WifiCredentials incoming;
  {
    osal::LockGuard<osal::Mutex> g(mailbox_);
    command = command_;
    incoming = pending_;
    command_ = Command::None;
    wipe(pending_);
  }
  if (command == Command::Forget) {
    driver_->disconnect();
    if (store_) store_->erase(kNamespace, kKey);
    wipe(creds_);
    connecting_ = false;
    current_ = NetworkStatus{};
    current_.present = true;
    current_.state = WifiState::NotConfigured;
    current_.timeSynced = syncStarted_;
    LOGI(kTag, "saved network forgotten");
  } else if (command == Command::Join) {
    creds_ = incoming;
    if (!saveRecord(store_, kNamespace, kKey, WifiCredentials::kVersion, &creds_, sizeof(creds_))) {
      LOGW(kTag, "credentials could not be saved; they will be lost on restart");
    }
    driver_->disconnect();
    current_.error[0] = '\0';
    backoffMs_ = 5000;
    startConnect(now);
  }
  wipe(incoming);

  if (scanRequested_.exchange(false) && !scanning_) {
    scanning_ = driver_->startScan();
    WifiScan s = scan_.get();
    s.scanning = scanning_;
    s.failed = !scanning_;
    scan_.publish(s);
  }
  if (scanning_) pollScan();

  if (creds_.ssid[0]) {
    const WifiLink link = driver_->link();
    if (link == WifiLink::Connected) {
      if (!current_.connected) {
        LOGI(kTag, "connected");
        backoffMs_ = 5000;
      }
      connecting_ = false;
      current_.connected = true;
      current_.state = WifiState::Connected;
      current_.error[0] = '\0';
      current_.rssi = driver_->rssi();
      driver_->address(current_.ip, sizeof(current_.ip));
      if (!syncStarted_) {
        driver_->startTimeSync();
        syncStarted_ = true;
      }
    } else {
      const bool wasConnected = current_.connected;
      current_.connected = false;
      current_.ip[0] = '\0';
      // Act on the transition only: a driver may keep reporting Failed until
      // the next connect(), which must not keep pushing the retry back.
      const bool attemptFailed =
          connecting_ && (link == WifiLink::Failed || elapsedMs(now, attemptMs_) >= kConnectTimeoutMs);
      if (attemptFailed || wasConnected) {
        snprintf(current_.error, sizeof(current_.error), wasConnected ? "Connection lost" : "Couldn't join %.20s",
                 creds_.ssid);
        driver_->disconnect();
        connecting_ = false;
        current_.state = WifiState::Disconnected;
        nextAttemptMs_ = now + backoffMs_;
        backoffMs_ = backoffMs_ * 2 > kMaxBackoffMs ? kMaxBackoffMs : backoffMs_ * 2;
      } else if (!connecting_ && reached(now, nextAttemptMs_)) {
        startConnect(now);
      }
    }
  }
  current_.timeSynced = syncStarted_;
  publish();
  if (jobs_) jobs_->setNetworkAvailable(current_.connected);
  return JobResult::Ok;
}

}  // namespace stella
