#include "QuickStartCard.h"

#include "CdlLookAndFeel.h"
#include "EditorKeys.h"

namespace {
const char* kButtonText[kQuickSteps] = {"Load...", "Detect", "Make one", "Hear it", "Export..."};
constexpr int kCelebrateTicks = 50;  // 5 s at the editor's 10 Hz tick
}  // namespace

QuickStartCard::QuickStartCard(EditorModel& m, EditorCommands& c, QuickStartTracker& t) : EditorPanelBase(m, c, "QuickStartCard"), tracker_(t) {
  setTitle("Quick start");
  for (int i = 0; i < kQuickSteps; ++i) {
    auto& b = rows_[static_cast<std::size_t>(i)].button;
    b.setButtonText(kButtonText[i]);
    b.setTitle(QuickStartTracker::stepTitle(static_cast<QuickStep>(i)));
    b.onClick = [this, i] { runStep(i); };
    addAndMakeVisible(b);
  }
  rows_[0].button.setTooltip("Choose a WAV or AIFF loop up to 60 seconds");
  skip_.setTitle("Skip quick start");
  skip_.setTooltip("Hide the checklist for now, the Quick start button brings it back");
  skip_.onClick = [this] { skip(); };
  addAndMakeVisible(skip_);
  hide_.setTitle("Hide quick start");
  hide_.setTooltip("Never show the checklist on its own again");
  hide_.setToggleState(tracker_.dismissed(), juce::dontSendNotification);
  hide_.onClick = [this] { setHidden(hide_.getToggleState()); };
  addAndMakeVisible(hide_);
  setVisible(false);
  refreshRows();
}

juce::Point<int> QuickStartCard::preferredSize() const { return {static_cast<int>(300 * scale_), static_cast<int>(150 * scale_)}; }

void QuickStartCard::setScale(float s) {
  scale_ = s;
  resized();
  repaint();
}

juce::String QuickStartCard::rowTitle(int i) const {
  return "Quick start step " + juce::String(i + 1) + " of " + juce::String(kQuickSteps) + ": " + QuickStartTracker::stepTitle(static_cast<QuickStep>(i)) + (stepDone(i) ? ", done" : ", not done");
}

juce::String QuickStartCard::stepReason(int i) const {
  const auto& v = model_.view();
  switch (static_cast<QuickStep>(i)) {
    case QuickStep::Load: return {};
    case QuickStep::Detect: return v.hasSource ? juce::String() : juce::String("Load a loop first.");
    case QuickStep::Generate:
      if (!v.hasSource) return "Load a loop first.";
      return v.chops.empty() ? juce::String("Detect chops first.") : juce::String();
    case QuickStep::Play:
    case QuickStep::Export:
      if (!v.hasSource) return "Load a loop first.";
      return v.hasPattern ? juce::String() : juce::String("Generate a pattern first.");
  }
  return {};
}

void QuickStartCard::runStep(int i) {
  if (stepReason(i).isNotEmpty()) return;
  switch (static_cast<QuickStep>(i)) {
    case QuickStep::Load: cmd_.chooseFile(); break;
    case QuickStep::Detect: cmd_.detectChops(static_cast<DetectChoice>(model_.detectMode())); break;
    case QuickStep::Generate: cmd_.generate(); break;
    case QuickStep::Play: cmd_.play(); break;
    case QuickStep::Export:
      if (onExport) onExport();
      else cmd_.exportWav(cmd_.exportUi());
      break;
  }
}

void QuickStartCard::refreshRows() {
  for (int i = 0; i < kQuickSteps; ++i) {
    auto& b = rows_[static_cast<std::size_t>(i)].button;
    const juce::String why = stepReason(i);
    b.setEnabled(why.isEmpty());
    b.setTitle(rowTitle(i));
    if (i > 0) {
      const juce::String what = i == 1 ? "Find chop markers in the loop" : i == 2 ? "Make a pattern from the chops (G)" : i == 3 ? "Start local playback, or press play in your DAW" : "Render the pattern to a WAV file";
      b.setTooltip(why.isNotEmpty() ? what + ", " + why.toLowerCase().trimCharactersAtEnd(".") : what);
    }
    b.setDescription(why);
    b.setVisible(celebrateTicks_ <= 0);
  }
  hide_.setToggleState(tracker_.dismissed(), juce::dontSendNotification);
}

void QuickStartCard::tick() { update(true); }

