#pragma once
// Periodic background work with explicit retry policy.
//
// Jobs run on a service thread, never on the UI thread. Each run returns:
//   Ok       - schedule the next run after policy.intervalMs; reset backoff.
//   Failed   - retry after retryMs, doubling per consecutive failure up to
//              maxBackoffMs.
//   Deferred - a shared resource (e.g. the TLS lease) was busy. This is NOT a
//              failure: keep last-good data and backoff, retry after deferMs.
//
// Deadlines are wrap-safe; zero is an ordinary time value, not "never".
#include <atomic>
#include <stddef.h>
#include <stdint.h>

#include "../osal/osal.h"

namespace stella {

enum class JobResult : uint8_t { Ok, Failed, Deferred };

struct JobPolicy {
  uint32_t intervalMs = 60000;
  uint32_t retryMs = 10000;
  uint32_t maxBackoffMs = 240000;
  uint32_t deferMs = 2000;
  bool needsNetwork = false;  // skipped (not failed) while the network is down
};

class Job {
 public:
  virtual ~Job() = default;
  virtual const char *name() const = 0;
  virtual JobPolicy policy() const = 0;
  virtual JobResult run(uint32_t nowMs) = 0;
};

class JobScheduler {
 public:
  static constexpr size_t kMaxJobs = 12;
  static constexpr uint32_t kIdleWaitMs = 1000;  // upper bound between checks

  struct Entry {
    Job *job = nullptr;
    uint32_t dueMs = 0;
    uint8_t failures = 0;
    JobResult last = JobResult::Ok;
    bool everRan = false;
    std::atomic<bool> runNow{false};
  };

  using Clock = uint32_t (*)(void *context);

  bool add(Job &job, uint32_t nowMs, uint32_t firstDelayMs = 0);

  // Thread-safe requests, applied at the start of the next runDue().
  void requestRefresh() { refresh_.store(true); }           // all jobs due now, backoff cleared
  void setNetworkAvailable(bool up) { network_.store(up ? 1 : 0); }
  // Run one job as soon as possible (thread-safe; keeps its backoff state).
  bool requestRun(const Job &job);
  // Signal to notify when a request arrives, so a sleeping service thread
  // wakes immediately instead of at its next deadline.
  void setWakeSignal(osal::Signal *signal) { wake_ = signal; }

  // Runs every due job once (reading the clock after each job, because jobs
  // can take seconds). Returns milliseconds until the next job is due.
  uint32_t runDue(Clock clock, void *context);

  size_t size() const { return count_; }
  const Entry &entry(size_t i) const { return entries_[i]; }

 private:
  uint32_t backoff(const JobPolicy &p, uint8_t failures) const;

  Entry entries_[kMaxJobs];
  size_t count_ = 0;
  std::atomic<bool> refresh_{false};
  std::atomic<int8_t> network_{1};
  bool networkUp_ = true;
  osal::Signal *wake_ = nullptr;
};

}  // namespace stella
