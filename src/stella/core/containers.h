#pragma once
// Fixed-capacity containers. Stella never allocates on the heap after boot, so
// every collection states its capacity at compile time and reports overflow.
#include <stddef.h>
#include <stdint.h>

namespace stella {

template <class T, size_t Capacity>
class FixedVector {
 public:
  bool push(const T &value) {
    if (count_ >= Capacity) return false;
    items_[count_++] = value;
    return true;
  }
  bool removeAt(size_t index) {
    if (index >= count_) return false;
    for (size_t i = index + 1; i < count_; ++i) items_[i - 1] = items_[i];
    --count_;
    return true;
  }
  void clear() { count_ = 0; }
  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  bool full() const { return count_ == Capacity; }
  static constexpr size_t capacity() { return Capacity; }
  T &operator[](size_t i) { return items_[i]; }
  const T &operator[](size_t i) const { return items_[i]; }
  T &back() { return items_[count_ - 1]; }
  const T &back() const { return items_[count_ - 1]; }
  T *begin() { return items_; }
  T *end() { return items_ + count_; }
  const T *begin() const { return items_; }
  const T *end() const { return items_ + count_; }

 private:
  T items_[Capacity]{};
  size_t count_ = 0;
};

// Single-threaded FIFO. Thread-safe queues wrap this with a mutex.
template <class T, size_t Capacity>
class RingBuffer {
 public:
  bool push(const T &value) {
    if (count_ == Capacity) return false;
    items_[(head_ + count_) % Capacity] = value;
    ++count_;
    return true;
  }
  bool pop(T &out) {
    if (count_ == 0) return false;
    out = items_[head_];
    head_ = (head_ + 1) % Capacity;
    --count_;
    return true;
  }
  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  static constexpr size_t capacity() { return Capacity; }

 private:
  T items_[Capacity]{};
  size_t head_ = 0, count_ = 0;
};

}  // namespace stella
