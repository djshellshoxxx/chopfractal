#include "CapturePanel.h"

#include <initializer_list>

#include "PluginProcessor.h"

namespace cap = chopfractal::capture;

void InputMeter::paint(juce::Graphics& g) {
  const auto b = getLocalBounds().toFloat();
  g.setColour(juce::Colours::black.withAlpha(0.6f));
  g.fillRoundedRectangle(b, 2.f);
  const float db = level_ > 1e-5f ? juce::Decibels::gainToDecibels(level_, -60.f) : -60.f;
  const float frac = juce::jlimit(0.f, 1.f, (db + 60.f) / 60.f);
  g.setColour(clipped_ ? juce::Colours::red : (db > -6.f ? juce::Colours::orange : juce::Colours::limegreen));
  g.fillRoundedRectangle(b.withWidth(b.getWidth() * frac), 2.f);
}

namespace {
constexpr int kBarChoices[] = {1, 2, 4, 8};
}

CapturePanel::CapturePanel(ChopFractalProcessor& processor) : proc_(processor) {
  mode_.addItemList({"Synced to host (bars)", "Free (until End Capture)"}, 1);
  mode_.setSelectedId(1, juce::dontSendNotification);
  mode_.setTitle("Capture mode");
  bars_.addItemList({"1 bar", "2 bars", "4 bars", "8 bars"}, 1);
  bars_.setSelectedId(3, juce::dontSendNotification);
  bars_.setTitle("Capture bars");
  for (juce::Button* b : std::initializer_list<juce::Button*>{&arm_, &start_, &stop_, &keep_, &discard_, &undo_, &chop_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  addAndMakeVisible(mode_);
  addAndMakeVisible(bars_);
  addAndMakeVisible(meter_);
  addAndMakeVisible(state_);
  meter_.setTitle("Input level");
  chop_.setToggleState(true, juce::dontSendNotification);
  state_.setMinimumHorizontalScale(0.7f);
  state_.setText("Idle", juce::dontSendNotification);

  arm_.onClick = [this] { arm(); };
  start_.onClick = [this] {
    const auto err = proc_.captureController().start();
    if (err.isNotEmpty()) proc_.setStatusMessage(err);
  };
  stop_.onClick = [this] { proc_.captureController().engine().requestStop(); };
  keep_.onClick = [this] { keep(); };
  discard_.onClick = [this] {
    proc_.captureController().engine().discard();
    update();
  };
  undo_.onClick = [this] {
    proc_.captureController().undoKeep();
    update();
  };
  update();
  startTimerHz(10);
}

void CapturePanel::resized() {
  auto all = getLocalBounds();
  auto row2 = all.removeFromBottom(18);
  auto r = all;
  for (juce::Button* b : std::initializer_list<juce::Button*>{&arm_, &start_, &stop_, &keep_, &discard_, &undo_}) b->setBounds(r.removeFromLeft(b == &keep_ || b == &stop_ ? 96 : 72).reduced(2));
  chop_.setBounds(r.removeFromLeft(130));
  mode_.setBounds(r.removeFromLeft(170).reduced(2));
  bars_.setBounds(r.removeFromLeft(80).reduced(2));
  meter_.setBounds(row2.removeFromLeft(120).withSizeKeepingCentre(120, 8));
  row2.removeFromLeft(8);
  state_.setBounds(row2);
}

void CapturePanel::arm() {
  const bool synced = mode_.getSelectedId() == 1;
  const int idx = juce::jlimit(0, 3, bars_.getSelectedId() - 1);
  const auto err = proc_.captureController().arm(synced ? cap::CaptureMode::SyncedBars : cap::CaptureMode::Free, kBarChoices[idx], 30.0);
  if (err.isNotEmpty()) proc_.setStatusMessage(err);
  update();
}

void CapturePanel::keep() {
  const bool chop = chop_.getToggleState();
  bool replacing = false;
  proc_.withSession([&](chopfractal::composition::ProjectSession& s) { replacing = s.hasSource(); });
  if (!replacing) {
    proc_.captureController().keepAsync(chop);
    return;
  }
  juce::WeakReference<ChopFractalProcessor> proc(&proc_);
  juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                   .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                   .withTitle("Replace the current source?")
                                   .withMessage("Keeping this capture replaces the source. Your pattern and variations will be cleared. You can undo this with Undo Keep.")
                                   .withButton("Keep")
                                   .withButton("Cancel"),
                               [proc, chop](int result) {
                                 if (proc != nullptr && result == 1) proc->captureController().keepAsync(chop);
                               });
}

void CapturePanel::update() {
  auto& cc = proc_.captureController();
  const cap::CaptureStatus st = cc.engine().status();
  const bool idle = st.state == cap::CaptureState::Idle;
  const bool armed = st.state == cap::CaptureState::Armed;
  const bool capturing = st.state == cap::CaptureState::Capturing;
  const bool captured = st.state == cap::CaptureState::Captured;
  arm_.setEnabled(idle);
  start_.setEnabled(armed);
  stop_.setEnabled(armed || capturing);
  keep_.setEnabled(captured);
  discard_.setEnabled(captured);
  undo_.setEnabled(cc.canUndoKeep());
  mode_.setEnabled(idle);
  bars_.setEnabled(idle && mode_.getSelectedId() == 1);

  // Decaying level (instant attack, 0.8x per 100 ms) with a clip flag that latches until the next Arm.
  const float recent = cc.engine().takeRecentPeak();
  shown_ = juce::jmax(recent, shown_ * 0.8f);
  if (idle && st.framesCaptured == 0 && st.clippedSamples == 0) shown_ = 0.f;
  const bool clipped = st.clippedSamples > 0 && !idle;
  meter_.set(shown_, clipped);
  meter_.setTooltip(clipped ? juce::String(static_cast<int>(st.clippedSamples)) + " samples clipped" : juce::String("Input level while armed or capturing"));

  const double sr = juce::jmax(1.0, proc_.currentSampleRate());
  const double secs = static_cast<double>(st.framesCaptured) / sr;
  juce::String text = "Idle";
  if (armed) text = mode_.getSelectedId() == 1 ? (proc_.hostPlaying.load() ? "Armed: starts at the next bar" : "Armed: waiting for the host to play") : "Armed: press Capture to start";
  else if (capturing) text = "Capturing " + juce::String(secs, 1) + " s";
  else if (captured) text = "Captured " + juce::String(secs, 1) + " s. Keep to chop it, or Discard.";
  if (armed) lastEnd_.clear();
  if (captured || idle) {
    const auto msg = cc.endMessage();
    if (msg.isNotEmpty() && msg != lastEnd_) {
      lastEnd_ = msg;
      proc_.setStatusMessage(msg);
    }
  }
  if (clipped) text = "Clipped  " + text;
  if (state_.getText() != text) state_.setText(text, juce::dontSendNotification);
}
