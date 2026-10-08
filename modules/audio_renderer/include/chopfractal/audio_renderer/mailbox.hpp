#pragma once
// Wait-free handoff of immutable objects from non-real-time publishers to one real-time reader.
//
//  * publish()  : any non-RT thread (serialized by a mutex). Allocates; never called from the audio thread.
//  * acquire()  : the single RT thread only. Wait-free, allocation-free, never frees anything.
//  * Reclamation: an object is freed only after the RT thread has acknowledged a strictly newer one, so
//                 the RT thread can never touch freed memory. Freeing happens on the publishing thread.
// The Mailbox must outlive the RT thread's last call to acquire().
#include <cstddef>
#include <utility>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace chopfractal::render {

template <class T>
class Mailbox {
 public:
  void publish(std::shared_ptr<const T> value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto slot = std::make_unique<Slot>();
    slot->value = std::move(value);
    slot->generation = ++counter_;
    const Slot* raw = slot.get();
    owned_.push_back(std::move(slot));
    pending_.store(raw, std::memory_order_release);
    collectLocked();
  }

  // RT thread only. Returns the newest published object, or nullptr if nothing was ever published.
  const T* acquire() {
    const Slot* p = pending_.load(std::memory_order_acquire);
    if (p != nullptr && p != active_) {
      active_ = p;
      activeGeneration_.store(p->generation, std::memory_order_release);
    }
    return active_ ? active_->value.get() : nullptr;
  }

  // Non-RT: drop objects the RT thread can no longer be using.
  void collect() {
    std::lock_guard<std::mutex> lock(mutex_);
    collectLocked();
  }

  std::size_t retainedForTesting() {
    std::lock_guard<std::mutex> lock(mutex_);
    return owned_.size();
  }

 private:
  struct Slot {
    std::shared_ptr<const T> value;
    std::uint64_t generation = 0;
  };

  void collectLocked() {
    const std::uint64_t active = activeGeneration_.load(std::memory_order_acquire);
    // Keep: anything not older than the acknowledged generation (this includes the newest, still
    // unacknowledged object). While no reader is running, bound the backlog: a reader that starts later
    // only ever jumps to the newest pending object.
    std::size_t keepFrom = 0;
    while (keepFrom < owned_.size() && owned_[keepFrom]->generation < active) ++keepFrom;
    if (active == 0 && owned_.size() > kInactiveBacklog) keepFrom = owned_.size() - kInactiveBacklog;
    if (keepFrom > 0) owned_.erase(owned_.begin(), owned_.begin() + static_cast<std::ptrdiff_t>(keepFrom));
  }

  static constexpr std::size_t kInactiveBacklog = 16;
  std::mutex mutex_;
  std::vector<std::unique_ptr<Slot>> owned_;  // ordered by generation
  std::uint64_t counter_ = 0;
  std::atomic<const Slot*> pending_{nullptr};
  std::atomic<std::uint64_t> activeGeneration_{0};
  const Slot* active_ = nullptr;  // touched by the RT thread only
};

}  // namespace chopfractal::render
