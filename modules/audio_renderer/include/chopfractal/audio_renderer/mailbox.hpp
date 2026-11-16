#pragma once
// Wait-free handoff of immutable objects from non-real-time publishers to one real-time reader.
//
//  * publish()  : any non-RT thread (serialized by a mutex). Allocates; never called from the audio thread.
//  * acquire()  : the single RT thread only. Wait-free, allocation-free, never frees anything.
//  * Reclamation: an object is freed only when it is neither the RT thread's active/candidate slot nor the newest
//                 pending one, so the RT thread can never touch freed memory and retention is bounded (at most 3)
//                 even if the RT thread never calls acquire(). Freeing happens on the publishing thread.
// The Mailbox must outlive the RT thread's last call to acquire().
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
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
    // seq_cst, not release: this store and the candidate_ load in collectLocked() form a Dekker pair with the RT thread's
    // candidate_ store and pending_ re-check. A release store may be reordered after the later load (StoreLoad, allowed on
    // x86), which let the RT thread adopt the previous slot while the collector freed it.
    pending_.store(raw, std::memory_order_seq_cst);
    collectLocked();
  }

  // RT thread only. Returns the newest published object, or nullptr if nothing was ever published.
  // The candidate slot is announced through `candidate_` and re-validated against `pending_` before it becomes active, so a
  // publisher never frees a slot between the RT thread reading the pointer and acknowledging it (hazard-pointer handshake).
  // The retry count is bounded: on a lost race the previous (still protected) object stays active until the next call.
  const T* acquire() {
    for (int attempt = 0; attempt < 4; ++attempt) {
      const Slot* p = pending_.load(std::memory_order_seq_cst);
      if (p == nullptr || p == active_) break;
      candidate_.store(p, std::memory_order_seq_cst);
      if (pending_.load(std::memory_order_seq_cst) == p) {
        active_ = p;
        activeSlot_.store(p, std::memory_order_seq_cst);
        break;
      }
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

  // Only three slots can be in use: the one the RT thread reads (active), the one it is about to adopt (candidate) and the
  // newest published (pending). Everything else, including superseded items the RT thread never saw (a bypassed track
  // while the UI keeps publishing), is freed here on the publishing thread, so retention stays bounded at three.
  void collectLocked() {
    // candidate_ before activeSlot_: the RT thread stores activeSlot_ = p before it overwrites candidate_ with a newer slot,
    // so if this load already sees the newer candidate, the activeSlot_ load below sees p or later (p is never missed).
    const Slot* keepC = candidate_.load(std::memory_order_seq_cst);
    const Slot* keepA = activeSlot_.load(std::memory_order_seq_cst);
    const Slot* keepP = pending_.load(std::memory_order_seq_cst);
    std::size_t w = 0;
    for (std::size_t r = 0; r < owned_.size(); ++r) {
      const Slot* s = owned_[r].get();
      if (s == keepA || s == keepC || s == keepP) {
        if (w != r) owned_[w] = std::move(owned_[r]);
        ++w;
      }
    }
    owned_.resize(w);
  }

  std::mutex mutex_;
  std::vector<std::unique_ptr<Slot>> owned_;  // ordered by generation
  std::uint64_t counter_ = 0;
  std::atomic<const Slot*> pending_{nullptr};
  std::atomic<const Slot*> candidate_{nullptr};   // RT thread's announced candidate
  std::atomic<const Slot*> activeSlot_{nullptr};  // slot the RT thread is reading
  const Slot* active_ = nullptr;                  // touched by the RT thread only
};

}  // namespace chopfractal::render
