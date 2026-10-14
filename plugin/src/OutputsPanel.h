#pragma once
// Output routing (docs/specs/roadmap/F11-multi-output-stems.md): which chop or role plays on which of the eight
// optional aux outputs. A view over ProjectSession::outputRouting; the audio side only sees the published playback.
#include <chopfractal/composition/project_session.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include "RulesPanel.h"  // StringListModel

class ChopFractalProcessor;

class OutputsPanel : public juce::Component, private juce::Timer {
 public:
  explicit OutputsPanel(ChopFractalProcessor& processor);
  ~OutputsPanel() override { stopTimer(); }
  void open();
  void close();
  void paint(juce::Graphics& g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;
  juce::String statusText() const { return status_.getText(); }

 private:
  void timerCallback() override { refresh(); }
  void refresh();
  void fillBusItems(juce::ComboBox& box, bool withRoleDefault);
  void apply(const std::function<void(chopfractal::composition::OutputRouting&, const chopfractal::composition::ProjectSession&)>& edit);
  void report(const juce::String& fallback = {});

  ChopFractalProcessor& proc_;
  StringListModel chopModel_, roleModel_;
  juce::ListBox chopList_, roleList_;
  juce::ComboBox mode_, chopBus_, roleBus_;
  juce::TextButton autoRole_{"Auto: by role"}, autoChop_{"Auto: by chop"}, reset_{"Reset to Main"}, close_{"Close Outputs"};
  juce::Label status_, chopCaption_, roleCaption_;
  std::vector<juce::String> chopRows_, roleRows_;
  std::vector<std::uint64_t> chopIds_;
  std::vector<std::string> roleNames_;
  int aux_ = 0;
  bool loading_ = false;
};
