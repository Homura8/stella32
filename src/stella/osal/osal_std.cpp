// Host implementation of the OS abstraction layer (C++ standard library).
#if !defined(ESP_PLATFORM)

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <new>
#include <thread>

#include "osal.h"

namespace stella {
namespace osal {
namespace {

template <class T, class Storage>
T &as(Storage &storage) {
  return *reinterpret_cast<T *>(storage);
}

template <class M>
bool timedLock(M &m, uint32_t timeoutMs) {
  if (timeoutMs == kForever) {
    m.lock();
    return true;
  }
  return m.try_lock_for(std::chrono::milliseconds(timeoutMs));
}

struct SignalState {
  std::mutex mutex;
  std::condition_variable cv;
  bool pending = false;
};

}  // namespace

static_assert(sizeof(std::timed_mutex) <= 96, "Mutex storage too small");
static_assert(sizeof(std::recursive_timed_mutex) <= 96, "RecursiveMutex storage too small");
static_assert(sizeof(SignalState) <= 160, "Signal storage too small");

Mutex::Mutex() { new (storage_) std::timed_mutex(); }
Mutex::~Mutex() { as<std::timed_mutex>(storage_).~timed_mutex(); }
bool Mutex::lock(uint32_t timeoutMs) { return timedLock(as<std::timed_mutex>(storage_), timeoutMs); }
void Mutex::unlock() { as<std::timed_mutex>(storage_).unlock(); }

RecursiveMutex::RecursiveMutex() { new (storage_) std::recursive_timed_mutex(); }
RecursiveMutex::~RecursiveMutex() { as<std::recursive_timed_mutex>(storage_).~recursive_timed_mutex(); }
bool RecursiveMutex::lock(uint32_t timeoutMs) { return timedLock(as<std::recursive_timed_mutex>(storage_), timeoutMs); }
void RecursiveMutex::unlock() { as<std::recursive_timed_mutex>(storage_).unlock(); }

Signal::Signal() { new (storage_) SignalState(); }
Signal::~Signal() { as<SignalState>(storage_).~SignalState(); }
void Signal::notify() {
  auto &s = as<SignalState>(storage_);
  {
    std::lock_guard<std::mutex> g(s.mutex);
    s.pending = true;
  }
  s.cv.notify_one();
}
bool Signal::wait(uint32_t timeoutMs) {
  auto &s = as<SignalState>(storage_);
  std::unique_lock<std::mutex> g(s.mutex);
  if (timeoutMs == kForever) {
    s.cv.wait(g, [&] { return s.pending; });
  } else if (!s.cv.wait_for(g, std::chrono::milliseconds(timeoutMs), [&] { return s.pending; })) {
    return false;
  }
  s.pending = false;
  return true;
}

bool startThread(const ThreadSpec &, ThreadEntry entry, void *arg) {
  std::thread(entry, arg).detach();
  return true;
}

void sleepMs(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

}  // namespace osal
}  // namespace stella

#endif
