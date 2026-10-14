#pragma once
// JUCE-side glue for live input capture: owns the portable CaptureEngine, turns host state into arm settings,
// and moves a finished capture into the session (Keep) with a one-step Undo Keep.
#include <chopfractal/input_capture/capture.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

namespace chopfractal::render {
struct SourceData;
}

class ChopFractalProcessor;

class CaptureController {
 public:
  explicit CaptureController(ChopFractalProcessor& owner) : owner_(owner) {}

  chopfractal::capture::CaptureEngine& engine() { return engine_; }

  // Message thread. Each returns an empty string on success, else the message to show the user.
  juce::String arm(chopfractal::capture::CaptureMode mode, int bars, double maxSeconds);
  juce::String start();

  void keepAsync(bool chopAfter);  // take() on a worker, then load on the message thread
  bool canUndoKeep() const { return canUndo_.load(); }
  bool undoKeep();
  void clearUndo();

  juce::String endMessage() const;  // explains why the last capture ended

 private:
  void finishKeep(std::shared_ptr<const chopfractal::render::SourceData> data, bool chopAfter);

  ChopFractalProcessor& owner_;
  chopfractal::capture::CaptureEngine engine_;
  std::vector<std::uint8_t> preKeepState_;
  std::shared_ptr<const chopfractal::render::SourceData> preKeepSource_;
  bool hadSource_ = false;
  std::atomic<bool> canUndo_{false};
  std::atomic<bool> keeping_{false};
};