void QuickStartCard::update(bool advance) {
  const bool all = tracker_.allDone();
  bool started = false;
  if (all && !wasAllDone_ && tracker_.revision() != 0 && isVisible()) {
    celebrateTicks_ = kCelebrateTicks;
    started = true;
  }
  wasAllDone_ = all;
  if (celebrateTicks_ > 0 && !started && advance) {
    --celebrateTicks_;
    if (celebrateTicks_ == 0) {
      forced_ = false;
      skipped_ = true;  // collapses to the status bar button
    }
  }
  const bool noSource = !model_.view().hasSource;
  const bool want = celebrateTicks_ > 0 || forced_ || (!tracker_.dismissed() && !skipped_ && noSource && !all);
  refreshRows();
  if (want != wanted_) {
    wanted_ = want;
    setVisible(want);
    if (onChanged) onChanged();
  }
  repaint();
}

void QuickStartCard::showNow() {
  forced_ = true;
  skipped_ = false;
  celebrateTicks_ = 0;
  tick();
}

void QuickStartCard::skip() {
  forced_ = false;
  skipped_ = true;
  celebrateTicks_ = 0;
  tick();
}

void QuickStartCard::celebrate() {
  celebrateTicks_ = kCelebrateTicks;
  update(false);
}

void QuickStartCard::setHidden(bool hidden) {
  tracker_.setDismissed(hidden);
  if (hidden) {
    forced_ = false;
    celebrateTicks_ = 0;
  }
  tick();
  if (onChanged) onChanged();
}

void QuickStartCard::resized() {
  const int s = std::max(1, static_cast<int>(scale_ * 100));
  auto sc = [&](int v) { return v * s / 100; };
  auto area = getLocalBounds().reduced(sc(10), sc(8));
  area.removeFromTop(sc(18));  // title
  const int rowH = sc(18);
  for (int i = 0; i < kQuickSteps; ++i) {
    auto r = area.removeFromTop(rowH);
    rowAreas_[static_cast<std::size_t>(i)] = r;
    auto& b = rows_[static_cast<std::size_t>(i)].button;
    b.setBounds(r.removeFromRight(sc(84)).reduced(0, sc(1)));
  }
  area.removeFromTop(sc(6));
  auto foot = area.removeFromTop(sc(20));
  skip_.setBounds(foot.removeFromLeft(sc(50)));
  hide_.setBounds(foot.removeFromRight(sc(140)));
}

void QuickStartCard::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  g.setColour(p.panel);
  g.fillRect(getLocalBounds());
  g.setColour(p.edgeHi);
  g.drawRect(getLocalBounds(), 1);
  g.setColour(p.spray);
  g.fillRect(0, 0, std::max(2, static_cast<int>(3 * scale_)), getHeight());
  g.setFont(cdl::font(cdl::FontRole::Label, scale_));
  g.setColour(p.ink);
  g.drawText("QUICK START", static_cast<int>(12 * scale_), static_cast<int>(6 * scale_), getWidth() - 24, static_cast<int>(18 * scale_), juce::Justification::centredLeft, false);
  const bool celebrate = celebrateTicks_ > 0;
  if (celebrate) {
    g.setFont(cdl::font(cdl::FontRole::Body, scale_));
    g.setColour(p.ok);
    const auto t = juce::String("You are set. Press ") + shortcutText(EditorAction::OpenPalette) + " to find everything else.";
    g.drawFittedText(t, getLocalBounds().reduced(static_cast<int>(14 * scale_), static_cast<int>(28 * scale_)).withTrimmedBottom(static_cast<int>(26 * scale_)), juce::Justification::centred, 3);
  }
  for (int i = 0; i < kQuickSteps; ++i) {
    if (celebrate) continue;
    auto r = rowAreas_[static_cast<std::size_t>(i)];
    const bool done = stepDone(i);
    auto mark = r.removeFromLeft(static_cast<int>(18 * scale_)).toFloat().reduced(2.0f * scale_);
    g.setColour(done ? p.ok : p.inkMuted);
    if (done) {
      juce::Path check;
      check.startNewSubPath(mark.getX(), mark.getCentreY());
      check.lineTo(mark.getCentreX() - 1.0f, mark.getBottom() - 2.0f);
      check.lineTo(mark.getRight(), mark.getY() + 2.0f);
      g.strokePath(check, juce::PathStrokeType(1.8f * scale_));
    } else {
      g.drawEllipse(mark.withSizeKeepingCentre(7.0f * scale_, 7.0f * scale_), 1.2f);
    }
    g.setFont(cdl::font(cdl::FontRole::Body, scale_));
    g.setColour(done ? p.inkMuted : p.ink);
    g.drawText(QuickStartTracker::stepTitle(static_cast<QuickStep>(i)), r.withTrimmedRight(static_cast<int>(86 * scale_)), juce::Justification::centredLeft, true);
  }
}
