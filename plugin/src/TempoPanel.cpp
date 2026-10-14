#include "TempoPanel.h"

#include <cmath>
#include <initializer_list>

#include "PluginProcessor.h"

namespace cf = chopfractal;

TempoPanel::TempoPanel(ChopFractalProcessor& processor) : proc_(processor) {
  setName("TempoPanel");
  setWantsKeyboardFocus(true);
  loop_.setTitle("Loop length");
  quality_.setTitle("Quality");
  quality_.addItemList({"Draft", "Standard", "High"}, 1);
  quality_.setSelectedId(2, juce::dontSendNotification);
  source_.setTitle("Source BPM");
  target_.setTitle("Target BPM");
  for (juce::TextEditor* e : {&source_, &target_}) {
    e->setInputRestrictions(7, "0123456789.");
    e->onTextChange = [this] {
      if (!loading_) recompute();
    };
    e->onReturnKey = [this] { recompute(); };
  }
  useHost_.setTitle("Use host");
  match_.setTitle("Match to host");
  cancel_.setTitle("Cancel");
  undo_.setTitle("Undo Stretch");
  close_.setTitle("Close Tempo");
  for (auto* l : {&srcCaption_, &tgtCaption_, &loopCaption_, &qualityCaption_}) addAndMakeVisible(*l);
  srcCaption_.setText("Source BPM", juce::dontSendNotification);
  tgtCaption_.setText("Target BPM", juce::dontSendNotification);
  loopCaption_.setText("Loop length", juce::dontSendNotification);
  qualityCaption_.setText("Quality", juce::dontSendNotification);
  for (juce::Component* c : std::initializer_list<juce::Component*>{&loop_, &quality_, &source_, &target_, &useHost_, &match_, &cancel_, &undo_, &close_, &info_, &state_}) addAndMakeVisible(*c);
  quality_.setTooltip("Draft is fast, for trying ideas. Standard is good for most loops. High is slowest and best for tonal material and small tempo changes.");
  loop_.onChange = [this] { loopChoiceChanged(); };
  quality_.onChange = [this] { recompute(); };
  useHost_.onClick = [this] {
    const double host = proc_.usedFallbackTempo.load() ? proc_.manualBpm.load() : proc_.activeBpm.load();
    loading_ = true;
    target_.setText(juce::String(host, 1), false);
    loading_ = false;
    recompute();
  };
  match_.onClick = [this] { startMatch(); };
  cancel_.onClick = [this] { proc_.stretchController().cancel(); };
  undo_.onClick = [this] {
    proc_.stretchController().undo();
    refresh(true);
  };
  close_.onClick = [this] { close(); };
  setVisible(false);
}

void TempoPanel::open() {
  refresh(true);
  setVisible(true);
  toFront(true);
  grabKeyboardFocus();
  startTimerHz(5);
}

void TempoPanel::close() {
  setVisible(false);
  stopTimer();
}

bool TempoPanel::keyPressed(const juce::KeyPress& key) {
  if (key == juce::KeyPress::escapeKey) {
    close();
    return true;
  }
  return false;
}

void TempoPanel::refresh(bool reloadSource) {
  bool have = false, missing = false;
  double hostBpm = 120.0;
  std::vector<cf::insight::LoopSuggestion> loops;
  double seconds = 0.0;
  hostBpm = proc_.usedFallbackTempo.load() ? proc_.manualBpm.load() : proc_.activeBpm.load();
  if (reloadSource) {
    proc_.withSession([&](cf::composition::ProjectSession& s) {
      have = s.hasSource();
      missing = s.sourceMissing();
      if (have && s.chopMap()) seconds = static_cast<double>(s.chopMap()->info().frames) / s.chopMap()->info().sampleRate;
      if (have && !missing) {
        auto sug = s.suggestSetup(hostBpm);
        if (sug.ok()) loops = sug.value().loops;
      }
    });
    haveSource_ = have && !missing;
    seconds_ = seconds;
    loading_ = true;
    loop_.clear(juce::dontSendNotification);
    suggestedBpm_.clear();
    int id = 1;
    for (std::size_t i = 0; i < loops.size() && i < 3; ++i) {
      loop_.addItem(loops[i].note, id++);
      suggestedBpm_.push_back(loops[i].bpm);
    }
    for (int bars : {1, 2, 4, 8, 16}) {
      loop_.addItem(juce::String(bars) + (bars == 1 ? " bar" : " bars"), id++);
      suggestedBpm_.push_back(seconds > 0.0 ? bars * 4.0 * 60.0 / seconds : 0.0);
    }
    loop_.addItem("Custom BPM", id);
    suggestedBpm_.push_back(0.0);
    loop_.setSelectedId(loops.empty() ? id : 1, juce::dontSendNotification);
    target_.setText(juce::String(hostBpm, 1), false);
    loading_ = false;
    loopChoiceChanged();
  }
  const auto& sc = proc_.stretchController();
  const bool running = sc.running();
  match_.setEnabled(haveSource_ && !running);
  cancel_.setVisible(running);
  undo_.setVisible(sc.canUndo() && !running);
  for (juce::Component* c : std::initializer_list<juce::Component*>{&loop_, &quality_, &source_, &target_, &useHost_}) c->setEnabled(haveSource_ && !running);
  state_.setText(running ? "Stretching... " + juce::String(juce::roundToInt(sc.progress() * 100.f)) + "%" : (haveSource_ ? "" : "Load a loop first."), juce::dontSendNotification);
  if (running) proc_.setStatusMessage(state_.getText());
  if (!running) recompute();
}

