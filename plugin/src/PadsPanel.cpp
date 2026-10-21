#include "PadsPanel.h"

#include <cmath>

namespace cf = chopfractal;
using namespace perform_ui;

// ---------------------------------------------------------------------------------------------
// PadCellComponent
// ---------------------------------------------------------------------------------------------

PadCellComponent::PadCellComponent(PerformCommands& perform, int index) : perform_(perform) {
  setComponentID("pad-" + juce::String(index + 1));
  setName("Pad " + juce::String(index + 1));
  setWantsKeyboardFocus(true);
  setMouseClickGrabsKeyboardFocus(false);
}

PadCellComponent::~PadCellComponent() { release(); }

void PadCellComponent::set(const cf::ui::PadCell& c) {
  const bool changed = c.label != cell_.label || c.role != cell_.role || c.noteName != cell_.noteName || c.enabled != cell_.enabled || c.lit != cell_.lit ||
                       c.chokeGroup != cell_.chokeGroup || c.title != cell_.title;
  cell_ = c;
  setName(juce::String::fromUTF8(c.name.c_str()));
  setTitle(juce::String::fromUTF8(c.title.c_str()));
  setTooltip(juce::String::fromUTF8(c.title.c_str()));
  setWantsKeyboardFocus(c.enabled);
  if (!c.enabled) release();
  if (changed) repaint();
}

void PadCellComponent::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  const float s = cdl::uiScale();
  auto r = getLocalBounds().toFloat().reduced(2.f);
  const bool on = held_ || cell_.lit;
  g.setColour(!cell_.enabled ? p.bg : (on ? p.sprayWash : p.knobBottom));
  g.fillRoundedRectangle(r, 3.f);
  if (cell_.enabled && cell_.chop >= 0) {  // lane tint along the top edge ties the pad to its lane in the pattern
    g.setColour(juce::Colour::fromHSV(static_cast<float>(cell_.chop % 8) / 8.f, 0.55f, 0.85f, 1.f).withAlpha(0.9f));
    g.fillRect(r.withHeight(3.f * s).reduced(3.f, 0.f));
  }
  g.setColour(on ? p.spray : (hasKeyboardFocus(false) ? p.rain : p.edgeHi));
  g.drawRoundedRectangle(r, 3.f, on || hasKeyboardFocus(false) ? 2.f : 1.f);
  if (on) {  // the marker is the non-colour cue for "sounding"
    g.setColour(p.spray);
    g.fillRect(juce::Rectangle<float>(r.getRight() - 9.f * s, r.getY() + 6.f * s, 5.f * s, 5.f * s));
  }
  if (cell_.chokeGroup > 0 && cell_.enabled) {
    g.setColour(p.inkMuted);
    g.setFont(cdl::font(cdl::FontRole::MonoXs, s));
    g.drawText("G" + juce::String(cell_.chokeGroup), r.toNearestInt().reduced(5, 4), juce::Justification::bottomRight, false);
  }
  if (cell_.chop < 0) return;
  g.setColour(cell_.enabled ? p.ink : p.inkMuted);
  g.setFont(cdl::font(cdl::FontRole::ValueLg, s));
  g.drawText(juce::String::fromUTF8(cell_.label.c_str()), r.toNearestInt(), juce::Justification::centred, false);
  g.setColour(p.inkMuted);
  g.setFont(cdl::font(cdl::FontRole::MonoXs, s));
  g.drawText(juce::String::fromUTF8(cell_.noteName.c_str()), r.toNearestInt().reduced(5, 4), juce::Justification::topLeft, false);
  if (!cell_.role.empty()) g.drawText(juce::String::fromUTF8(cell_.role.c_str()), r.toNearestInt().reduced(5, 4), juce::Justification::centredBottom, true);
}

void PadCellComponent::press(int velocity) {
  if (held_ || !cell_.enabled || cell_.note < 0) return;
  held_ = true;
  heldNote_ = cell_.note;
  perform_.padTrigger(heldNote_, velocity, true);
  repaint();
}

void PadCellComponent::release() {
  if (!held_) return;
  held_ = false;
  perform_.padTrigger(heldNote_, 0, false);
  heldNote_ = -1;
  repaint();
}

void PadCellComponent::mouseDown(const juce::MouseEvent& e) {
  if (e.mods.isPopupMenu()) {
    chokeMenu();
    return;
  }
  press(cf::ui::padVelocityFromY(static_cast<double>(e.position.y), static_cast<double>(getHeight())));
}
void PadCellComponent::mouseUp(const juce::MouseEvent&) { release(); }
void PadCellComponent::mouseExit(const juce::MouseEvent&) { release(); }

