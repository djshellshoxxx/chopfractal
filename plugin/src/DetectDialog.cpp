#include "DetectDialog.h"

#include "CdlLookAndFeel.h"

DetectDialog::DetectDialog(EditorModel& model, EditorCommands& commands) : model_(model), cmd_(commands) {
  setComponentID("DetectDialog");
  model_.addListener(this);
  for (juce::ToggleButton* b : {&replace_, &merge_}) {
    b->setRadioGroupId(4711);
    b->setClickingTogglesState(true);
    b->setTitle(b->getButtonText());
    addAndMakeVisible(*b);
  }
  replace_.setTooltip("Remove my markers and use the detected chops");
  merge_.setTooltip("Keep the markers I placed and add the detected chops between them");
  const bool merge = model_.detectPreview().options.merge == chopfractal::source::MergeMode::Merge;
  merge_.setToggleState(merge, juce::dontSendNotification);
  replace_.setToggleState(!merge, juce::dontSendNotification);
  replace_.onClick = [this] { cmd_.setDetectMerge(false); };
  merge_.onClick = [this] { cmd_.setDetectMerge(true); };
  for (juce::TextButton* b : {&apply_, &cancel_}) {
    b->setTitle(b->getButtonText());
    addAndMakeVisible(*b);
  }
  apply_.setTooltip("Apply the proposed chops as one undo step");
  cancel_.setTooltip("Leave my markers as they are");
  apply_.onClick = [this] {
    cmd_.applyDetect();
    close();
  };
  cancel_.onClick = [this] {
    cmd_.cancelDetect();
    close();
  };
  info_.setFont(cdl::font(cdl::FontRole::Body));
  info_.setColour(juce::Label::textColourId, cdl::pal().ink);
  info_.setJustificationType(juce::Justification::topLeft);
  addAndMakeVisible(info_);
  setSize(kWidth, kHeight);
  refreshText();
}

DetectDialog::~DetectDialog() {
  model_.removeListener(this);
  cmd_.cancelDetect();  // no-op after Apply
}

void DetectDialog::refreshText() {
  const auto& p = model_.detectPreview();
  info_.setText(p.active ? juce::String(static_cast<int>(p.frames.size())) + " chops proposed, " + juce::String(p.existing) + " markers now.\nThe proposals are dashed on the waveform."
                         : juce::String("No detection pending."),
                juce::dontSendNotification);
  apply_.setEnabled(p.active);
}

void DetectDialog::modelChanged(EditorModel::Change c) {
  if (c == EditorModel::Change::Detect) refreshText();
}

void DetectDialog::close() {
  if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
}

void DetectDialog::paint(juce::Graphics& g) { g.fillAll(cdl::pal().panel); }

void DetectDialog::resized() {
  auto r = getLocalBounds().reduced(10);
  info_.setBounds(r.removeFromTop(38));
  replace_.setBounds(r.removeFromTop(24));
  merge_.setBounds(r.removeFromTop(24));
  r.removeFromTop(6);
  auto row = r.removeFromTop(28);
  apply_.setBounds(row.removeFromLeft(row.getWidth() / 2 - 3));
  row.removeFromLeft(6);
  cancel_.setBounds(row);
}

void DetectDialog::launch(juce::Component& parent, juce::Rectangle<int> target, EditorModel& model, EditorCommands& commands) {
  auto& box = juce::CallOutBox::launchAsynchronously(std::make_unique<DetectDialog>(model, commands), target, &parent);
  box.setDismissalMouseClicksAreAlwaysConsumed(true);
}
