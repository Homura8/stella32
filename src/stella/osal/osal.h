#pragma once
// OS abstraction layer: the only part of Stella that knows which kernel it runs
// on. Implementations: FreeRTOS (osal_freertos.cpp, ESP-IDF/Arduino-ESP32) and
// the C++ standard library (osal_std.cpp, host builds and the simulator).
//
// Synchronisation objects keep their kernel object in fixed inline storage, so
// creating one never allocates heap. They are not copyable or movable.
#include <stddef.h>
#include <stdint.h>

namespace stella {
namespace osal {

constexpr uint32_t kForever = 0xFFFFFFFFu;

class Mutex {
 public:
  Mutex();
  ~Mutex();
  Mutex(const Mutex &) = delete;
  Mutex &operator=(const Mutex &) = delete;
  bool lock(uint32_t timeoutMs = kForever);
  void unlock();

 private:
  alignas(8) unsigned char storage_[96];
};

// Re-entrant by the owning thread; each lock() needs a matching unlock().
class RecursiveMutex {
 public:
  RecursiveMutex();
  ~RecursiveMutex();
  RecursiveMutex(const RecursiveMutex &) = delete;
  RecursiveMutex &operator=(const RecursiveMutex &) = delete;
  bool lock(uint32_t timeoutMs = kForever);
  void unlock();

 private:
  alignas(8) unsigned char storage_[96];
};

// Binary wake-up signal: notify() from any thread, wait() in one consumer.
// Multiple notifications before a wait collapse into one.
class Signal {
 public:
  Signal();
  ~Signal();
  Signal(const Signal &) = delete;
  Signal &operator=(const Signal &) = delete;
  void notify();
  bool wait(uint32_t timeoutMs);  // true if notified, false on timeout

 private:
  alignas(8) unsigned char storage_[160];
};

// Scoped lock for any type with lock(timeout)/unlock().
template <class M>
class LockGuard {
 public:
  explicit LockGuard(M &m, uint32_t timeoutMs = kForever) : m_(m), held_(m.lock(timeoutMs)) {}
  ~LockGuard() {
    if (held_) m_.unlock();
  }
  LockGuard(const LockGuard &) = delete;
  LockGuard &operator=(const LockGuard &) = delete;
  explicit operator bool() const { return held_; }

 private:
  M &m_;
  bool held_;
};

struct ThreadSpec {
  const char *name = "stella";
  uint32_t stackBytes = 4096;
  uint8_t priority = 1;  // FreeRTOS priority; ignored on host
  int8_t core = -1;      // -1 = no affinity
};

using ThreadEntry = void (*)(void *arg);

// Start a detached thread that runs for the lifetime of the system.
bool startThread(const ThreadSpec &spec, ThreadEntry entry, void *arg);
void sleepMs(uint32_t ms);

}  // namespace osal
}  // namespace stella
