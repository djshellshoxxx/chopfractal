#pragma once
// Fixed-size single-producer / single-consumer queue for the real-time path. Never allocates, never blocks.
//   * tryPush(): producer thread only (false when full, nothing is lost or overwritten).
//   * tryPop():  consumer thread only (false when empty).
// Either side may be the audio thread: the UI pushes pad presses that the audio thread pops, and the audio thread
// pushes recorded pad events that the message thread pops.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace chopfractal::render {

template <class T, std::size_t N>
class SpscQueue {
  static_assert(std::is_trivially_copyable_v<T>, "queue elements are copied with plain assignment");
  static_assert(N >= 2 && (N & (N - 1)) == 0, "capacity must be a power of two");

 public:
  bool tryPush(const T& value) {
    const std::uint32_t head = head_.load(std::memory_order_relaxed);
    const std::uint32_t tail = tail_.load(std::memory_order_acquire);
    if (head - tail >= N) return false;
    slots_[head & (N - 1)] = value;
    head_.store(head + 1, std::memory_order_release);
    return true;
  }
  bool tryPop(T& out) {
    const std::uint32_t tail = tail_.load(std::memory_order_relaxed);
    const std::uint32_t head = head_.load(std::memory_order_acquire);
    if (tail == head) return false;
    out = slots_[tail & (N - 1)];
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }
  std::size_t sizeApprox() const {
    const std::uint32_t head = head_.load(std::memory_order_relaxed);
    const std::uint32_t tail = tail_.load(std::memory_order_relaxed);
    return static_cast<std::size_t>(head - tail);
  }
  static constexpr std::size_t capacity() { return N; }

 private:
  T slots_[N]{};
  alignas(64) std::atomic<std::uint32_t> head_{0};
  alignas(64) std::atomic<std::uint32_t> tail_{0};
};

}  // namespace chopfractal::render
