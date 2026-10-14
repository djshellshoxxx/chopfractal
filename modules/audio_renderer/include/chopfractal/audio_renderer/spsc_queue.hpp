#pragma once
// Fixed-size single-producer / single-consumer queue. push() and pop() are wait-free, never allocate and never block;
// a full queue rejects the push (the caller counts the drop). One thread may push, one other thread may pop.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace chopfractal::render {

template <class T, std::size_t N>
class SpscQueue {
  static_assert(N >= 2, "a queue needs at least two slots");

 public:
  bool push(const T& value) {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = (head + 1) % (N + 1);
    if (next == tail_.load(std::memory_order_acquire)) return false;  // full
    items_[head] = value;
    head_.store(next, std::memory_order_release);
    return true;
  }
  bool pop(T& out) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;  // empty
    out = items_[tail];
    tail_.store((tail + 1) % (N + 1), std::memory_order_release);
    return true;
  }
  bool empty() const { return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire); }
  static constexpr std::size_t capacity() { return N; }

 private:
  std::array<T, N + 1> items_{};  // one spare slot distinguishes full from empty
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

}  // namespace chopfractal::render
