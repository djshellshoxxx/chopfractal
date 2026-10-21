#pragma once
#include "ParamTip.h"
// The static furniture of the main view, painted behind the waveform and the pattern grid: the Source and Pattern
// panels with their toolbars, the source ruler, the chop strip (one cell per chop with number and role), the pattern bar
// ruler and the per-role lane headers. It also owns the toolbar widgets of those two panels (Load Loop, Detect Chops,
// pattern Length and, in Advanced mode, Zoom In, Collapse, Lock Bar and Move / resize hits).
#include "EditorPanels.h"
#include "ShellLayout.h"

class ShellChrome : public EditorPanelBase {
 public:
  ShellChrome(EditorModel&, EditorCommands&, juce::AudioProcessorValueTreeState&);
  void setLayout(const shell::ShellRects&, bool advanced);
  void paint(juce::Graphics&) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

  static juce::String roleName(chopfractal::DrumRole, int chopIndex);
  static const char* roleLetter(chopfractal::DrumRole);

 private:
  void modelChanged(Change) override;
  void layoutWidgets();
  void syncWaveButtons();

  juce::AudioProcessorValueTreeState& apvts_;
  ParamTip tips_;
  shell::ShellRects rects_;
  bool advanced_ = false;
  juce::TextButton load_{"Load Loop"}, detect_{"Detect Chops"}, zoom_{"Zoom In"}, collapse_{"Collapse"}, lock_{"Lock Bar"};
  juce::TextButton fit_{"Fit"}, undoMarker_{"Undo Marker"}, redoMarker_{"Redo Marker"};
  juce::ComboBox snap_;
  juce::ToggleButton editMode_{"Move / resize hits"};  // retained for saved projects; hits are always editable now, so it is hidden
  juce::ToggleButton gridSnap_{"Snap"};
  juce::ComboBox snapDivision_;
  juce::ComboBox bars_;
  juce::Label barsLabel_{{}, "Length"};
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> barsAttachment_;
};
