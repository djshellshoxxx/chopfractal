#include "EvolvePanel.h"

#include <algorithm>
#include <cmath>

namespace cf = chopfractal;
using namespace perform_ui;

namespace {
const char* uiRoleName(cf::insight::Role r) {
  switch (r) {
    case cf::insight::Role::Kick: return "kick";
    case cf::insight::Role::Snare: return "snare";
    case cf::insight::Role::Hat: return "hat";
    case cf::insight::Role::Cymbal: return "cymbal";
    default: return "other";
  }
}
}  // namespace

void SuggestionList::set(std::vector<juce::String> loopLines, std::vector<juce::String> roleLines, const juce::String& emptyText) {
  if (loopLines == loops_ && roleLines == roles_ && emptyText == empty_) return;
  loops_ = std::move(loopLines);
  roles_ = std::move(roleLines);
  empty_ = emptyText;
  repaint();
}

int SuggestionList::heightFor() const { return Stack::scaled(20) * std::max(2, std::min(lineCount() + (loops_.empty() || roles_.empty() ? 0 : 1), 40)) + Stack::scaled(8); }

void SuggestionList::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  g.setColour(p.bg);
  g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.f);
  g.setColour(p.edge);
  g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.f, 1.f);
  auto r = getLocalBounds().reduced(Stack::scaled(6), Stack::scaled(4));
  const int line = Stack::scaled(20);
  g.setFont(cdl::font(cdl::FontRole::Body, cdl::uiScale()));
  if (lineCount() == 0) {
    g.setColour(p.inkMuted);
    g.drawFittedText(empty_, r, juce::Justification::topLeft, 3);
    return;
  }
  auto drawLines = [&](const std::vector<juce::String>& lines, juce::Colour c) {
    g.setColour(c);
    for (const auto& l : lines) g.drawText(l, r.removeFromTop(line), juce::Justification::centredLeft, true);
  };
  drawLines(loops_, p.ink);
  if (!loops_.empty() && !roles_.empty()) r.removeFromTop(line);
  drawLines(roles_, p.inkMuted);
}

const std::vector<std::pair<const char*, const char*>>& EvolvePanel::motifPresets() {
  static const std::vector<std::pair<const char*, const char*>> kPresets = {
      {"Default", "x.xx"}, {"Straight", "xxxx"}, {"Offbeat", ".x.x"}, {"Accented", "axxx"}, {"Gallop", "x.xx.x.x"}, {"Clave", "x..x..x."}, {"Sparse", "x...x..."}, {"Skip", "xx.x.xx."}};
  return kPresets;
}

