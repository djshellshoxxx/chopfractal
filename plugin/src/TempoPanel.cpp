#include "TempoPanel.h"

#include <cmath>

namespace cf = chopfractal;
using namespace perform_ui;
using cf::composition::LoopLengthChoice;
using cf::composition::StretchPreview;

namespace {
juce::String bpmText(double bpm) { return juce::String(bpm, 1); }
}  // namespace

TempoPanel::TempoPanel(EditorModel& m, EditorCommands& c, PerformCommands& perform) : DrawerPanel(m, c, "TempoPanel", 10), perform_(perform) {
  styleLabel(loopLabel_, "Loop length");
  styleLabel(sourceLabel_, "Source BPM");
  styleLabel(targetLabel_, "Target BPM");
  styleLabel(qualityLabel_, "Quality");
  styleLabel(previewText_, "", false);
  styleLabel(adviceText_, "");
  styleLabel(note_, "");
  previewText_.setJustificationType(juce::Justification::topLeft);
  adviceText_.setJustificationType(juce::Justification::topLeft);
  note_.setJustificationType(juce::Justification::topLeft);
  quality_.addItem("Draft", 1);
  quality_.addItem("Standard", 2);
  quality_.addItem("High", 3);
  quality_.setSelectedId(2, juce::dontSendNotification);
  for (auto* e : {&sourceBpm_, &targetBpm_}) {
    e->setInputRestrictions(7, "0123456789.");
    e->setJustification(juce::Justification::centredLeft);
  }
  progressBar_.setPercentageDisplay(false);

  describe(loopLength_, "Loop length", "How long the loop is in bars, which gives its tempo");
  describe(sourceBpm_, "Source BPM", "The tempo the loop was recorded at, editable with Custom BPM");
  describe(targetBpm_, "Target BPM", "The tempo to stretch the loop to");
  describe(quality_, "Quality", "Draft is fast, Standard suits most loops, High is slowest and best for tonal material");
  describe(detect_, "Detect Tempo", "Run Smart Setup to suggest the loop length and tempo");
  describe(useHost_, "Use host", "Set the target to the host tempo");
  describe(match_, "Match to host", "Stretch the loop to the target tempo, keeping every chop on its beat");
  describe(cancel_, "Cancel Stretch", "Stop the stretch and leave the loop as it was");
  describe(undo_, "Undo Stretch", "Go back to the loop as it was before the stretch");
  describe(progressBar_, "Stretch progress", "How much of the stretch is done");
  describe(previewText_, "Tempo match preview", "What the stretch will do to the loop");

  add({&loopLabel_, &loopLength_, &sourceLabel_, &sourceBpm_, &targetLabel_, &targetBpm_, &useHost_, &qualityLabel_, &quality_, &previewText_, &adviceText_, &note_,
       &detect_, &match_, &cancel_, &undo_, &progressBar_});

  loopLength_.onChange = [this] {
    updateSourceField();
    tick();
  };
  quality_.onChange = [this] { tick(); };
  sourceBpm_.onTextChange = [this] {
    if (!updating_) tick();
  };
  targetBpm_.onTextChange = [this] {
    if (!updating_) {
      targetEdited_ = true;
      tick();
    }
  };
  sourceBpm_.onReturnKey = [this] { tick(); };
  targetBpm_.onReturnKey = [this] { tick(); };
  useHost_.onClick = [this] {
    targetEdited_ = false;
    tick();
  };
  detect_.onClick = [this] { cmd_.smartSetup(); };  // Change::Setup rebuilds the choices
  match_.onClick = [this] { matchToHost(); };
  cancel_.onClick = [this] { perform_.cancelStretch(); };
  undo_.onClick = [this] {
    perform_.undoStretch();
    tick();
  };
  tick();
}

void TempoPanel::modelChanged(Change c) {
  if (c == Change::Setup && model_.haveSetup()) {  // Detect Tempo ran: offer its suggestions again, top one selected
    choiceKey_.clear();
    if (isShowing()) tick();
  }
}

double TempoPanel::parseBpm(const juce::TextEditor& e) const { return e.getText().getDoubleValue(); }

void TempoPanel::rebuildChoices() {
  const std::string key = perform_.loopChoiceKey();
  if (key == choiceKey_) return;
  choiceKey_ = key;
  choices_ = perform_.loopChoices();
  loopLength_.clear(juce::dontSendNotification);
  int id = 1;
  for (const auto& c : choices_) loopLength_.addItem(juce::String::fromUTF8(c.text.c_str()), id++);
  // Default: the top suggestion if there is one, else Custom BPM (the last entry).
  int pick = static_cast<int>(choices_.size());
  if (!choices_.empty() && choices_.front().kind == LoopLengthChoice::Kind::Suggestion) pick = 1;
  loopLength_.setSelectedId(choices_.empty() ? 0 : pick, juce::dontSendNotification);
  updateSourceField();
}

