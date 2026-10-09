#pragma once
// Bounded snapshot stack for undo/redo of immutable value states.
#include <cstddef>
#include <utility>
#include <vector>

namespace chopfractal {

template <class T>
class SnapshotStack {
 public:
  explicit SnapshotStack(T initial, std::size_t capacity = 128) : capacity_(capacity < 2 ? 2 : capacity) {
    states_.push_back(std::move(initial));
  }
  const T& current() const { return states_[index_]; }
  // Records a new state; any redo tail is discarded and the oldest state is dropped at capacity.
  void commit(T next) {
    states_.erase(states_.begin() + static_cast<std::ptrdiff_t>(index_) + 1, states_.end());
    states_.push_back(std::move(next));
    if (states_.size() > capacity_) states_.erase(states_.begin());
    index_ = states_.size() - 1;
  }
  // Replaces everything (e.g. when a new source is loaded) and clears history.
  void reset(T initial) {
    states_.clear();
    states_.push_back(std::move(initial));
    index_ = 0;
  }
  bool canUndo() const { return index_ > 0; }
  bool canRedo() const { return index_ + 1 < states_.size(); }
  bool undo() {
    if (!canUndo()) return false;
    --index_;
    return true;
  }
  bool redo() {
    if (!canRedo()) return false;
    ++index_;
    return true;
  }

 private:
  std::size_t capacity_;
  std::vector<T> states_;
  std::size_t index_ = 0;
};

}  // namespace chopfractal
