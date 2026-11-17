#pragma once
// Arbitration for one scarce resource shared by several threads, typically
// the single TLS session a PSRAM-less ESP32 can afford (from Celestia
// Nightstand's network lease).
//
//   Primary  clients wait (bounded) for the resource.
//   Optional clients only try; they never start while a Primary client is
//            waiting, and hand the resource back if one arrived meanwhile.
//
// A lease that cannot be acquired means "deferred" (JobResult::Deferred), never
// "failed": keep last-good data and retry shortly. Leases are recursive for
// the owning thread. Lock ordering: take a resource lease before any data lock.
#include <atomic>
#include <stdint.h>

#include "../osal/osal.h"

namespace stella {

enum class LeasePriority : uint8_t { Primary, Optional };

class SharedResource {
 public:
  static constexpr uint32_t kPrimaryWaitMs = 30000;

  bool acquire(LeasePriority priority, uint32_t waitMs = kPrimaryWaitMs) {
    if (priority == LeasePriority::Optional) {
      if (primaryWaiting_.load() != 0 || !mutex_.lock(0)) {
        optionalDeferred_.fetch_add(1);
        return false;
      }
      if (primaryWaiting_.load() != 0) {  // A Primary client arrived meanwhile.
        mutex_.unlock();
        optionalDeferred_.fetch_add(1);
        return false;
      }
      return true;
    }
    primaryWaiting_.fetch_add(1);
    const bool held = mutex_.lock(waitMs);
    primaryWaiting_.fetch_sub(1);
    if (!held) primaryTimeouts_.fetch_add(1);
    return held;
  }
  void release() { mutex_.unlock(); }

  bool primaryWaiting() const { return primaryWaiting_.load() != 0; }
  uint32_t optionalDeferred() const { return optionalDeferred_.load(); }
  uint32_t primaryTimeouts() const { return primaryTimeouts_.load(); }

 private:
  osal::RecursiveMutex mutex_;
  std::atomic<uint8_t> primaryWaiting_{0};
  std::atomic<uint32_t> optionalDeferred_{0}, primaryTimeouts_{0};
};

class Lease {
 public:
  Lease(SharedResource &r, LeasePriority p, uint32_t waitMs = SharedResource::kPrimaryWaitMs)
      : r_(r), held_(r.acquire(p, waitMs)) {}
  ~Lease() {
    if (held_) r_.release();
  }
  Lease(const Lease &) = delete;
  Lease &operator=(const Lease &) = delete;
  explicit operator bool() const { return held_; }

 private:
  SharedResource &r_;
  bool held_;
};

}  // namespace stella
