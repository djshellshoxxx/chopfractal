#include "InspectorPanel.h"

#include <chopfractal/chop_contracts/limits.hpp>
#include <chopfractal/chop_roles_grammar/grammar.hpp>
#include <chopfractal/plugin_ui_adapter/inspector_view.hpp>

#include "CdlLookAndFeel.h"

#include <initializer_list>

namespace cf = chopfractal;

namespace {
constexpr int kBodyH = 470;
juce::String text(const std::string& s) { return juce::String(s); }
}  // namespace

InspectorPanel::InspectorPanel(EditorModel& model, EditorCommands& commands)
    : EditorPanelBase(model, commands, "InspectorPanel"),
      level_("Level", "Playback level of the hit in dB; -48 dB and below is silent", cf::ui::kInspectorMinDb, cf::ui::kInspectorMaxDb, 0.5, [](double v) { return text(cf::ui::formatLevelDb(cf::ui::dbToLevel(v))); }),
      pan_("Pan", "Stereo position, L100 to R100", -1.0, 1.0, 0.01, [](double v) { return text(cf::ui::formatPan(static_cast<float>(v))); }),
      pitch_("Pitch", "Resampling pitch in semitones; the hit also gets shorter or longer", -cf::limits::kMaxPitchSemitones, cf::limits::kMaxPitchSemitones, 1.0, [](double v) { return text(cf::ui::formatPitch(static_cast<float>(v))); }),
      probability_("Chance", "How often the hit plays when the loop comes round", 0.0, 1.0, 0.01, [](double v) { return text(cf::ui::formatPercent(v)); }),
      retrigger_("Retrigger", "Repeat the hit this many times across its length", 1.0, cf::limits::kMaxRetrigger, 1.0, [](double v) { return juce::String(static_cast<int>(std::lround(v))); }) {
  addAndMakeVisible(viewport_);
  viewport_.setViewedComponent(&body_, false);
  viewport_.setScrollBarsShown(true, false);
  viewport_.setComponentID("InspectorViewport");

  for (juce::Label* l : {&title_, &subtitle_, &refusal_, &sourceLabel_, &roleLabel_}) {
    l->setInterceptsMouseClicks(false, false);
    body_.addAndMakeVisible(*l);
  }
  sourceLabel_.setText("Source", juce::dontSendNotification);
  roleLabel_.setText("Role", juce::dontSendNotification);
  refusal_.setJustificationType(juce::Justification::topLeft);

  source_.setTitle("Source chop of the selected hits");
  source_.setTooltip("Play a different chop in this hit");
  source_.onChange = [this] {
    if (!syncing_ && source_.getSelectedId() > 0) cmd_.setHitsLane(ids(), source_.getSelectedId() - 1);
  };
  role_.setTitle("Role of the hits' chop");
  role_.setTooltip("The role steers the next Generate or Mutate for this chop");
  for (const auto& r : cf::roles::builtinRoles()) role_.addItem(juce::String(r), role_.getNumItems() + 1);
  role_.onChange = [this] {
    if (!syncing_ && role_.getSelectedId() > 0) cmd_.setRoleOfHits(ids(), role_.getText());
  };
  body_.addAndMakeVisible(source_);
  body_.addAndMakeVisible(role_);

  level_.onCommit = [this](double db) {
    const float lin = cf::ui::dbToLevel(db);
    cmd_.setHitsTransform(ids(), [lin](cf::EventTransform& t) { t.level = lin; });
  };
  level_.onLive = [this](double db) {
    const float lin = cf::ui::dbToLevel(db);
    cmd_.previewHitsTransform(ids(), [lin](cf::EventTransform& t) { t.level = lin; });
  };
  level_.setDefault(0.0);
  pan_.onCommit = [this](double v) {
    const float p = static_cast<float>(v);
    cmd_.setHitsTransform(ids(), [p](cf::EventTransform& t) { t.pan = p; });
  };
  pan_.onLive = [this](double v) {
    const float p = static_cast<float>(v);
    cmd_.previewHitsTransform(ids(), [p](cf::EventTransform& t) { t.pan = p; });
  };
  pan_.setDefault(0.0);
  pitch_.onCommit = [this](double v) {
    const float p = static_cast<float>(v);
    cmd_.setHitsTransform(ids(), [p](cf::EventTransform& t) { t.pitchSemitones = p; });
  };
  pitch_.onLive = [this](double v) {
    const float p = static_cast<float>(v);
    cmd_.previewHitsTransform(ids(), [p](cf::EventTransform& t) { t.pitchSemitones = p; });
  };
  pitch_.setDefault(0.0);
  probability_.onCommit = [this](double v) { cmd_.setHitsProbability(ids(), static_cast<float>(v)); };
  probability_.onLive = [this](double v) { cmd_.previewHitsProbability(ids(), static_cast<float>(v)); };
  probability_.setDefault(1.0);
  retrigger_.onCommit = [this](double v) {
    const auto n = static_cast<std::uint8_t>(std::lround(v));
    cmd_.setHitsTransform(ids(), [n](cf::EventTransform& t) { t.retrigger = n; });
  };
  retrigger_.onLive = [this](double v) {
    const auto n = static_cast<std::uint8_t>(std::lround(v));
    cmd_.previewHitsTransform(ids(), [n](cf::EventTransform& t) { t.retrigger = n; });
  };
  retrigger_.setDefault(1.0);
  for (ParamRow* r : {&level_, &pan_, &pitch_, &probability_, &retrigger_}) body_.addAndMakeVisible(*r);

  reverse_.setTooltip("Play the hit backwards");
  reverse_.onClick = [this] {
    const bool on = reverse_.getToggleState();
    if (!syncing_) cmd_.setHitsTransform(ids(), [on](cf::EventTransform& t) { t.reverse = on; });
  };
  lock_.setTooltip("Keep Generate and Mutate away from the hit and refuse edits");
  lock_.onClick = [this] {
    if (!syncing_) cmd_.setHitsLocked(ids(), lock_.getToggleState());
  };
  mute_.setTooltip("Silence the hit without deleting it");
  mute_.onClick = [this] {
    if (syncing_) return;
    const bool muted = mute_.getToggleState();
    for (const auto id : ids()) cmd_.setHitEnabled(id, !muted);
  };
  for (juce::ToggleButton* b : {&reverse_, &lock_, &mute_}) {
    b->setTitle(b->getButtonText());
    body_.addAndMakeVisible(*b);
  }
  duplicate_.setTooltip("Copy the hit onto the next free cell of the grid");
  duplicate_.onClick = [this] { cmd_.duplicateSelected(); };
  delete_.setTooltip("Remove the hit");
  delete_.onClick = [this] { cmd_.deleteSelected(); };
  reset_.setTooltip("Back to level 0 dB, centre, no pitch, no effects, always plays");
  reset_.onClick = [this] {
    for (const auto id : ids()) cmd_.resetHit(id);
  };
  effects_.setTooltip("Open the effects rack for these hits");
  effects_.onClick = [this] {
    if (onOpenFx) onOpenFx();
  };
  for (juce::TextButton* b : {&duplicate_, &delete_, &reset_, &effects_}) {
    b->setTitle(b->getButtonText());
    body_.addAndMakeVisible(*b);
  }
  refresh();
}