bool PadCellComponent::keyPressed(const juce::KeyPress& k) {
  if (k == juce::KeyPress::spaceKey || k == juce::KeyPress::returnKey) {
    press(100);
    return true;
  }
  return false;
}
bool PadCellComponent::keyStateChanged(bool) {
  if (held_ && !juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey) && !juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::returnKey)) release();
  return false;
}

void PadCellComponent::chokeMenu() {
  if (cell_.chop < 0) return;
  juce::PopupMenu menu;
  const auto items = cf::ui::padChokeMenuItems();
  for (std::size_t i = 0; i < items.size(); ++i) menu.addItem(static_cast<int>(i) + 1, juce::String::fromUTF8(items[i].c_str()), true, static_cast<int>(i) == cell_.chokeGroup);
  const int chop = cell_.chop;
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, chop](int r) {
    if (r > 0) perform_.setChokeGroup(chop, r - 1);
  });
}

// ---------------------------------------------------------------------------------------------
// PadGrid
// ---------------------------------------------------------------------------------------------

PadGrid::PadGrid(PerformCommands& perform) {
  setComponentID("PadGrid");
  for (int i = 0; i < cf::ui::kPadsPerPage; ++i) {
    pads_[static_cast<std::size_t>(i)] = std::make_unique<PadCellComponent>(perform, i);
    addAndMakeVisible(*pads_[static_cast<std::size_t>(i)]);
  }
}

void PadGrid::set(const cf::ui::PadView& v) {
  for (int i = 0; i < cf::ui::kPadsPerPage && i < static_cast<int>(v.pads.size()); ++i) pads_[static_cast<std::size_t>(i)]->set(v.pads[static_cast<std::size_t>(i)]);
  const juce::String text = v.padsEnabled ? juce::String() : juce::String::fromUTF8(v.emptyText.c_str());
  if (text != emptyText_) {
    emptyText_ = text;
    repaint();
  }
}

void PadGrid::resized() {
  const int cols = 4;
  const float cw = static_cast<float>(getWidth()) / cols, ch = static_cast<float>(getHeight()) / cols;
  for (int i = 0; i < cf::ui::kPadsPerPage; ++i) {
    // Row 0 of the pad bank is the bottom row, like a drum machine: pad 1 sits at the bottom left.
    const int col = i % cols, row = cols - 1 - i / cols;
    pads_[static_cast<std::size_t>(i)]->setBounds(juce::Rectangle<float>(col * cw, row * ch, cw, ch).toNearestInt());
  }
}

void PadGrid::paint(juce::Graphics& g) {
  if (emptyText_.isEmpty()) return;
  g.setColour(cdl::pal().inkMuted);
  g.setFont(cdl::font(cdl::FontRole::Body, cdl::uiScale()));
  g.drawFittedText(emptyText_, getLocalBounds().reduced(12), juce::Justification::centred, 4);
}

// ---------------------------------------------------------------------------------------------
// PadsPanel
// ---------------------------------------------------------------------------------------------