void TempoPanel::loopChoiceChanged() {
  const int idx = loop_.getSelectedItemIndex();
  const bool custom = idx >= 0 && idx == loop_.getNumItems() - 1;
  source_.setReadOnly(!custom);
  if (!custom && idx >= 0 && idx < static_cast<int>(suggestedBpm_.size()) && suggestedBpm_[static_cast<std::size_t>(idx)] > 0.0) {
    loading_ = true;
    source_.setText(juce::String(suggestedBpm_[static_cast<std::size_t>(idx)], 1), false);
    loading_ = false;
  }
  recompute();
}

void TempoPanel::recompute() {
  if (!haveSource_) return;
  const double src = sourceBpm(), tgt = targetBpm();
  const bool running = proc_.stretchController().running();
  juce::String text;
  bool ok = false;
  if (src < 20.0 || src > 999.0 || tgt < 20.0 || tgt > 999.0) {
    text = "Enter tempos between 20 and 999 BPM.";
  } else if (std::fabs(src - tgt) < 0.05) {
    text = "Already at the target tempo.";
  } else {
    const double ratio = cf::stretch::lengthRatio(src, tgt);
    const double outSeconds = seconds_ * ratio;
    const juce::String head = juce::String(src, 1) + " -> " + juce::String(tgt, 1) + " BPM: " + juce::String(juce::roundToInt(100.0 / ratio)) + "% speed, length x" +
                              juce::String(ratio, 3) + " (" + juce::String(seconds_, 1) + " s -> " + juce::String(outSeconds, 1) + " s)";
    if (!cf::stretch::checkRatio(ratio).ok()) {
      text = "Out of range: the tempo change must stay between 0.5x and 2x.";
    } else if (outSeconds > 60.0) {
      text = "The stretched audio would be " + juce::String(outSeconds, 1) + " s; the maximum is 60 s.";
    } else {
      text = head + (ratio < 0.75 || ratio > 1.5 ? "  Large change: High quality is recommended." : "");
      ok = true;
    }
  }
  if (info_.getText() != text) info_.setText(text, juce::dontSendNotification);
  match_.setEnabled(ok && haveSource_ && !running);
}

void TempoPanel::startMatch() {
  cf::composition::StretchRequest r;
  r.sourceBpm = sourceBpm();
  r.targetBpm = targetBpm();
  r.quality = static_cast<cf::stretch::Quality>(juce::jlimit(0, 2, quality_.getSelectedId() - 1));
  bool hasPattern = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) { hasPattern = s.pattern() != nullptr; });
  auto go = [proc = juce::WeakReference<ChopFractalProcessor>(&proc_), r] {
    if (proc == nullptr) return;
    const juce::String err = proc->stretchController().start(r);
    if (err.isNotEmpty()) proc->setStatusMessage(err);
  };
  if (!hasPattern) {
    go();
    return;
  }
  juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                   .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                   .withTitle("Stretch the source?")
                                   .withMessage("Stretching replaces the source audio. Your pattern keeps its rhythm, but the variation tree restarts from the current pattern and A/B snapshots are cleared. You can undo this with Undo Stretch.")
                                   .withButton("Stretch")
                                   .withButton("Cancel"),
                               [go](int result) {
                                 if (result == 1) go();
                               });
}

void TempoPanel::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(0xf015181d));
  g.setColour(juce::Colours::white.withAlpha(0.25f));
  g.drawRect(getLocalBounds());
}

void TempoPanel::resized() {
  auto r = getLocalBounds().reduced(10);
  auto row = [&](juce::Label& cap, juce::Component& c, int width) {
    auto line = r.removeFromTop(30);
    cap.setBounds(line.removeFromLeft(110));
    c.setBounds(line.removeFromLeft(width).reduced(2));
    return line;
  };
  row(loopCaption_, loop_, 260);
  row(srcCaption_, source_, 100);
  auto tline = row(tgtCaption_, target_, 100);
  useHost_.setBounds(tline.removeFromLeft(100).reduced(2));
  row(qualityCaption_, quality_, 160);
  info_.setBounds(r.removeFromTop(30));
  auto buttons = r.removeFromTop(34);
  match_.setBounds(buttons.removeFromLeft(140).reduced(2));
  cancel_.setBounds(buttons.removeFromLeft(90).reduced(2));
  undo_.setBounds(buttons.removeFromLeft(120).reduced(2));
  close_.setBounds(buttons.removeFromRight(120).reduced(2));
  state_.setBounds(r.removeFromTop(26));
}
