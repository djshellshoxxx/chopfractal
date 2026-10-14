#pragma once
// Tempo match (docs/specs/roadmap/F14-tempo-match-stretch.md): choose the loop's length or source BPM and the target BPM,
// then stretch the source offline with the chop boundaries kept on their scaled positions.
#include <chopfractal/composition/project_session.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

class ChopFractalProcessor;

class TempoPanel : public juce::Component, private juce::Timer {
 public:
  explicit TempoPanel(ChopFractalProcessor& processor);
  ~TempoPanel() override { stopTimer(); }
  void open();
  void close();
  void paint(juce::Graphics& g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;
  bool matchEnabled() const { return match_.isEnabled(); }
  juce::String infoText() const { return info_.getText(); }

 private:
  void timerCallback() override { refresh(false); }
  void refresh(bool reloadSource);
  void loopChoiceChanged();
  void recompute();
  void startMatch();
  double sourceBpm() const { return source_.getText().getDoubleValue(); }
  double targetBpm() const { return target_.getText().getDoubleValue(); }

  ChopFractalProcessor& proc_;
  juce::ComboBox loop_, quality_;
  juce::TextEditor source_, target_;
  juce::TextButton useHost_{"Use host"}, match_{"Match to host"}, cancel_{"Cancel"}, undo_{"Undo Stretch"}, close_{"Close Tempo"};
  juce::Label info_, srcCaption_, tgtCaption_, loopCaption_, qualityCaption_, state_;
  std::vector<double> suggestedBpm_;
  double seconds_ = 0.0;
  bool haveSource_ = false;
  bool loading_ = false;
};
