#include "CapturePanel.h"

#include <cmath>

namespace cf = chopfractal;
using namespace perform_ui;

void CaptureMeter::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  auto r = getLocalBounds().toFloat();
  auto bar = r.withTrimmedRight(text_.isEmpty() ? 0.f : r.getWidth() * 0.28f).reduced(0.f, r.getHeight() * 0.25f);
  g.setColour(p.bg);
  g.fillRect(bar);
  const float lv = juce::jlimit(0.f, 1.f, level_);
  g.setColour(clipped_ ? p.clip : (lv > 0.85f ? p.warn : p.ok));
  g.fillRect(bar.withWidth(bar.getWidth() * lv));
  if (peak_ > 0.f) {
    const float x = bar.getX() + bar.getWidth() * juce::jlimit(0.f, 1.f, peak_);
    g.setColour(p.ink);
    g.fillRect(juce::Rectangle<float>(x - 1.f, bar.getY() - 2.f, 2.f, bar.getHeight() + 4.f));
  }
  g.setColour(p.edgeHi);
  g.drawRect(bar, 1.f);
  if (text_.isNotEmpty()) {
    g.setColour(p.clip);
    g.setFont(cdl::font(cdl::FontRole::Label, cdl::uiScale()));
    g.drawText(text_, r.withLeft(bar.getRight() + 6.f).toNearestInt(), juce::Justification::centredLeft, false);
  }
}

CapturePanel::CapturePanel(EditorModel& m, EditorCommands& c, PerformCommands& perform) : DrawerPanel(m, c, "CapturePanel", 10), perform_(perform) {
  styleLabel(state_, "Idle", false);
  state_.setJustificationType(juce::Justification::topLeft);
  styleLabel(length_, "");
  styleLabel(modeLabel_, "Mode");
  styleLabel(barsLabel_, "Bars");
  styleLabel(countdown_, "");
  countdown_.setJustificationType(juce::Justification::centredRight);
  for (const char* item : cf::ui::kCaptureModeItems) mode_.addItem(item, mode_.getNumItems() + 1);
  mode_.setSelectedId(1, juce::dontSendNotification);
  for (int b : cf::ui::kCaptureBarsChoices) bars_.addItem(juce::String(b) + (b == 1 ? " bar" : " bars"), bars_.getNumItems() + 1);
  bars_.setSelectedId(3, juce::dontSendNotification);
  for (int s : cf::ui::kCaptureMaxSecondsChoices) maxLength_.addItem(juce::String(s) + " s", maxLength_.getNumItems() + 1);
  maxLength_.setSelectedId(2, juce::dontSendNotification);
  chopAfter_.setToggleState(true, juce::dontSendNotification);

  describe(mode_, "Capture mode", "Record a set number of bars from the host grid, or keep the last seconds until Stop");
  describe(bars_, "Capture bars", "How many bars to record, starting at the next bar line");
  describe(maxLength_, "Max length", "How many seconds the free recording keeps");
  describe(arm_, "Arm", "Wait for the next bar line, then record the track's input");
  describe(now_, "Capture Now", "Start recording the track's input right away");
  describe(stop_, "Stop Capture", "End the recording and keep what was captured so far");
  describe(keep_, "Keep Capture", "Use the captured audio as the project's loop");
  describe(discard_, "Discard Capture", "Throw the captured audio away");
  describe(undoKeep_, "Undo Keep", "Go back to the loop that was in the project before Keep");
  describe(chopAfter_, "Chop after Keep", "Run Smart Setup on the captured loop when it is kept");
  describe(meter_, "Input level", "Level of the track's input, with the peak since arming");
  describe(progressBar_, "Capture progress", "How much of the recording is done");
  describe(state_, "Capture state", "An unkept capture is lost when the project closes");
  describe(countdown_, "Capture time", "Time left in the recording, or elapsed time in free mode");
  progressBar_.setPercentageDisplay(false);

  add({&state_, &modeLabel_, &mode_, &barsLabel_, &bars_, &maxLength_, &length_, &meter_, &progressBar_, &countdown_, &arm_, &now_, &stop_, &keep_, &discard_, &chopAfter_, &undoKeep_});
  mode_.onChange = [this] { tick(); relayout(); };
  bars_.onChange = [this] { tick(); };
  maxLength_.onChange = [this] { tick(); };
  arm_.onClick = [this] { perform_.arm(choice(), view_.armNeedsConfirm); tick(); };
  now_.onClick = [this] { perform_.startCapture(); tick(); };
  stop_.onClick = [this] { perform_.stopCapture(); tick(); };
  keep_.onClick = [this] { perform_.keepCapture(view_, chopAfter_.getToggleState()); tick(); };
  discard_.onClick = [this] { perform_.discardCapture(); tick(); };
  undoKeep_.onClick = [this] { perform_.undoKeep(); tick(); };
  tick();
}

