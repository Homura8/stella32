#include "supervisor.h"

#include "../core/text.h"

namespace stella {

int Supervisor::watch(const char *name, uint32_t limitMs, uint32_t nowMs) {
  if (count_ >= kMaxThreads || limitMs == 0 || limitMs > 0x7FFFFFFFu) return -1;
  Slot &s = slots_[count_];
  s.name = name ? name : "?";
  s.limitMs = limitMs;
  s.lastBeat.store(nowMs);
  s.active.store(true);
  return (int)count_++;
}

void Supervisor::retire(int id) {
  if (valid(id)) slots_[id].active.store(false);
}

void Supervisor::beat(int id, uint32_t nowMs) {
  if (valid(id)) slots_[id].lastBeat.store(nowMs);
}

namespace {
uint32_t ageOf(uint32_t lastBeat, uint32_t nowMs) {
  const int32_t d = (int32_t)(nowMs - lastBeat);
  return d > 0 ? (uint32_t)d : 0;
}
}  // namespace

uint32_t Supervisor::age(int id, uint32_t nowMs) const {
  return valid(id) ? ageOf(slots_[id].lastBeat.load(), nowMs) : 0;
}

int Supervisor::firstStalled(uint32_t nowMs, uint32_t &stalledForMs) const {
  for (unsigned i = 0; i < count_; ++i) {
    if (!slots_[i].active.load()) continue;
    const uint32_t a = ageOf(slots_[i].lastBeat.load(), nowMs);
    if (a > slots_[i].limitMs) {
      stalledForMs = a;
      return (int)i;
    }
  }
  stalledForMs = 0;
  return -1;
}

const char *Supervisor::name(int id) const { return valid(id) ? slots_[id].name : "?"; }

namespace restart_record {
namespace {
constexpr uint32_t kMagic = 0x53545252u;  // "STRR"
uint32_t checksum(const RestartRecord &r) { return hash32(&r, offsetof(RestartRecord, check)); }
}  // namespace

bool valid(const RestartRecord &r) {
  return r.magic == kMagic && r.thread[sizeof(r.thread) - 1] == '\0' && r.check == checksum(r);
}

void reset(RestartRecord &r) {
  memset(&r, 0, sizeof(r));
  r.magic = kMagic;
  r.check = checksum(r);
}

void note(RestartRecord &r, const char *thread, uint32_t stalledMs) {
  if (!valid(r)) reset(r);
  ++r.restarts;
  r.stalledMs = stalledMs;
  r.pending = 1;
  memset(r.thread, 0, sizeof(r.thread));
  copyText(r.thread, sizeof(r.thread), thread);
  r.check = checksum(r);
}

bool takePending(RestartRecord &r) {
  if (!valid(r)) {
    reset(r);
    return false;
  }
  if (!r.pending) return false;
  r.pending = 0;
  r.check = checksum(r);
  return true;
}

}  // namespace restart_record
}  // namespace stella
