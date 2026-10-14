#include "PadPanel.h"

#include <initializer_list>

#include <chopfractal/plugin_host_adapter/parameters.hpp>

#include "PluginProcessor.h"

namespace cf = chopfractal;

// ---------------------------------------------------------------------------------------------
// PadButton
// ---------------------------------------------------------------------------------------------

PadButton::PadButton(ChopFractalProcessor& processor, int index) : juce::Button("Pad " + juce::String(index + 1)), proc_(processor), index_(index) {
  setTitle("Pad " + juce::String(index + 1));
  setTriggeredOnMouseDown(false);
}

void PadButton::setNote(int note, const juce::String& caption, bool available) {
  note_ = available ? note : -1;
  caption_ = caption;
  setEnabled(available);
  setTitle(available ? "Pad " + juce::String(index_ + 1) + ": " + caption : "Pad " + juce::String(index_ + 1));
  repaint();
}

void PadButton::mouseDown(const juce::MouseEvent& e) {
  if (e.mods.isPopupMenu()) {
    if (onContext) onContext(*this);
    return;
  }
  velocity_ = juce::jlimit(1, 127, 127 - juce::roundToInt(87.0 * e.y / juce::jmax(1, getHeight())));
  juce::Button::mouseDown(e);
}

void PadButton::buttonStateChanged() {
  const bool down = getState() == juce::Button::buttonDown;
  if (note_ < 0) return;
  if (down && !sounding_) {
    proc_.padTrigger(note_, velocity_, true);
    sounding_ = true;
    light();
  } else if (!down && sounding_) {
    proc_.padTrigger(note_, 0, false);
    sounding_ = false;
  }
}

void PadButton::paintButton(juce::Graphics& g, bool over, bool down) {
  const auto b = getLocalBounds().toFloat().reduced(2.f);
  const bool on = down || isLit();
  g.setColour(!isEnabled() ? juce::Colour(0xff1c1f24) : (on ? juce::Colour(0xff6fb1ff) : (over ? juce::Colour(0xff3a4150) : juce::Colour(0xff2b303a))));
  g.fillRoundedRectangle(b, 4.f);
  g.setColour(juce::Colours::white.withAlpha(isEnabled() ? 0.9f : 0.3f));
  g.drawRoundedRectangle(b, 4.f, on ? 2.f : 1.f);
  g.setFont(juce::FontOptions(11.f));
  g.drawFittedText(caption_, getLocalBounds().reduced(4), juce::Justification::centred, 3);
  if (on) g.drawText("Playing", getLocalBounds().reduced(4), juce::Justification::topRight);  // text, not colour only
}

// ---------------------------------------------------------------------------------------------
// PadPanel
// ---------------------------------------------------------------------------------------------

juce::String PadPanel::noteName(int note) {
  static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return juce::String(names[note % 12]) + juce::String(note / 12 - 2);
}

PadPanel::PadPanel(ChopFractalProcessor& processor) : proc_(processor) {
  setName("PadPanel");
  setWantsKeyboardFocus(true);
  for (int i = 0; i < 16; ++i) {
    pads_.push_back(std::make_unique<PadButton>(proc_, i));
    PadButton& pad = *pads_.back();
    pad.onContext = [this](PadButton& p) { showChokeMenu(p); };
    addAndMakeVisible(pad);
  }
  mode_.addItemList({"Poly", "Mono", "Gate"}, 1);
  curve_.addItemList({"Linear", "Soft", "Hard", "Fixed"}, 1);
  mode_.setTitle("Pad mode");
  curve_.setTitle("Velocity curve");
  modeAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.apvts, "perf_mode", mode_);
  curveAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.apvts, "perf_velocity_curve", curve_);
  page_.setTitle("Pad page");
  page_.onChange = [this] { refreshPads(); };
  channel_.setTitle("MIDI channel");
  channel_.addItem("Omni", 1);
  for (int c = 1; c <= 16; ++c) channel_.addItem(juce::String(c), c + 1);
  channel_.setSelectedId(1, juce::dontSendNotification);
  quantize_.setTitle("Quantize");
  quantize_.addItemList({"Off", "1/4", "1/8", "1/16", "1/32", "1/16T"}, 1);
  quantize_.setSelectedId(4, juce::dontSendNotification);
  merge_.setTitle("Merge");
  merge_.addItemList({"Add to pattern", "Replace slots"}, 1);
  merge_.setSelectedId(1, juce::dontSendNotification);
  baseNote_.setTitle("Base note");
  baseNote_.setRange(0, 111, 1);
  baseNote_.setValue(36, juce::dontSendNotification);
  baseNote_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
  baseNote_.textFromValueFunction = [](double v) { return noteName(static_cast<int>(v)) + " (" + juce::String(static_cast<int>(v)) + ")"; };
  record_.setTitle("Record pad hits into the pattern");
  discard_.setTitle("Discard Take");
  close_.setTitle("Close Pads");
  hits_.setText("No hits waiting", juce::dontSendNotification);
  empty_.setText("Load a source and detect chops to play the pads.", juce::dontSendNotification);
  for (juce::Component* c : std::initializer_list<juce::Component*>{&page_, &mode_, &curve_, &channel_, &quantize_, &merge_, &baseNote_, &record_, &discard_, &close_, &hits_, &empty_})
    addAndMakeVisible(*c);
  for (juce::ComboBox* c : {&channel_, &quantize_, &merge_}) c->onChange = [this] { settingsChanged(); };
  baseNote_.onValueChange = [this] { settingsChanged(); };
  record_.onClick = [this] { proc_.setRecordArmed(record_.getToggleState()); };
  discard_.onClick = [this] {
    proc_.discardTake();
    proc_.setStatusMessage("Take discarded.");
  };
  close_.onClick = [this] { close(); };
  setVisible(false);
}

