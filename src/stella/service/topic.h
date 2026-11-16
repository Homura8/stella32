#pragma once
// Versioned data published by services (any thread) and read by apps (UI
// thread). This is the only way data crosses threads: no shared globals.
//
// publish() copies the value under a mutex and posts one "topic changed"
// notification; further publishes before the UI thread reads it coalesce, so a
// chatty service can never flood the event queue.
#include <atomic>
#include <stdint.h>

#include "../osal/osal.h"

namespace stella {

class TopicNotifier {
 public:
  virtual ~TopicNotifier() = default;
  virtual void topicChanged(uint16_t topicId) = 0;  // thread-safe
};

class TopicBase {
 public:
  explicit TopicBase(uint16_t id) : id_(id) {}
  virtual ~TopicBase() = default;
  uint16_t id() const { return id_; }
  uint32_t version() const { return version_.load(); }
  void attach(TopicNotifier *notifier) { notifier_ = notifier; }
  // Called by the runtime just before delivering the change to subscribers.
  void acknowledge() { pending_.store(false); }

 protected:
  void notify() {
    version_.fetch_add(1);
    if (notifier_ && !pending_.exchange(true)) notifier_->topicChanged(id_);
  }

 private:
  const uint16_t id_;
  std::atomic<uint32_t> version_{0};
  std::atomic<bool> pending_{false};
  TopicNotifier *notifier_ = nullptr;
};

// T must be trivially copyable and bounded in size.
template <class T>
class Topic : public TopicBase {
 public:
  explicit Topic(uint16_t id, const T &initial = T{}) : TopicBase(id), value_(initial) {}

  void publish(const T &value) {
    {
      osal::LockGuard<osal::Mutex> g(mutex_);
      value_ = value;
    }
    notify();
  }
  // Copies the current value; returns its version (0 = never published).
  uint32_t read(T &out) const {
    osal::LockGuard<osal::Mutex> g(mutex_);
    out = value_;
    return version();
  }
  T get() const {
    T copy;
    read(copy);
    return copy;
  }

 private:
  mutable osal::Mutex mutex_;
  T value_;
};

}  // namespace stella
