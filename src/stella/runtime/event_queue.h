#pragma once
// Bounded multi-producer event queue feeding the UI thread. Producers (service
// threads, ports) push; the UI thread drains. Overflow drops the event and is
// counted; topics coalesce, so overflow means a real bug, not load.
#include <atomic>
#include <stdint.h>

#include "../core/containers.h"
#include "../input/event.h"
#include "../osal/osal.h"

namespace stella {

class EventQueue {
 public:
  static constexpr size_t kCapacity = 32;

  bool push(const Event &e) {
    bool ok;
    {
      osal::LockGuard<osal::Mutex> g(mutex_);
      ok = ring_.push(e);
    }
    if (!ok) dropped_.fetch_add(1);
    wake_.notify();
    return ok;
  }
  bool pop(Event &out) {
    osal::LockGuard<osal::Mutex> g(mutex_);
    return ring_.pop(out);
  }
  // Wake the UI thread early (also used for non-queue reasons).
  void wake() { wake_.notify(); }
  bool wait(uint32_t timeoutMs) { return wake_.wait(timeoutMs); }
  uint32_t dropped() const { return dropped_.load(); }

 private:
  osal::Mutex mutex_;
  osal::Signal wake_;
  RingBuffer<Event, kCapacity> ring_;
  std::atomic<uint32_t> dropped_{0};
};

}  // namespace stella