void TempoPanel::updateSourceField() {
  const int i = loopLength_.getSelectedId() - 1;
  const bool custom = i >= 0 && i < static_cast<int>(choices_.size()) && choices_[static_cast<std::size_t>(i)].kind == LoopLengthChoice::Kind::Custom;
  sourceBpm_.setReadOnly(!custom);
  sourceBpm_.setEnabled(true);
  if (i >= 0 && i < static_cast<int>(choices_.size()) && !custom) {
    updating_ = true;
    sourceBpm_.setText(bpmText(choices_[static_cast<std::size_t>(i)].bpm), false);
    updating_ = false;
  } else if (custom && sourceBpm_.getText().isEmpty()) {
    updating_ = true;
    sourceBpm_.setText(bpmText(perform_.targetBpmDefault()), false);
    updating_ = false;
  }
}

cf::composition::StretchRequest TempoPanel::request() const {
  cf::composition::StretchRequest r;
  r.sourceBpm = parseBpm(sourceBpm_);
  r.targetBpm = parseBpm(targetBpm_);
  r.quality = static_cast<cf::stretch::Quality>(std::max(0, std::min(2, quality_.getSelectedId() - 1)));
  return r;
}

void TempoPanel::tick() {
  brief_ = perform_.sourceBrief();
  const bool running = perform_.stretch().running();
  const bool usable = brief_.present && !brief_.missing;
  rebuildChoices();
  if (!targetEdited_ && !targetBpm_.hasKeyboardFocus(true)) {
    updating_ = true;
    targetBpm_.setText(bpmText(perform_.targetBpmDefault()), false);
    updating_ = false;
  }
  if (usable) {
    const auto r = request();
    preview_ = cf::composition::previewStretch(r.sourceBpm, r.targetBpm, brief_.frames, brief_.sampleRate, brief_.channels);
  } else {
    preview_ = {};
    preview_.level = StretchPreview::Level::Hidden;
    preview_.canApply = false;
    preview_.text = brief_.missing ? "Locate the source audio first." : "Load a loop to match its tempo.";
  }
  const auto& p = cdl::pal();
  previewText_.setText(juce::String::fromUTF8(preview_.text.c_str()), juce::dontSendNotification);
  previewText_.setColour(juce::Label::textColourId, preview_.level == StretchPreview::Level::Error ? p.clip : (preview_.level == StretchPreview::Level::Hidden ? p.inkMuted : p.ink));
  adviceText_.setText(juce::String::fromUTF8(preview_.advice.c_str()), juce::dontSendNotification);
  adviceText_.setColour(juce::Label::textColourId, p.warn);
  const bool looksStretched = usable && perform_.sourceLooksStretched();
  note_.setText(looksStretched ? "This source was already stretched. Stretching again lowers quality; use Undo Stretch first to start from the original." : "",
                juce::dontSendNotification);
  note_.setColour(juce::Label::textColourId, p.warn);
  for (auto* c : std::initializer_list<juce::Component*>{&loopLength_, &sourceBpm_, &targetBpm_, &quality_, &useHost_, &detect_}) c->setEnabled(usable && !running);
  match_.setEnabled(usable && !running && preview_.canApply);
  cancel_.setVisible(running);
  const bool canUndo = !running && perform_.stretch().undoAvailable();
  undo_.setVisible(canUndo);
  progressBar_.setVisible(running);
  progress_ = static_cast<double>(perform_.stretch().progress());
  if (running) progressBar_.repaint();
  repaint();
}

void TempoPanel::matchToHost() {
  if (!match_.isEnabled() || !preview_.canApply) return;
  const auto r = request();
  auto go = [this, r] { perform_.startStretch(r); };
  if (perform_.sourceLooksStretched()) model_.setStatus("This source was already stretched. Stretching again lowers quality; use Undo Stretch first to start from the original.");
  if (perform_.hasPattern())
    perform_.confirm("Stretch the source?",
                     "Stretching replaces the source audio. Your pattern keeps its rhythm, but the variation tree restarts from the current pattern and A/B snapshots are cleared. You can undo this with Undo Stretch.",
                     "Stretch", go);
  else
    go();
}

int TempoPanel::layoutBody(int width) {
  beginLayout();
  Stack s(width);
  const int lw = Stack::scaled(86);
  labelled(s.row(), loopLabel_, loopLength_, lw);
  labelled(s.row(), sourceLabel_, sourceBpm_, lw);
  {
    auto r = s.row();
    targetLabel_.setBounds(r.removeFromLeft(lw));
    useHost_.setBounds(r.removeFromRight(Stack::scaled(84)));
    r.removeFromRight(Stack::scaled(6));
    targetBpm_.setBounds(r);
  }
  labelled(s.row(), qualityLabel_, quality_, lw);
  previewText_.setBounds(s.row(Stack::scaled(44)));
  adviceText_.setBounds(s.row(Stack::scaled(30)));
  note_.setBounds(s.row(note_.getText().isEmpty() ? Stack::scaled(2) : Stack::scaled(58)));
  progressBar_.setBounds(s.row().reduced(0, Stack::scaled(6)));
  grid(s, {&detect_, &match_, &cancel_, &undo_}, 2);
  body_.repaint();
  return s.height();
}
