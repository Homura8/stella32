#pragma once
// Task liveness supervision (from Celestia Nightstand Stage A).
//
// Every long-running thread "beats" after each unit of work. A thread silent
// for longer than its own limit is considered wedged (for example inside a
// network stack call that ignored its timeouts); the runtime then records
// which thread stalled in retained memory and restarts the device instead of
// showing stale data until someone power-cycles it.
//
// Limits must be several times the worst case between two beats.
#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace stella {

class Supervisor {
 public:
  static constexpr unsigned kMaxThreads = 6;

  // Returns a watch id, or -1 if the table is full or the limit is invalid.
  // Register threads before they start.
  int watch(const char *name, uint32_t limitMs, uint32_t nowMs);
  void retire(int id);
  void beat(int id, uint32_t nowMs);
  uint32_t age(int id, uint32_t nowMs) const;  // 0 for a beat newer than nowMs
  int firstStalled(uint32_t nowMs, uint32_t &stalledForMs) const;
  const char *name(int id) const;
  unsigned size() const { return count_; }

 private:
  struct Slot {
    const char *name = nullptr;
    uint32_t limitMs = 0;
    std::atomic<uint32_t> lastBeat{0};
    std::atomic<bool> active{false};
  };
  bool valid(int id) const { return id >= 0 && (unsigned)id < count_; }
  Slot slots_[kMaxThreads];
  unsigned count_ = 0;
};

// Lives in Platform::retainedMemory(); validated by magic + checksum because
// the memory is random after power-on.
struct RestartRecord {
  uint32_t magic;
  uint32_t restarts;
  uint32_t stalledMs;
  uint32_t pending;
  char thread[16];
  uint32_t check;
};

namespace restart_record {
bool valid(const RestartRecord &r);
void reset(RestartRecord &r);
void note(RestartRecord &r, const char *thread, uint32_t stalledMs);
// True exactly once after a supervisor restart.
bool takePending(RestartRecord &r);
}  // namespace restart_record

}  // namespace stella