void InspectorPanel::modelChanged(Change c) {
  if (c == Change::View || c == Change::Selection) refresh();
}

void InspectorPanel::setFlag(juce::ToggleButton& b, bool mixed, bool value, const juce::String& base) {
  b.setButtonText(mixed ? base + " (mixed)" : base);
  b.setToggleState(mixed ? false : value, juce::dontSendNotification);
}

void InspectorPanel::rebuildLaneItems() {
  const auto& chops = model_.view().chops;
  if (laneItems_ == static_cast<int>(chops.size())) return;
  laneItems_ = static_cast<int>(chops.size());
  source_.clear(juce::dontSendNotification);
  for (std::size_t i = 0; i < chops.size(); ++i) source_.addItem("Chop " + juce::String(static_cast<int>(i) + 1), static_cast<int>(i) + 1);
}

void InspectorPanel::refresh() {
  if (level_.dragging() || pan_.dragging() || pitch_.dragging() || probability_.dragging() || retrigger_.dragging()) return;  // do not fight the user's drag
  view_ = model_.inspector();
  syncing_ = true;
  rebuildLaneItems();
  title_.setText(text(view_.title), juce::dontSendNotification);
  subtitle_.setText(text(view_.subtitle), juce::dontSendNotification);
  refusal_.setText(text(view_.refusal), juce::dontSendNotification);
  const bool any = view_.hitCount > 0;
  if (view_.chopIndex >= 0) source_.setSelectedId(view_.chopIndex + 1, juce::dontSendNotification);
  else source_.setSelectedId(0, juce::dontSendNotification);
  source_.setTextWhenNothingSelected(view_.chopMixed ? "mixed" : "");
  if (!view_.role.empty()) {
    const auto roles = cf::roles::builtinRoles();
    int sel = 0;
    for (std::size_t i = 0; i < roles.size(); ++i)
      if (roles[i] == view_.role) sel = static_cast<int>(i) + 1;
    role_.setSelectedId(sel, juce::dontSendNotification);
  } else {
    role_.setSelectedId(0, juce::dontSendNotification);
  }
  role_.setTextWhenNothingSelected(view_.roleMixed ? "mixed" : "none");
  level_.setValue(view_.levelDb.value, view_.levelDb.mixed);
  pan_.setValue(view_.pan.value, view_.pan.mixed);
  pitch_.setValue(view_.pitch.value, view_.pitch.mixed);
  probability_.setValue(view_.probability.value, view_.probability.mixed);
  retrigger_.setValue(view_.retrigger.value, view_.retrigger.mixed);
  setFlag(reverse_, view_.reverseMixed, view_.reverse, "Reverse");
  setFlag(lock_, view_.lockedMixed, view_.locked, "Lock");
  setFlag(mute_, view_.mutedMixed, view_.muted, "Mute");
  const bool edit = any && view_.editable;
  for (juce::Component* c : std::initializer_list<juce::Component*>{&source_, &role_, &level_, &pan_, &pitch_, &probability_, &retrigger_, &reverse_, &mute_, &duplicate_, &delete_, &reset_, &effects_}) c->setEnabled(edit);
  lock_.setEnabled(any);  // a locked hit has to stay unlockable
  syncing_ = false;
  repaint();
}

