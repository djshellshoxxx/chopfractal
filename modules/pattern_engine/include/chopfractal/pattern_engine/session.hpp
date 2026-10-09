#pragma once
// Undo/redo and A/B snapshot slots over immutable Pattern values. Generation and mutation are single
// undo operations; each manual edit is its own. Undo restores the exact pre-operation pattern, seed
// included.
#include <chopfractal/chop_contracts/undo.hpp>
#include <chopfractal/pattern_engine/pattern.hpp>

#include <array>
#include <memory>

namespace chopfractal::pattern {

class PatternSession {
 public:
  PatternSession() : history_(nullptr) {}

  bool hasPattern() const { return history_.current() != nullptr; }
  // Valid only when hasPattern().
  const Pattern& current() const { return *history_.current(); }
  std::shared_ptr<const Pattern> currentShared() const { return history_.current(); }

  // Commit the outcome of any pure edit function. On error nothing changes and the error is returned.
  Status commit(Result<Pattern> next);
  // Replace the current pattern (e.g. when restoring a history node or loading state) as one undo step.
  void adopt(Pattern p);
  void reset();  // new source: drops pattern and undo history
  void restore(Pattern p);  // project load: the pattern becomes the only history entry; A/B slots are cleared

  bool canUndo() const { return history_.canUndo(); }
  bool canRedo() const { return history_.canRedo(); }
  bool undo() { return history_.undo(); }
  bool redo() { return history_.redo(); }

  // A/B comparison: slots hold complete patterns (events, locks, and seed).
  Status storeSnapshot(std::size_t slot);
  Status recallSnapshot(std::size_t slot);  // undoable
  bool hasSnapshot(std::size_t slot) const { return slot < 2 && slots_[slot] != nullptr; }

 private:
  SnapshotStack<std::shared_ptr<const Pattern>> history_;
  std::array<std::shared_ptr<const Pattern>, 2> slots_;
};

}  // namespace chopfractal::pattern
