#pragma once
// JUCE-side glue for tempo matching: plans on the message thread, runs the heavy WSOLA stretch on the worker pool with
// progress and cancel, applies the result atomically, and keeps one level of Undo Stretch.
#include <chopfractal/composition/project_session.hpp>
#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class ChopFractalProcessor;

class StretchController {
 public:
  explicit StretchController(ChopFractalProcessor& owner) : owner_(owner) {}

  // Message thread. Returns an empty string when the job started, else the message to show.
  juce::String start(const chopfractal::composition::StretchRequest& request);
  void cancel() { cancel_ = true; }
  bool running() const { return running_.load(); }
  float progress() const { return progress_.load(); }
  bool canUndo() const { return canUndo_.load(); }
  bool undo();
  void clearUndo();

 private:
  ChopFractalProcessor& owner_;
  std::atomic<bool> running_{false}, cancel_{false}, canUndo_{false};
  std::atomic<float> progress_{0.f};
  std::vector<std::uint8_t> preState_;
  std::shared_ptr<const chopfractal::render::SourceData> preSource_;  // used when the old source was too large to embed
};
