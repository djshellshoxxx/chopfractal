#pragma once
// Tempo-match stretch job (F14): plans under the session lock, runs the stretch on a worker over the immutable source
// snapshot, and applies the result on the message thread (poll). One level of undo through SourceReplaceUndo. The runner
// belongs to the editor side (PerformCommands); closing the editor cancels and joins a running job.
#include <juce_core/juce_core.h>

#include <atomic>
#include <chopfractal/chop_contracts/result.hpp>
#include <chopfractal/composition/source_replace.hpp>
#include <chopfractal/composition/stretch.hpp>
#include <memory>
#include <optional>

#include "PluginProcessor.h"

class StretchRunner {
 public:
  explicit StretchRunner(ChopFractalProcessor&);
  ~StretchRunner();

  // Plans (validation errors are returned and nothing starts). Conflict "A stretch is already running." while one runs.
  chopfractal::Status start(const chopfractal::composition::StretchRequest&);
  void cancel();                          // asks the worker to stop; poll() reports it
  bool running() const { return worker_ != nullptr; }
  float progress() const { return progress_.load(); }

  enum class Outcome { None, Applied, Cancelled, Failed };
  // Message thread, called from the editor's background tick: applies a finished job. `message` is the status text.
  Outcome poll(juce::String& message);

  // Undo Stretch is offered until the project moves on (a new pattern, another source, a load).
  bool undoAvailable();
  chopfractal::Status undo();

 private:
  class Worker;
  ChopFractalProcessor& proc_;
  std::unique_ptr<Worker> worker_;
  std::atomic<float> progress_{0.f};
  chopfractal::composition::SourceReplaceUndo undo_;
  // The state right after the apply; any difference ends the undo window.
  std::shared_ptr<const chopfractal::render::SourceData> appliedData_;
  std::size_t appliedHistorySize_ = 0;
  std::uint64_t appliedActive_ = 0;
};