PadPanel::~PadPanel() { stopTimer(); }

void PadPanel::open() {
  loading_ = true;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    const auto& ps = s.padSettings();
    baseNote_.setValue(ps.baseNote, juce::dontSendNotification);
    channel_.setSelectedId(ps.midiChannel + 1, juce::dontSendNotification);
    const int q = ps.quantizeDivision == 0 ? 1 : ps.quantizeTriplet ? 6 : ps.quantizeDivision == 4 ? 2 : ps.quantizeDivision == 8 ? 3 : ps.quantizeDivision == 16 ? 4 : 5;
    quantize_.setSelectedId(q, juce::dontSendNotification);
    merge_.setSelectedId(ps.merge + 1, juce::dontSendNotification);
  });
  loading_ = false;
  refreshPads();
  setVisible(true);
  toFront(true);
  grabKeyboardFocus();
  startTimerHz(20);
}

void PadPanel::close() {
  setVisible(false);
  stopTimer();
  for (auto& p : pads_) p->setState(juce::Button::buttonNormal);  // no stuck notes
}

bool PadPanel::keyPressed(const juce::KeyPress& key) {
  if (key == juce::KeyPress::escapeKey) {
    close();
    return true;
  }
  return false;
}

void PadPanel::refreshPads() {
  std::vector<juce::String> roles;
  int baseNote = 36;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    baseNote = s.padSettings().baseNote;
    chopCount_ = s.chops() ? static_cast<int>(s.chops()->chops.size()) : 0;
    for (int i = 0; i < chopCount_; ++i) roles.push_back(s.roleMap().roleOf(s.chops()->chops[static_cast<std::size_t>(i)].id));
  });
  const int pages = juce::jmax(1, (chopCount_ + 15) / 16);
  const int selected = juce::jmax(0, page_.getSelectedId() - 1);
  if (page_.getNumItems() != pages) {
    page_.clear(juce::dontSendNotification);
    for (int p = 0; p < pages; ++p) page_.addItem("Chops " + juce::String(p * 16 + 1) + "-" + juce::String(juce::jmin(chopCount_, p * 16 + 16)), p + 1);
  }
  const int pageIndex = juce::jmin(selected, pages - 1);
  page_.setSelectedId(pageIndex + 1, juce::dontSendNotification);
  page_.setVisible(pages > 1);
  empty_.setVisible(chopCount_ == 0);
  for (int i = 0; i < 16; ++i) {
    const int chop = pageIndex * 16 + i;
    const int note = baseNote + chop;
    const bool available = chop < chopCount_ && note <= 127;
    juce::String caption;
    if (available) caption = "Chop " + juce::String(chop + 1) + "\n" + (roles[static_cast<std::size_t>(chop)].isEmpty() ? juce::String("no role") : roles[static_cast<std::size_t>(chop)]) + "\n" + noteName(note);
    pads_[static_cast<std::size_t>(i)]->setNote(note, caption, available);
    if (chop < chopCount_ && note > 127) pads_[static_cast<std::size_t>(i)]->setTooltip("No MIDI note available for this chop; lower the base note.");
  }
}