EvolvePanel::EvolvePanel(EditorModel& m, EditorCommands& c) : DrawerPanel(m, c, "EvolvePanel", 10) {
  for (auto* l : {&motifLabel_, &presetLabel_, &depthLabel_, &accentLabel_, &everyLabel_, &amountLabel_}) styleLabel(*l, l->getText());
  styleLabel(evolveStatus_, "");
  evolveStatus_.setJustificationType(juce::Justification::topLeft);

  motifPreset_.setTextWhenNothingSelected("Choose a motif...");
  int id = 1;
  for (const auto& p : motifPresets()) motifPreset_.addItem(juce::String(p.first) + "  " + p.second, id++);
  motif_.setText(juce::String(model_.extras().motif), juce::dontSendNotification);
  motif_.setInputRestrictions(8, "xa.");
  depth_.addItemList({"1", "2", "3"}, 1);
  depth_.setSelectedId(model_.extras().fractalDepth, juce::dontSendNotification);
  accent_.setSliderStyle(juce::Slider::LinearHorizontal);
  accent_.setTextBoxStyle(juce::Slider::TextBoxRight, false, static_cast<int>(52 * cdl::uiScale()), 22);
  accent_.setRange(0.1, 1.0, 0.01);
  accent_.setValue(model_.extras().fractalAccentDecay, juce::dontSendNotification);
  every_.addItemList({"1 loop", "2 loops", "4 loops", "8 loops", "16 loops"}, 1);
  every_.setSelectedId(3, juce::dontSendNotification);
  evolveAmount_.setSliderStyle(juce::Slider::LinearHorizontal);
  evolveAmount_.setTextBoxStyle(juce::Slider::TextBoxRight, false, static_cast<int>(52 * cdl::uiScale()), 22);
  evolveAmount_.setRange(0.05, 1.0, 0.01);
  evolveAmount_.setValue(0.25, juce::dontSendNotification);
  acceptRoles_.setEnabled(false);
  useTempo_.setEnabled(false);
  fractal_.setEnabled(false);
  smart_.setEnabled(false);

  // Titles keep the names the Features panel used, so existing tools and tests still find these controls.
  describe(motifPreset_, "Motif preset", "A ready-made motif to start from");
  describe(motif_, "Fractal motif: x hit, a accent, dot rest", "Up to eight characters: x is a hit, a is an accent, dot is a rest");
  describe(depth_, "Fractal depth", "How many nested levels the rhythm repeats itself");
  describe(accent_, "Accent decay", "How much quieter each nested level gets");
  describe(fractal_, "Fractal", "Build the nested rhythm from the motif");
  describe(evolve_, "Run Evolve", "Change the pattern a little every few loops while it plays");
  describe(every_, "Evolve every", "How many loops pass between changes");
  describe(evolveAmount_, "Evolve amount", "How much each change moves the rhythm");
  describe(keep_, "Keep", "Keep the current pattern and stop Evolve");
  describe(smart_, "Smart Setup", "Listen to the loop and suggest roles, length and tempo");
  describe(acceptRoles_, "Accept Roles", "Give the suggested roles to chops that have none");
  describe(useTempo_, "Use Tempo", "Use the suggested tempo when the host gives none");
  describe(suggestions_, "Smart Setup suggestions", "What Smart Setup found in the loop");
  describe(evolveStatus_, "Evolve status", "What Evolve is doing now");

  add({&presetLabel_, &motifPreset_, &motifLabel_, &motif_, &depthLabel_, &depth_, &accentLabel_, &accent_, &fractal_, &evolve_, &everyLabel_, &every_, &amountLabel_, &evolveAmount_,
       &keep_, &evolveStatus_, &smart_, &suggestions_, &acceptRoles_, &useTempo_});

  motifPreset_.onChange = [this] {
    const int i = motifPreset_.getSelectedId() - 1;
    if (updating_ || i < 0 || i >= static_cast<int>(motifPresets().size())) return;
    motif_.setText(motifPresets()[static_cast<std::size_t>(i)].second, true);
  };
  motif_.onTextChange = [this] {
    if (updating_) return;
    model_.setExtras([this](cf::composition::ProjectExtras& e) { e.motif = motif_.getText().toStdString(); });
  };
  depth_.onChange = [this] {
    if (!updating_) model_.setExtras([this](cf::composition::ProjectExtras& e) { e.fractalDepth = depth_.getSelectedId(); });
  };
  accent_.onValueChange = [this] {
    if (!updating_) model_.setExtras([this](cf::composition::ProjectExtras& e) { e.fractalAccentDecay = accent_.getValue(); });
  };
  fractal_.onClick = [this] { cmd_.applyFractal(fractalUi()); };
  evolve_.onClick = [this] { pushEvolve(); };
  every_.onChange = [this] {
    if (!updating_ && evolve_.getToggleState()) pushEvolve();
  };
  evolveAmount_.onDragEnd = [this] {
    if (evolve_.getToggleState()) pushEvolve();
  };
  keep_.onClick = [this] { cmd_.keepEvolved(); };
  smart_.onClick = [this] { cmd_.smartSetup(); };
  acceptRoles_.onClick = [this] { cmd_.acceptRoles(); };
  useTempo_.onClick = [this] { cmd_.useTempo(); };
  refreshSuggestions();
}

FractalUi EvolvePanel::fractalUi() const {
  FractalUi f;
  f.motif = motif_.getText().toStdString();
  f.depth = depth_.getSelectedId();
  f.mutation = model_.extras().fractalMutation;
  f.accent = model_.extras().fractalAccentDecay;
  return f;
}

EvolveUi EvolvePanel::evolveUi() const { return {evolve_.getToggleState(), every_.getSelectedId() - 1, evolveAmount_.getValue()}; }

void EvolvePanel::setEvolveEnabled(bool on) {
  evolve_.setToggleState(on, juce::dontSendNotification);
  pushEvolve();
}

void EvolvePanel::pushEvolve() {
  const EvolveUi ui = evolveUi();
  const bool running = cmd_.setEvolve(ui, pushedEvolve_);
  if (ui.enabled && !running) evolve_.setToggleState(false, juce::dontSendNotification);  // refused (for example no pattern yet)
}

void EvolvePanel::showPreset(const cf::presets::Preset& p) {
  updating_ = true;
  motif_.setText(juce::String(p.motif), juce::dontSendNotification);
  depth_.setSelectedId(std::max(1, std::min(3, p.depth)), juce::dontSendNotification);
  accent_.setValue(p.accentDecay, juce::dontSendNotification);
  static const int kEvery[] = {1, 2, 4, 8, 16};
  for (int i = 0; i < 5; ++i)
    if (kEvery[i] == p.evolveEvery) every_.setSelectedId(i + 1, juce::dontSendNotification);
  evolveAmount_.setValue(p.evolveAmount, juce::dontSendNotification);
  updating_ = false;
}