PadsPanel::PadsPanel(EditorModel& m, EditorCommands& c, PerformCommands& perform, juce::AudioProcessorValueTreeState& apvts)
    : DrawerPanel(m, c, "PadsPanel", 30), perform_(perform), grid_(perform) {
  styleLabel(pageLabel_, "Page");
  styleLabel(modeLabel_, "Mode");
  styleLabel(curveLabel_, "Velocity");
  styleLabel(baseLabel_, "Base note");
  styleLabel(channelLabel_, "MIDI channel");
  styleLabel(quantizeLabel_, "Quantize");
  styleLabel(mergeLabel_, "Merge");
  styleLabel(midiIn_, "MIDI in: none yet");
  styleLabel(hint_, "");
  hint_.setJustificationType(juce::Justification::topLeft);
  for (const char* item : cf::ui::kPadModeItems) mode_.addItem(item, mode_.getNumItems() + 1);
  for (const char* item : cf::ui::kPadCurveItems) curve_.addItem(item, curve_.getNumItems() + 1);
  int id = 1;
  for (const auto& item : cf::ui::padMidiChannelItems()) channel_.addItem(juce::String::fromUTF8(item.c_str()), id++);
  for (const char* item : cf::ui::kPadQuantizeItems) quantize_.addItem(item, quantize_.getNumItems() + 1);
  for (const char* item : cf::ui::kPadMergeItems) merge_.addItem(item, merge_.getNumItems() + 1);
  baseNote_.setSliderStyle(juce::Slider::LinearHorizontal);
  baseNote_.setTextBoxStyle(juce::Slider::TextBoxRight, false, static_cast<int>(70 * cdl::uiScale()), 22);
  baseNote_.setRange(0, 111, 1);
  baseNote_.setValue(36, juce::dontSendNotification);
  baseNote_.textFromValueFunction = [](double v) { return juce::String::fromUTF8(cf::ui::padBaseNoteText(static_cast<int>(v)).c_str()); };

  describe(page_c_, "Pad page", "Which sixteen chops the pads play");
  describe(mode_, "Pad mode", "Poly lets pads overlap, Mono cuts the previous pad, Gate plays while the pad is held");
  describe(curve_, "Velocity curve", "How the pad velocity maps to loudness");
  describe(baseNote_, "Base note", "The MIDI note of the first chop's pad");
  describe(channel_, "MIDI channel", "The MIDI channel the pads answer on");
  describe(record_, "Record", "Record the pads you play into the pattern while the transport runs");
  describe(quantize_, "Quantize", "Snap recorded hits to this grid");
  describe(merge_, "Merge", "Add recorded hits to the pattern, or replace the hit in the same slot");
  describe(discard_, "Discard Take", "Throw away the hits recorded so far");
  describe(learn_, "Learn", "Press a key on your MIDI controller to make it the first pad");
  describe(midiIn_, "MIDI in", "The last MIDI note received");
  describe(hint_, "Pad status", "What the pads need next");

  modeAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, "perf_mode", mode_);
  curveAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, "perf_velocity_curve", curve_);

  add({&grid_, &pageLabel_, &page_c_, &modeLabel_, &mode_, &curveLabel_, &curve_, &baseLabel_, &baseNote_, &channelLabel_, &channel_, &record_, &quantizeLabel_, &quantize_,
       &mergeLabel_, &merge_, &discard_, &learn_, &midiIn_, &hint_});

  page_c_.onChange = [this] {
    if (!updating_) {
      page_ = std::max(0, page_c_.getSelectedId() - 1);
      tick();
    }
  };
  baseNote_.onValueChange = [this] {
    if (!updating_) perform_.editPadSettings([&](cf::composition::PadSettings& s) { s.baseNote = static_cast<std::uint8_t>(baseNote_.getValue()); });
  };
  channel_.onChange = [this] {
    if (!updating_) perform_.editPadSettings([&](cf::composition::PadSettings& s) { s.midiChannel = static_cast<std::uint8_t>(channel_.getSelectedId() - 1); });
  };
  quantize_.onChange = [this] {
    if (updating_) return;
    const auto q = cf::ui::padQuantizeFromIndex(quantize_.getSelectedId() - 1);
    perform_.editPadSettings([&](cf::composition::PadSettings& s) {
      s.quantizeDivision = static_cast<std::uint8_t>(q.division);
      s.quantizeTriplet = q.triplet;
    });
  };
  merge_.onChange = [this] {
    if (!updating_) perform_.editPadSettings([&](cf::composition::PadSettings& s) { s.merge = static_cast<std::uint8_t>(merge_.getSelectedId() - 1); });
  };
  record_.onClick = [this] {
    perform_.setRecordArmed(record_.getToggleState());
    tick();
  };
  discard_.onClick = [this] {
    perform_.discardTake();
    tick();
  };
  learn_.onClick = [this] {
    model_.setStatus(learn_.getToggleState() ? "Press a key on your MIDI controller: it becomes the first pad." : "MIDI learn off.");
  };
  tick();
}

PadsPanel::~PadsPanel() { perform_.padAllNotesOff(); }

void PadsPanel::visibilityChanged() {
  if (!isVisible()) perform_.padAllNotesOff();  // closing the panel releases every pad
  DrawerPanel::visibilityChanged();
}

