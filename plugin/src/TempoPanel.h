#pragma once
// Tempo drawer (F14 section 3): match a loop recorded at one tempo to the project tempo by stretching it offline. Loop length
// choices come from Smart Setup's analysis (Detect Tempo runs it), the live line comes from composition::previewStretch, the
// job runs on a worker (PerformCommands / StretchRunner) and Undo Stretch goes back one step.
#include "DrawerPanel.h"
#include "PerformCommands.h"

class TempoPanel : public DrawerPanel {
 public:
  TempoPanel(EditorModel&, EditorCommands&, PerformCommands&);
  // The request the controls describe right now.
  chopfractal::composition::StretchRequest request() const;
  const chopfractal::composition::StretchPreview& preview() const { return preview_; }
  // Match to host: asks first when a pattern exists (the confirm hook of PerformCommands), then starts the job.
  void matchToHost();

 private:
  int layoutBody(int width) override;
  void tick() override;
  void modelChanged(Change) override;
  void rebuildChoices();
  void updateSourceField();
  double parseBpm(const juce::TextEditor&) const;

  PerformCommands& perform_;
  std::vector<chopfractal::composition::LoopLengthChoice> choices_;
  std::string choiceKey_;
  chopfractal::composition::StretchPreview preview_;
  PerformCommands::SourceBrief brief_;
  bool targetEdited_ = false, updating_ = false;
  juce::Label loopLabel_, sourceLabel_, targetLabel_, qualityLabel_, previewText_, adviceText_, note_;
  juce::ComboBox loopLength_, quality_;
  juce::TextEditor sourceBpm_, targetBpm_;
  juce::TextButton detect_{"Detect Tempo"}, useHost_{"Use host"}, match_{"Match to host"}, cancel_{"Cancel Stretch"}, undo_{"Undo Stretch"};
  double progress_ = 0.0;
  juce::ProgressBar progressBar_{progress_};
};