void EvolvePanel::setCadence(int everyIndex, double amount) {
  updating_ = true;
  every_.setSelectedId(std::max(0, std::min(4, everyIndex)) + 1, juce::dontSendNotification);
  evolveAmount_.setValue(amount, juce::dontSendNotification);
  updating_ = false;
}

void EvolvePanel::refreshSuggestions() {
  std::vector<juce::String> loops, roles;
  juce::String empty = model_.view().hasSource ? "Press Smart Setup to hear what the loop suggests." : "Load a loop first.";
  if (model_.haveSetup()) {
    const auto& st = model_.setup();
    for (const auto& l : st.loops)
      loops.push_back("Loop: " + juce::String::fromUTF8(l.note.c_str()) + "  " + juce::String(static_cast<int>(std::lround(l.confidence * 100.0))) + "%");
    const auto& chops = model_.view().chops;
    for (const auto& r : st.roles) {
      int number = 0;
      for (std::size_t i = 0; i < chops.size(); ++i)
        if (chops[i].id == r.chop) number = static_cast<int>(i) + 1;
      roles.push_back("Chop " + juce::String(number).paddedLeft('0', 2) + "  " + juce::String(uiRoleName(r.role)) + "  " +
                      juce::String(static_cast<int>(std::lround(r.confidence * 100.0))) + "%  " + juce::String::fromUTF8(r.reason.c_str()));
    }
    if (loops.empty() && roles.empty()) empty = "Smart Setup found nothing to suggest.";
  }
  const int before = suggestions_.heightFor();
  suggestions_.set(std::move(loops), std::move(roles), empty);
  if (before != suggestions_.heightFor()) relayout();
}

void EvolvePanel::modelChanged(Change c) {
  switch (c) {
    case Change::Evolve: {
      const bool running = model_.evolveRunning();
      if (evolve_.getToggleState() != running) evolve_.setToggleState(running, juce::dontSendNotification);
      const auto& se = model_.evolve();
      if (!running && (se.everyLoops != pushedEvolve_.everyLoops || se.amount != pushedEvolve_.amount || se.startSeed != pushedEvolve_.startSeed)) {
        pushedEvolve_ = se;
        updating_ = true;
        static const int kEvery[] = {1, 2, 4, 8, 16};
        for (int i = 0; i < 5; ++i)
          if (kEvery[i] == se.everyLoops && every_.getSelectedId() != i + 1) every_.setSelectedId(i + 1, juce::dontSendNotification);
        evolveAmount_.setValue(se.amount, juce::dontSendNotification);
        updating_ = false;
      }
      evolveStatus_.setText(running ? "Evolve is running: it changes the pattern every " + every_.getText() + "." : "Evolve is off.", juce::dontSendNotification);
      break;
    }
    case Change::View: {
      const auto& v = model_.view();
      smart_.setEnabled(v.hasSource);
      fractal_.setEnabled(v.hasPattern);
      break;
    }
    case Change::Extras: {  // a project was restored: show its saved choices without firing the handlers
      const auto& e = model_.extras();
      updating_ = true;
      if (motif_.getText().toStdString() != e.motif) motif_.setText(juce::String(e.motif), juce::dontSendNotification);
      if (depth_.getSelectedId() != e.fractalDepth) depth_.setSelectedId(e.fractalDepth, juce::dontSendNotification);
      accent_.setValue(e.fractalAccentDecay, juce::dontSendNotification);
      updating_ = false;
      break;
    }
    case Change::Setup:
      acceptRoles_.setEnabled(model_.haveSetup() && !model_.setup().roles.empty());
      useTempo_.setEnabled(model_.haveSetup() && !model_.setup().loops.empty());
      refreshSuggestions();
      break;
    default:
      break;
  }
}

int EvolvePanel::layoutBody(int width) {
  beginLayout();
  Stack s(width);
  const int lw = Stack::scaled(96);
  {
    auto r = s.row();
    evolve_.setBounds(r.removeFromLeft(r.getWidth() / 2));
    keep_.setBounds(r);
  }
  labelled(s.row(), everyLabel_, every_, lw);
  labelled(s.row(), amountLabel_, evolveAmount_, lw);
  evolveStatus_.setBounds(s.row(Stack::scaled(34)));
  heading(s, "Fractal");
  labelled(s.row(), presetLabel_, motifPreset_, lw);
  labelled(s.row(), motifLabel_, motif_, lw);
  labelled(s.row(), depthLabel_, depth_, lw);
  labelled(s.row(), accentLabel_, accent_, lw);
  fractal_.setBounds(s.row());
  heading(s, "Smart Setup");
  smart_.setBounds(s.row());
  suggestions_.setBounds(s.row(suggestions_.heightFor()));
  grid(s, {&acceptRoles_, &useTempo_}, 2);
  body_.repaint();
  return s.height();
}
