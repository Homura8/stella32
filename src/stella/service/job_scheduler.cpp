#include "job_scheduler.h"

#include "../core/deadline.h"
#include "../core/log.h"

namespace stella {

bool JobScheduler::add(Job &job, uint32_t nowMs, uint32_t firstDelayMs) {
  if (count_ >= kMaxJobs) {
    LOGE("jobs", "capacity reached; '%s' not scheduled", job.name());
    return false;
  }
  Entry &e = entries_[count_++];
  e.job = &job;
  e.dueMs = nowMs + firstDelayMs;
  e.runNow.store(false);
  return true;
}

bool JobScheduler::requestRun(const Job &job) {
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].job == &job) {
      entries_[i].runNow.store(true);
      if (wake_) wake_->notify();
      return true;
    }
  }
  return false;
}

uint32_t JobScheduler::backoff(const JobPolicy &p, uint8_t failures) const {
  uint32_t wait = p.retryMs;
  for (uint8_t i = 1; i < failures && wait < p.maxBackoffMs; ++i) wait *= 2;
  return wait < p.maxBackoffMs ? wait : p.maxBackoffMs;
}

uint32_t JobScheduler::runDue(Clock clock, void *context) {
  uint32_t now = clock(context);
  if (refresh_.exchange(false)) {
    for (size_t i = 0; i < count_; ++i) {
      entries_[i].dueMs = now;
      entries_[i].failures = 0;
    }
  }
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].runNow.exchange(false)) entries_[i].dueMs = now;
  }
  const int8_t network = network_.load();
  const bool up = network != 0;
  if (up && !networkUp_) {
    // Reconnected: outage backoff is meaningless, refresh network jobs now.
    for (size_t i = 0; i < count_; ++i) {
      if (entries_[i].job->policy().needsNetwork) {
        entries_[i].dueMs = now;
        entries_[i].failures = 0;
      }
    }
  }
  networkUp_ = up;

  for (size_t i = 0; i < count_; ++i) {
    Entry &e = entries_[i];
    now = clock(context);
    if (!reached(now, e.dueMs)) continue;
    const JobPolicy p = e.job->policy();
    if (p.needsNetwork && !networkUp_) {
      e.dueMs = now + JobScheduler::kIdleWaitMs;  // skipped, not failed
      continue;
    }
    const JobResult r = e.job->run(now);
    now = clock(context);
    e.last = r;
    e.everRan = true;
    // A job's policy may depend on its state (e.g. poll faster while
    // connecting), so schedule with the policy as it is after the run.
    const JobPolicy after = e.job->policy();
    switch (r) {
      case JobResult::Ok:
        e.failures = 0;
        e.dueMs = now + after.intervalMs;
        break;
      case JobResult::Failed:
        if (e.failures < 255) ++e.failures;
        e.dueMs = now + backoff(after, e.failures);
        break;
      case JobResult::Deferred:
        e.dueMs = now + after.deferMs;
        break;
    }
  }

  now = clock(context);
  uint32_t wait = kIdleWaitMs;
  for (size_t i = 0; i < count_; ++i) wait = minWait(wait, remainingMs(now, entries_[i].dueMs));
  return wait;
}

}  // namespace stella