void PadPanel::settingsChanged() {
  if (loading_) return;
  cf::composition::PadSettings ps;
  proc_.withSession([&](cf::composition::ProjectSession& s) { ps = s.padSettings(); });
  ps.baseNote = static_cast<std::uint8_t>(juce::jlimit(0, 111, juce::roundToInt(baseNote_.getValue())));
  ps.midiChannel = static_cast<std::uint8_t>(juce::jmax(0, channel_.getSelectedId() - 1));
  switch (quantize_.getSelectedId()) {
    case 1: ps.quantizeDivision = 0; ps.quantizeTriplet = false; break;
    case 2: ps.quantizeDivision = 4; ps.quantizeTriplet = false; break;
    case 3: ps.quantizeDivision = 8; ps.quantizeTriplet = false; break;
    case 5: ps.quantizeDivision = 32; ps.quantizeTriplet = false; break;
    case 6: ps.quantizeDivision = 16; ps.quantizeTriplet = true; break;
    default: ps.quantizeDivision = 16; ps.quantizeTriplet = false; break;
  }
  ps.merge = static_cast<std::uint8_t>(juce::jmax(0, merge_.getSelectedId() - 1));
  cf::Status st;
  proc_.withSession([&](cf::composition::ProjectSession& s) { st = s.setPadSettings(ps); });
  if (!st.ok()) proc_.setStatusMessage(st.error().message);
  refreshPads();
}

void PadPanel::showChokeMenu(PadButton& pad) {
  if (pad.note() < 0) return;
  juce::PopupMenu menu;
  menu.addItem(1, "Choke group: Off");
  for (int g = 1; g <= 4; ++g) menu.addItem(g + 1, "Choke group " + juce::String(g));
  const int note = pad.note();
  juce::WeakReference<ChopFractalProcessor> proc(&proc_);
  menu.showMenuAsync(juce::PopupMenu::Options(), [proc, note](int result) {
    if (proc == nullptr || result == 0) return;
    proc->withSession([&](cf::composition::ProjectSession& s) {
      const int chop = note - s.padSettings().baseNote;
      if (s.chops() && chop >= 0 && chop < static_cast<int>(s.chops()->chops.size())) s.setChokeGroup(s.chops()->chops[static_cast<std::size_t>(chop)].id, static_cast<std::uint8_t>(result - 1));
    });
  });
}

void PadPanel::timerCallback() {
  for (int word = 0; word < 4; ++word) {
    const std::uint32_t bits = proc_.pads().activity(word);
    if (!bits) continue;
    for (auto& p : pads_)
      if (p->note() >= 0 && (p->note() >> 5) == word && (bits & (1u << (p->note() & 31)))) p->light();
  }
  for (auto& p : pads_)
    if (p->decayLight()) p->repaint();
  const int waiting = proc_.pads().waitingHits();
  const juce::String text = waiting == 0 ? "No hits waiting" : juce::String(waiting) + " hits waiting";
  if (hits_.getText() != text) hits_.setText(text, juce::dontSendNotification);
  discard_.setEnabled(waiting > 0);
  if (record_.getToggleState() != proc_.pads().recordArmed()) record_.setToggleState(proc_.pads().recordArmed(), juce::dontSendNotification);
  bool havePattern = false;
  int count = 0;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    havePattern = s.pattern() != nullptr;
    count = s.chops() ? static_cast<int>(s.chops()->chops.size()) : 0;
  });
  record_.setEnabled(havePattern);
  if (count != chopCount_) refreshPads();
}

void PadPanel::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(0xf015181d));
  g.setColour(juce::Colours::white.withAlpha(0.25f));
  g.drawRect(getLocalBounds());
}

void PadPanel::resized() {
  auto r = getLocalBounds().reduced(8);
  empty_.setBounds(r.removeFromTop(22));
  auto controls = r.removeFromBottom(30);
  auto left = r.removeFromLeft(juce::jmin(r.getWidth() / 2, r.getHeight() * 5 / 4));
  const int cw = left.getWidth() / 4, ch = left.getHeight() / 4;
  for (int i = 0; i < 16; ++i) pads_[static_cast<std::size_t>(i)]->setBounds(left.getX() + (i % 4) * cw, left.getY() + (i / 4) * ch, cw, ch);
  r.removeFromLeft(10);
  auto row = [&](juce::Component& c) { c.setBounds(r.removeFromTop(28).removeFromLeft(240)); };
  row(page_);
  row(mode_);
  row(curve_);
  row(baseNote_);
  row(channel_);
  row(quantize_);
  row(merge_);
  record_.setBounds(controls.removeFromLeft(90));
  discard_.setBounds(controls.removeFromLeft(110).reduced(2));
  hits_.setBounds(controls.removeFromLeft(140));
  close_.setBounds(controls.removeFromRight(110).reduced(2));
}