PerformCommands::CaptureChoice CapturePanel::choice() const {
  PerformCommands::CaptureChoice c;
  c.mode = mode_.getSelectedId() == 2 ? cf::capture::CaptureMode::Free : cf::capture::CaptureMode::SyncedBars;
  c.bars = cf::ui::kCaptureBarsChoices[std::max(0, std::min(3, bars_.getSelectedId() - 1))];
  c.maxSeconds = cf::ui::kCaptureMaxSecondsChoices[std::max(0, std::min(2, maxLength_.getSelectedId() - 1))];
  return c;
}

void CapturePanel::tick() {
  const auto c = choice();
  view_ = perform_.captureView(c);
  const auto& v = view_;
  const auto st = perform_.captureStatus();
  state_.setText(juce::String::fromUTF8(v.stateLabel.c_str()), juce::dontSendNotification);
  state_.setTooltip(juce::String::fromUTF8(v.stripTooltip.c_str()));
  length_.setText(juce::String::fromUTF8(v.lengthLabel.c_str()), juce::dontSendNotification);
  const bool showBars = v.showBars, showMax = v.showMaxLength;
  barsLabel_.setText(showMax ? "Max length" : "Bars", juce::dontSendNotification);
  bars_.setVisible(showBars);
  maxLength_.setVisible(showMax);
  mode_.setEnabled(v.optionsEnabled);
  bars_.setEnabled(v.optionsEnabled);
  maxLength_.setEnabled(v.optionsEnabled);
  arm_.setEnabled(v.armEnabled);
  now_.setEnabled(v.captureEnabled);
  stop_.setEnabled(v.stopEnabled);
  keep_.setEnabled(v.keepEnabled);
  discard_.setEnabled(v.discardEnabled);
  undoKeep_.setVisible(v.undoKeepVisible);
  const juce::String why = juce::String::fromUTF8(v.disabledReason.c_str());
  arm_.setTooltip(why.isNotEmpty() ? why : juce::String("Wait for the next bar line, then record the track's input"));
  meter_.set(v.meter, v.meterPeak, v.clipped, juce::String::fromUTF8(v.meterText.c_str()));
  meter_.setTooltip(juce::String::fromUTF8(v.meterTooltip.c_str()));
  const bool active = st.state == cf::capture::CaptureState::Capturing;
  progress_ = (active || st.state == cf::capture::CaptureState::Captured) && st.capacityFrames > 0
                  ? std::min(1.0, static_cast<double>(st.framesCaptured) / static_cast<double>(st.capacityFrames))
                  : 0.0;
  const double sr = perform_.sampleRate();
  if (active) {
    const double elapsed = static_cast<double>(st.framesCaptured) / sr;
    const double left = static_cast<double>(std::max<std::int64_t>(0, st.capacityFrames - st.framesCaptured)) / sr;
    countdown_.setText(c.mode == cf::capture::CaptureMode::SyncedBars ? "-" + juce::String(cf::ui::formatCaptureTime(left)) : juce::String(cf::ui::formatCaptureTime(elapsed)),
                       juce::dontSendNotification);
  } else {
    countdown_.setText(st.state == cf::capture::CaptureState::Armed ? "Waiting" : "", juce::dontSendNotification);
  }
  progressBar_.repaint();
}

int CapturePanel::layoutBody(int width) {
  beginLayout();
  Stack s(width);
  state_.setBounds(s.row(Stack::scaled(62)));
  labelled(s.row(), modeLabel_, mode_, Stack::scaled(64));
  {
    auto r = s.row();
    barsLabel_.setBounds(r.removeFromLeft(Stack::scaled(64)));
    bars_.setBounds(r);
    maxLength_.setBounds(r);
  }
  length_.setBounds(s.row());
  heading(s, "Input");
  meter_.setBounds(s.row());
  {
    auto r = s.row();
    countdown_.setBounds(r.removeFromRight(Stack::scaled(86)));
    progressBar_.setBounds(r.reduced(0, Stack::scaled(6)));
  }
  grid(s, {&arm_, &now_, &stop_, &keep_, &discard_, &undoKeep_}, 2);
  chopAfter_.setBounds(s.row());
  body_.repaint();
  return s.height();
}