void PadsPanel::applyLearn(const std::array<std::uint32_t, 4>& activity) {
  int note = -1;
  for (int n = 0; n < 128 && note < 0; ++n)
    if (activity[static_cast<std::size_t>(n >> 5)] & (1u << (n & 31))) note = n;
  if (note < 0) return;
  lastNote_ = note;
  midiIn_.setText("MIDI in: " + juce::String::fromUTF8(cf::ui::padNoteName(note).c_str()) + " (" + juce::String(note) + ")", juce::dontSendNotification);
  if (!learn_.getToggleState()) return;
  // An on-screen press is also activity; a pad that is held right now is not a controller key.
  for (int i = 0; i < cf::ui::kPadsPerPage; ++i)
    if (grid_.pad(i).held() && grid_.pad(i).cell().note == note) return;
  const int base = std::min(note, 111);
  perform_.editPadSettings([&](cf::composition::PadSettings& s) { s.baseNote = static_cast<std::uint8_t>(base); });
  learn_.setToggleState(false, juce::dontSendNotification);
  model_.setStatus("Pad 1 now plays " + juce::String::fromUTF8(cf::ui::padNoteName(base).c_str()) + " (" + juce::String(base) + ").");
}

void PadsPanel::tick() {
  const auto activity = perform_.takePadActivity();
  applyLearn(activity);
  for (int n = 0; n < 128; ++n)
    if (activity[static_cast<std::size_t>(n >> 5)] & (1u << (n & 31))) hold_[static_cast<std::size_t>(n)] = 6;  // about 200 ms at 30 Hz
  std::array<std::uint32_t, 4> lit{};
  for (int n = 0; n < 128; ++n)
    if (hold_[static_cast<std::size_t>(n)] > 0) {
      lit[static_cast<std::size_t>(n >> 5)] |= 1u << (n & 31);
      --hold_[static_cast<std::size_t>(n)];
    }
  view_ = perform_.padView(page_, lit);
  page_ = view_.page;
  updating_ = true;
  grid_.set(view_);
  if (view_.showPage) {
    if (page_c_.getNumItems() != static_cast<int>(view_.pageItems.size())) {
      page_c_.clear(juce::dontSendNotification);
      int id = 1;
      for (const auto& item : view_.pageItems) page_c_.addItem(juce::String::fromUTF8(item.c_str()), id++);
    }
    page_c_.setSelectedId(view_.page + 1, juce::dontSendNotification);
  }
  const bool pageShown = view_.showPage;
  if (page_c_.isVisible() != pageShown) {
    page_c_.setVisible(pageShown);
    pageLabel_.setVisible(pageShown);
    relayout();
  }
  const auto settings = perform_.padSettings();
  if (!baseNote_.isMouseButtonDown()) baseNote_.setValue(settings.baseNote, juce::dontSendNotification);
  channel_.setSelectedId(settings.midiChannel + 1, juce::dontSendNotification);
  quantize_.setSelectedId(cf::ui::padQuantizeIndex(settings.quantizeDivision, settings.quantizeTriplet) + 1, juce::dontSendNotification);
  merge_.setSelectedId(settings.merge + 1, juce::dontSendNotification);
  for (auto* c : std::initializer_list<juce::Component*>{&mode_, &curve_, &baseNote_, &channel_, &quantize_, &merge_, &learn_}) c->setEnabled(view_.settingsEnabled);
  record_.setEnabled(view_.recordEnabled);
  record_.setToggleState(view_.recordOn, juce::dontSendNotification);
  discard_.setEnabled(view_.discardEnabled);
  discard_.setButtonText(juce::String::fromUTF8(view_.discardLabel.c_str()));
  hint_.setText(juce::String::fromUTF8((view_.recordHint.empty() ? view_.emptyText : view_.recordHint).c_str()), juce::dontSendNotification);
  if (lastNote_ < 0) midiIn_.setText("MIDI in: none yet", juce::dontSendNotification);
  updating_ = false;
}

int PadsPanel::layoutBody(int width) {
  beginLayout();
  Stack s(width);
  const int lw = Stack::scaled(92);
  const int gridSize = std::min(width, Stack::scaled(340));
  grid_.setBounds(s.row(gridSize).withWidth(gridSize));
  if (page_c_.isVisible()) labelled(s.row(), pageLabel_, page_c_, lw);
  labelled(s.row(), modeLabel_, mode_, lw);
  labelled(s.row(), curveLabel_, curve_, lw);
  labelled(s.row(), baseLabel_, baseNote_, lw);
  labelled(s.row(), channelLabel_, channel_, lw);
  {
    auto r = s.row();
    learn_.setBounds(r.removeFromRight(Stack::scaled(90)));
    midiIn_.setBounds(r);
  }
  heading(s, "Record");
  perform_ui::grid(s, {&record_, &discard_}, 2);
  labelled(s.row(), quantizeLabel_, quantize_, lw);
  labelled(s.row(), mergeLabel_, merge_, lw);
  hint_.setBounds(s.row(Stack::scaled(44)));
  body_.repaint();
  return s.height();
}