void InspectorPanel::paint(juce::Graphics& g) { g.fillAll(cdl::pal().panel); }

void InspectorPanel::resized() {
  viewport_.setBounds(getLocalBounds());
  const int w = std::max(200, viewport_.getMaximumVisibleWidth());
  body_.setSize(w, kBodyH);
  auto a = body_.getLocalBounds().reduced(10, 8);
  title_.setBounds(a.removeFromTop(22));
  subtitle_.setBounds(a.removeFromTop(18));
  refusal_.setBounds(a.removeFromTop(view_.refusal.empty() ? 4 : 34));
  a.removeFromTop(4);
  auto row = [&](int h = 28) { return a.removeFromTop(h).reduced(0, 2); };
  auto r = row();
  sourceLabel_.setBounds(r.removeFromLeft(78));
  source_.setBounds(r);
  r = row();
  roleLabel_.setBounds(r.removeFromLeft(78));
  role_.setBounds(r);
  a.removeFromTop(6);
  for (ParamRow* p : {&level_, &pan_, &pitch_, &probability_, &retrigger_}) p->setBounds(row(30));
  a.removeFromTop(6);
  r = row();
  reverse_.setBounds(r.removeFromLeft(r.getWidth() / 2));
  mute_.setBounds(r);
  r = row();
  lock_.setBounds(r);
  a.removeFromTop(10);
  r = row(30);
  duplicate_.setBounds(r.removeFromLeft(r.getWidth() / 2 - 3));
  r.removeFromLeft(6);
  delete_.setBounds(r);
  r = row(30);
  reset_.setBounds(r.removeFromLeft(r.getWidth() / 2 - 3));
  r.removeFromLeft(6);
  effects_.setBounds(r);
}
