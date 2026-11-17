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
  return true;
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
    switch (r) {
      case JobResult::Ok:
        e.failures = 0;
        e.dueMs = now + p.intervalMs;
        break;
      case JobResult::Failed:
        if (e.failures < 255) ++e.failures;
        e.dueMs = now + backoff(p, e.failures);
        break;
      case JobResult::Deferred:
        e.dueMs = now + p.deferMs;
        break;
    }
  }

  now = clock(context);
  uint32_t wait = kIdleWaitMs;
  for (size_t i = 0; i < count_; ++i) wait = minWait(wait, remainingMs(now, entries_[i].dueMs));
  return wait;
}

}  // namespace stella
