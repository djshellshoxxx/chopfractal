#include "CaptureController.h"

#include <chopfractal/composition/project_session.hpp>

#include "PluginProcessor.h"

namespace cf = chopfractal;
namespace cap = chopfractal::capture;

juce::String CaptureController::arm(cap::CaptureMode mode, int bars, double maxSeconds) {
  if (owner_.isNonRealtime()) return "Capture is not available during an offline render.";
  const int inputs = owner_.getTotalNumInputChannels();
  if (inputs < 1) return "Capture needs an audio input. Your host gave this plugin no input channels.";
  if (engine_.status().state == cap::CaptureState::Captured) return "A finished capture is waiting. Keep or discard it first.";
  cap::ArmSettings s;
  s.mode = mode;
  s.channels = juce::jmin(2, inputs);
  s.sampleRate = owner_.currentSampleRate();
  s.bars = bars;
  s.barQuarters = 4.0 * owner_.hostBeatsPerBar.load() / juce::jmax(1, owner_.hostBeatUnit.load());
  s.bpmHint = owner_.activeBpm.load();
  s.maxSeconds = maxSeconds;
  const cf::Status st = engine_.arm(s);
  if (!st.ok()) return st.error().message;
  clearUndo();
  return {};
}

juce::String CaptureController::start() {
  const cf::Status st = engine_.startNow();
  return st.ok() ? juce::String() : juce::String(st.error().message);
}

juce::String CaptureController::endMessage() const {
  const cap::CaptureStatus st = engine_.status();
  switch (st.ended) {
    case cap::EndReason::TransportStopped:
      return "Capture stopped: the host transport stopped or jumped. Kept " + juce::String(st.framesCaptured / juce::jmax(1.0, owner_.currentSampleRate()), 1) + " s.";
    case cap::EndReason::Overflow: return "Capture ended early: the tempo changed during capture.";
    case cap::EndReason::Reconfigured: return "Capture cancelled: the audio settings changed.";
    default: return {};
  }
}

void CaptureController::clearUndo() {
  canUndo_ = false;
  preKeepState_.clear();
  preKeepSource_.reset();
  hadSource_ = false;
}

void CaptureController::keepAsync(bool chopAfter) {
  if (engine_.status().state != cap::CaptureState::Captured) {
    owner_.setStatusMessage("There is no finished capture to keep.");
    return;
  }
  if (keeping_.exchange(true)) return;
  // Snapshot the project first so the Keep can be undone (skipped when it is too large to embed).
  clearUndo();
  owner_.withSession([this](cf::composition::ProjectSession& s) {
    hadSource_ = s.hasSource();
    if (!hadSource_) return;
    auto saved = s.saveState(true);
    if (saved.ok()) {
      preKeepState_ = std::move(saved.value());
      preKeepSource_ = s.sourceData();
    }
  });
  owner_.setStatusMessage("Keeping capture...");
  juce::WeakReference<ChopFractalProcessor> self(&owner_);
  owner_.workerPool().addJob([self, chopAfter] {
    if (self == nullptr) return;
    CaptureController& c = self->captureController();
    auto taken = c.engine_.take();
    std::shared_ptr<cf::render::SourceData> data;
    if (taken.ok()) {
      data = std::make_shared<cf::render::SourceData>();
      data->channels = taken.value().channels;
      data->sampleRate = taken.value().sampleRate;
      data->frames = taken.value().frames;
      data->samples = std::move(taken.value().samples);
    }
    const juce::String error = taken.ok() ? juce::String() : juce::String(taken.error().message);
    juce::MessageManager::callAsync([self, data, error, chopAfter] {
      if (self == nullptr) return;
      CaptureController& cc = self->captureController();
      if (!data) {
        cc.keeping_ = false;
        self->setStatusMessage(error);
        return;
      }
      cc.finishKeep(data, chopAfter);
    });
  });
}

void CaptureController::finishKeep(std::shared_ptr<const cf::render::SourceData> data, bool chopAfter) {
  const juce::String name = "Capture " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M");
  cf::Status status;
  const double bpm = owner_.usedFallbackTempo.load() ? 0.0 : owner_.activeBpm.load();
  owner_.withSession([&](cf::composition::ProjectSession& s) {
    status = s.loadCapturedSource(data, name.toStdString());
    if (status.ok() && chopAfter) s.smartSetup(bpm);
  });
  keeping_ = false;
  if (!status.ok()) {
    clearUndo();
    owner_.setStatusMessage(status.error().message);
    return;
  }
  owner_.embedSource = true;
  canUndo_ = hadSource_ ? !preKeepState_.empty() : true;
  owner_.setStatusMessage("Captured audio is saved inside the project.");
}

bool CaptureController::undoKeep() {
  if (!canUndo_.load()) return false;
  cf::Status status;
  const bool restore = hadSource_;
  owner_.withSession([&](cf::composition::ProjectSession& s) {
    if (!restore) {
      s.clearSource();
      return;
    }
    const auto source = preKeepSource_;
    status = s.loadState(preKeepState_.data(), preKeepState_.size(), [source](const cf::source::SourceInfo&) { return source; });
  });
  if (!status.ok()) {
    owner_.setStatusMessage(status.error().message);
    return false;
  }
  clearUndo();
  owner_.setStatusMessage("Restored the project from before the capture.");
  return true;
}
