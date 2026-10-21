#include "PluginEditor.h"
#include "BuildInfo.h"

#include <algorithm>
#include <initializer_list>

#include "ControlHelp.h"
#include "EditorTheme.h"

namespace cf = chopfractal;

ChopFractalEditor::ChopFractalEditor(ChopFractalProcessor& processor)
    : AudioProcessorEditor(&processor),
      model_(processor),
      commands_(processor, model_),
      controls_(model_, commands_, processor.apvts),
      features_(model_, commands_),
      wave_(model_, commands_),
      pattern_(model_, commands_),
      history_(model_, commands_),
      orbit_(model_, commands_),
      status_(model_, commands_) {
  for (juce::Component* panel : std::initializer_list<juce::Component*>{&controls_, &features_, &wave_, &pattern_, &history_, &orbit_, &status_}) addAndMakeVisible(*panel);
  setWantsKeyboardFocus(true);
  setSize(940, 912);
  setResizable(false, false);
  syncHelpTree();
  model_.refresh(true, wave_.getWidth());
  startTimerHz(10);
}

ChopFractalEditor::~ChopFractalEditor() { stopTimer(); }

void ChopFractalEditor::syncHelpTree() {
  if (auto* root = getTopLevelComponent(); root != tooltipRoot_) {
    tooltipRoot_ = root;
    // Include the standalone wrapper's Options and feedback-notice Settings controls.
    control_help::applyToTree(*root);
  }
}

void ChopFractalEditor::timerCallback() {
  syncHelpTree();
  const bool rebuilt = model_.refresh(false, wave_.getWidth());
  const bool playing = model_.playheadQuarters() >= 0.0;
  if (!rebuilt && (playing || lastPlaying_)) orbit_.repaint();  // only the Orbit View shows the playhead while the transport runs
  lastPlaying_ = playing;
}

juce::String ChopFractalEditor::getTooltip() {
  const auto point = getMouseXYRelative();
  if (wave_.getBounds().contains(point))
    return "Source waveform: click a chop to select and audition it; drag across sections to select several. Shift-click adds a marker; Alt-click removes the nearest marker. Drop a sample anywhere to load it.";
  if (pattern_.getBounds().contains(point))
    return "Pattern grid: each lane is a chop. Click a hit or drag a rectangle to select sections. Zoom In subdivides selected hits. M toggles selected hits; Delete removes them. Enable Move / resize hits to edit by dragging.";
  if (history_.getBounds().contains(point))
    return "Variation family tree: click a node to recall it, at the next loop boundary during playback. Right-click for favorite, rename, compare and delete actions. Each Generate, Mutate, Zoom and completed edit adds a branch.";
  if (orbit_.getBounds().contains(point))
    return "Orbit view: concentric rings show chop lanes and their rhythmic positions. The moving line is the playhead. Click a hit to select it in the pattern.";
  return "ChopFractal: load or drop a loop, Detect Chops, Generate and Play. Hover controls for instructions.";
}

bool ChopFractalEditor::isInterestedInFileDrag(const juce::StringArray& files) {
  for (const auto& path : files)
    if (EditorCommands::isAudioFile(juce::File(path))) return true;
  return false;
}

void ChopFractalEditor::filesDropped(const juce::StringArray& files, int, int) { commands_.handleDroppedFiles(files); }

void ChopFractalEditor::paint(juce::Graphics& g) {
  g.fillAll(editor_theme::background());
  g.setColour(juce::Colours::white.withAlpha(0.5f));
  g.setFont(10.0f);
  g.drawText(juce::String("ChopFractal ") + cfbuild::kVersionFull, getLocalBounds().removeFromTop(11).withTrimmedRight(10), juce::Justification::centredRight);
}

void ChopFractalEditor::resized() {
  auto area = getLocalBounds().reduced(10);
  controls_.setBounds(area.removeFromTop(232));  // transport 36, buttons 30, gap 6, seed row 26, gap 6, parameter block 128
  area.removeFromTop(6);
  features_.setBounds(area.removeFromTop(104));  // four rows of 26
  area.removeFromTop(6);
  wave_.setBounds(area.removeFromTop(120));
  area.removeFromTop(6);
  pattern_.setBounds(area.removeFromTop(230));
  area.removeFromTop(6);
  status_.setBounds(area.removeFromBottom(74).reduced(0, 2));
  auto bottom = area.removeFromTop(area.getHeight() - 2);
  orbit_.setBounds(bottom.removeFromRight(std::min(bottom.getHeight(), bottom.getWidth() / 2)));
  history_.setBounds(bottom.withTrimmedRight(6));
}

// Keyboard equivalents of the drag gestures work on the selected hit.
bool ChopFractalEditor::keyPressed(const juce::KeyPress& k) {
  if (!model_.hasSelection()) return false;
  const auto mods = k.getModifiers();
  if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown()) return false;  // leave the host's own shortcuts alone
  if (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey) {
    commands_.nudgeSelected(k.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1, mods.isShiftDown());
    return true;
  }
  if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) {
    commands_.deleteSelected();
    return true;
  }
  if (k.getTextCharacter() == 'm' || k.getTextCharacter() == 'M') {
    commands_.toggleMuteSelected();
    return true;
  }
  if (k.getTextCharacter() == 'd' || k.getTextCharacter() == 'D') {
    commands_.duplicateSelected();
    return true;
  }
  return false;
}
