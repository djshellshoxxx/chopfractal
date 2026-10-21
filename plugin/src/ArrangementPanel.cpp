#include "ArrangementPanel.h"

#include <algorithm>

namespace cf = chopfractal;
using namespace perform_ui;

// ---------------------------------------------------------------------------------------------
// SectionList
// ---------------------------------------------------------------------------------------------

SectionList::SectionList() {
  setComponentID("canvas-sections");
  setWantsKeyboardFocus(true);
}

void SectionList::set(std::vector<juce::String> rows, int selected, const juce::String& emptyText) {
  if (rows == rows_ && selected == selected_ && emptyText == empty_) return;
  rows_ = std::move(rows);
  selected_ = selected;
  empty_ = emptyText;
  repaint();
}

int SectionList::heightFor() const { return Stack::scaled(24) * std::max(3, std::min(count(), 12)) + Stack::scaled(4); }

juce::Rectangle<int> SectionList::rowBounds(int i) const { return {0, i * Stack::scaled(24), getWidth(), Stack::scaled(24)}; }

void SectionList::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  g.setColour(p.bg);
  g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.f);
  g.setColour(hasKeyboardFocus(false) ? p.rain : p.edge);
  g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.f, 1.f);
  g.setFont(cdl::font(cdl::FontRole::Body, cdl::uiScale()));
  if (rows_.empty()) {
    g.setColour(p.inkMuted);
    g.drawFittedText(empty_, getLocalBounds().reduced(8), juce::Justification::topLeft, 3);
    return;
  }
  for (int i = 0; i < count(); ++i) {
    auto r = rowBounds(i);
    if (i == selected_) {
      g.setColour(p.sprayWash);
      g.fillRect(r.reduced(1));
      g.setColour(p.spray);
      g.fillRect(r.withWidth(3).reduced(0, 2));  // the bar at the left edge marks the selected section besides its tint
    }
    g.setColour(i == selected_ ? p.ink : p.inkMuted);
    g.drawText(rows_[static_cast<std::size_t>(i)], r.reduced(Stack::scaled(8), 0), juce::Justification::centredLeft, true);
  }
}

void SectionList::mouseDown(const juce::MouseEvent& e) {
  if (isShowing()) grabKeyboardFocus();
  for (int i = 0; i < count(); ++i)
    if (rowBounds(i).contains(e.getPosition())) {
      if (onSelect) onSelect(i);
      return;
    }
}

bool SectionList::keyPressed(const juce::KeyPress& k) {
  if (count() == 0) return false;
  if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) {
    if (onDelete) onDelete();
    return true;
  }
  const bool alt = k.getModifiers().isAltDown();
  if (k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey) {
    const int d = k.getKeyCode() == juce::KeyPress::upKey ? -1 : 1;
    if (alt) {
      if (onMove) onMove(d);
    } else if (onSelect) {
      onSelect(std::max(0, std::min(count() - 1, selected_ + d)));
    }
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------------------------
// ArrangementPanel
// ---------------------------------------------------------------------------------------------

ArrangementPanel::ArrangementPanel(EditorModel& m, EditorCommands& c, PerformCommands& perform) : DrawerPanel(m, c, "ArrangementPanel", 20), perform_(perform) {
  styleLabel(total_, "", false);
  styleLabel(status_, "");
  styleLabel(notes_, "");
  styleLabel(sceneLabel_, "Scene");
  styleLabel(barsLabel_, "Bars");
  styleLabel(repeatsLabel_, "Repeats");
  styleLabel(transitionLabel_, "Transition");
  styleLabel(nameLabel_, "File name");
  status_.setJustificationType(juce::Justification::topLeft);
  notes_.setJustificationType(juce::Justification::topLeft);
  repeats_.setSliderStyle(juce::Slider::LinearHorizontal);
  repeats_.setTextBoxStyle(juce::Slider::TextBoxRight, false, static_cast<int>(40 * cdl::uiScale()), 22);
  repeats_.setRange(1, cf::composition::kMaxSectionRepeats, 1);
  transition_.addItemList({"Cut", "Crossfade 10 ms", "Crossfade 50 ms"}, 1);
  transition_.setSelectedId(2, juce::dontSendNotification);
  name_.setMultiLine(false);
  name_.setTextToShowWhenEmpty("Project name", cdl::pal().inkMuted);
  progressBar_.setPercentageDisplay(false);

  describe(list_, "Arrangement sections", "Up and Down select a section, Delete removes it, Alt with Up or Down moves it");
  describe(scene_, "Section scene", "The scene the selected section plays");
  describe(bars_, "Section bars", "How many bars of the scene the selected section plays");
  describe(repeats_, "Section repeats", "How many times the selected section repeats");
  describe(add_, "Add Section", "Add a section that plays a scene");
  describe(remove_, "Remove Section", "Remove the selected section");
  describe(up_, "Section Up", "Move the selected section earlier");
  describe(down_, "Section Down", "Move the selected section later");
  describe(chain_, "Use scene chain", "Fill the sections from the scene order, with each scene's loops as repeats");
  describe(autoFill_, "Auto fill 16 bars", "Spread the scenes over 16 bars, about four bars each");
  describe(fade_, "Fade out last bar", "Fade the last bar to silence instead of adding a tail");
  describe(tail_, "Add tail", "Add the two second decay of the last hit");
  describe(transition_, "Section transition", "How one section joins the next");
  describe(midi_, "Write MIDI", "Also write a MIDI file of the arrangement");
  describe(markers_, "Write markers", "Also write the cue list and the markers in the WAV and MIDI files");
  describe(name_, "Arrangement name", "The file name, or the project name when empty");
  describe(export_, "Export Arrangement", "Render the arrangement to WAV, MIDI and markers");
  describe(cancel_, "Cancel Arrangement", "Stop the export; no files are left behind");
  describe(progressBar_, "Export progress", "How much of the export is done");
  describe(total_, "Arrangement total", "The length of the arrangement");
  describe(status_, "Arrangement status", "What the export is doing or why it cannot start");
  describe(notes_, "Arrangement notes", "Sections that use only part of their scene");

  add({&total_, &list_, &sceneLabel_, &scene_, &barsLabel_, &bars_, &repeatsLabel_, &repeats_, &add_, &remove_, &up_, &down_, &chain_, &autoFill_, &fade_, &tail_,
       &transitionLabel_, &transition_, &midi_, &markers_, &notes_, &nameLabel_, &name_, &progressBar_, &status_, &export_, &cancel_});

  list_.onSelect = [this](int i) { select(i); };
  list_.onDelete = [this] { if (remove_.isEnabled()) remove_.triggerClick(); };
  list_.onMove = [this](int d) {
    auto& b = d < 0 ? up_ : down_;
    if (b.isEnabled()) b.triggerClick();
  };
  scene_.onChange = [this] {
    if (updating_ || selected_ < 0) return;
    const int i = scene_.getSelectedId() - 1;
    if (i < 0 || i >= static_cast<int>(state_.scenes.size())) return;
    const auto id = state_.scenes[static_cast<std::size_t>(i)].first;
    const int row = selected_;
    perform_.editArrangement([=](cf::composition::Arrangement& a) {
      if (row < static_cast<int>(a.sections.size())) a.sections[static_cast<std::size_t>(row)].sceneId = id;
    });
    tick();
  };
  bars_.onChange = [this] {
    const int i = bars_.getSelectedId() - 1;
    if (updating_ || selected_ < 0 || i < 0 || i >= static_cast<int>(barChoices_.size())) return;
    const int row = selected_, b = barChoices_[static_cast<std::size_t>(i)];
    perform_.editArrangement([=](cf::composition::Arrangement& a) {
      if (row < static_cast<int>(a.sections.size())) a.sections[static_cast<std::size_t>(row)].bars = static_cast<std::uint8_t>(b);
    });
    tick();
  };
  repeats_.onValueChange = [this] {
    if (updating_ || selected_ < 0 || repeats_.isMouseButtonDown()) return;
    const int row = selected_, n = static_cast<int>(repeats_.getValue());
    perform_.editArrangement([=](cf::composition::Arrangement& a) {
      if (row < static_cast<int>(a.sections.size())) a.sections[static_cast<std::size_t>(row)].repeats = static_cast<std::uint8_t>(n);
    });
    tick();
  };
  repeats_.onDragEnd = [this] {
    if (updating_ || selected_ < 0) return;
    const int row = selected_, n = static_cast<int>(repeats_.getValue());
    perform_.editArrangement([=](cf::composition::Arrangement& a) {
      if (row < static_cast<int>(a.sections.size())) a.sections[static_cast<std::size_t>(row)].repeats = static_cast<std::uint8_t>(n);
    });
    tick();
  };
  add_.onClick = [this] {
    if (state_.scenes.empty()) return;
    // The new section plays the scene of the selected row, else the first scene.
    std::uint32_t id = state_.scenes.front().first;
    int patternBars = state_.scenes.front().second;
    if (selected_ >= 0 && selected_ < static_cast<int>(state_.arrangement.sections.size())) {
      id = state_.arrangement.sections[static_cast<std::size_t>(selected_)].sceneId;
      for (const auto& s : state_.scenes)
        if (s.first == id) patternBars = s.second;
    }
    perform_.editArrangement([=](cf::composition::Arrangement& a) {
      if (a.sections.size() < cf::composition::kMaxArrangementSections)
        a.sections.push_back({id, static_cast<std::uint8_t>(cf::composition::defaultBars(patternBars)), 1});
    });
    selected_ = static_cast<int>(perform_.arrangementState().arrangement.sections.size()) - 1;
    tick();
  };
  remove_.onClick = [this] {
    const int row = selected_;
    if (row < 0) return;
    perform_.editArrangement([=](cf::composition::Arrangement& a) {
      if (row < static_cast<int>(a.sections.size())) a.sections.erase(a.sections.begin() + row);
    });
    selected_ = std::min(row, static_cast<int>(perform_.arrangementState().arrangement.sections.size()) - 1);
    tick();
  };
  auto move = [this](int d) {
    const int row = selected_;
    perform_.editArrangement([=](cf::composition::Arrangement& a) {
      const int to = row + d;
      if (row >= 0 && to >= 0 && row < static_cast<int>(a.sections.size()) && to < static_cast<int>(a.sections.size())) std::swap(a.sections[static_cast<std::size_t>(row)], a.sections[static_cast<std::size_t>(to)]);
    });
    selected_ = std::max(0, std::min(row + d, static_cast<int>(perform_.arrangementState().arrangement.sections.size()) - 1));
    tick();
  };
  up_.onClick = [move] { move(-1); };
  down_.onClick = [move] { move(1); };
  chain_.onClick = [this] { perform_.fillArrangementFromChain(); selected_ = 0; tick(); };
  autoFill_.onClick = [this] { perform_.autoFillArrangement(); selected_ = 0; tick(); };
  fade_.onClick = [this] { perform_.editArrangement([on = fade_.getToggleState()](cf::composition::Arrangement& a) { a.fadeOut = on; }); tick(); };
  tail_.onClick = [this] { perform_.editArrangement([on = tail_.getToggleState()](cf::composition::Arrangement& a) { a.tail = on; }); tick(); };
  midi_.onClick = [this] { perform_.editArrangement([on = midi_.getToggleState()](cf::composition::Arrangement& a) { a.writeMidi = on; }); tick(); };
  markers_.onClick = [this] { perform_.editArrangement([on = markers_.getToggleState()](cf::composition::Arrangement& a) { a.writeMarkers = on; }); tick(); };
  transition_.onChange = [this] {
    if (updating_) return;
    perform_.editArrangement([t = static_cast<cf::composition::Transition>(transition_.getSelectedId() - 1)](cf::composition::Arrangement& a) { a.transition = t; });
    tick();
  };
  export_.onClick = [this] { exportNow(); };
  name_.onReturnKey = [this] { if (export_.isEnabled()) exportNow(); };
  cancel_.onClick = [this] { perform_.cancelArrangementExport(); tick(); };
  tick();
}

void ArrangementPanel::exportNow() {
  if (!view_.exportEnabled) return;
  std::string n = name_.getText().toStdString();
  if (n.empty()) n = perform_.projectName();
  perform_.startArrangementExport(n);
  tick();
}

void ArrangementPanel::tick() {
  state_ = perform_.arrangementState();
  const int rows = static_cast<int>(state_.rows.size());
  if (rows == 0) selected_ = -1;
  else selected_ = std::max(0, std::min(selected_, rows - 1));
  view_ = perform_.arrangementView(state_, selected_);
  const auto& v = view_;
  updating_ = true;
  std::vector<juce::String> texts;
  for (const auto& r : v.rows) texts.push_back(juce::String::fromUTF8(r.c_str()));
  const int before = list_.heightFor();
  list_.set(std::move(texts), selected_, "No sections yet. Use scene chain or Auto fill 16 bars, or Add Section.");
  total_.setText(juce::String::fromUTF8(v.totalText.c_str()), juce::dontSendNotification);
  total_.setColour(juce::Label::textColourId, v.valid ? cdl::pal().ink : cdl::pal().warn);
  status_.setText(juce::String::fromUTF8(v.statusText.c_str()), juce::dontSendNotification);
  juce::String notes;
  for (const auto& n : v.notes) notes << juce::String::fromUTF8(n.c_str()) << "\n";
  notes_.setText(notes.trimEnd(), juce::dontSendNotification);

  // Editors of the selected section.
  scene_.clear(juce::dontSendNotification);
  for (std::size_t i = 0; i < state_.scenes.size(); ++i) scene_.addItem("Scene " + juce::String(static_cast<int>(i) + 1), static_cast<int>(i) + 1);
  bars_.clear(juce::dontSendNotification);
  barChoices_.clear();
  const bool have = selected_ >= 0 && selected_ < static_cast<int>(state_.arrangement.sections.size());
  if (have) {
    const auto& sec = state_.arrangement.sections[static_cast<std::size_t>(selected_)];
    const auto& row = state_.rows[static_cast<std::size_t>(selected_)];
    for (std::size_t i = 0; i < state_.scenes.size(); ++i)
      if (state_.scenes[i].first == sec.sceneId) scene_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
    barChoices_ = cf::ui::arrangementBarChoices(row.patternBars);
    for (std::size_t i = 0; i < barChoices_.size(); ++i) {
      bars_.addItem(juce::String(barChoices_[i]) + (barChoices_[i] == 1 ? " bar" : " bars"), static_cast<int>(i) + 1);
      if (barChoices_[i] == sec.bars) bars_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
    }
    if (!repeats_.isMouseButtonDown()) repeats_.setValue(sec.repeats, juce::dontSendNotification);
  }
  fade_.setToggleState(state_.arrangement.fadeOut, juce::dontSendNotification);
  tail_.setToggleState(state_.arrangement.tail, juce::dontSendNotification);
  midi_.setToggleState(state_.arrangement.writeMidi, juce::dontSendNotification);
  markers_.setToggleState(state_.arrangement.writeMarkers, juce::dontSendNotification);
  transition_.setSelectedId(static_cast<int>(state_.arrangement.transition) + 1, juce::dontSendNotification);
  updating_ = false;

  for (auto* c : std::initializer_list<juce::Component*>{&scene_, &bars_, &repeats_, &remove_}) c->setEnabled(have && v.editable);
  add_.setEnabled(v.addEnabled && !state_.scenes.empty());
  up_.setEnabled(v.moveUpEnabled);
  down_.setEnabled(v.moveDownEnabled);
  chain_.setEnabled(v.useChainEnabled);
  autoFill_.setEnabled(v.autoFillEnabled);
  for (auto* c : std::initializer_list<juce::Component*>{&fade_, &tail_, &midi_, &markers_, &transition_, &name_}) c->setEnabled(v.editable);
  export_.setEnabled(v.exportEnabled);
  export_.setTooltip(v.exportEnabled ? juce::String("Render the arrangement to WAV, MIDI and markers") : juce::String::fromUTF8(v.exportTooltip.c_str()));
  cancel_.setVisible(state_.running);
  export_.setVisible(!state_.running);
  progressBar_.setVisible(state_.running);
  progress_ = v.progress;
  if (state_.running) progressBar_.repaint();
  if (list_.heightFor() != before) relayout();
}

int ArrangementPanel::layoutBody(int width) {
  beginLayout();
  Stack s(width);
  const int lw = Stack::scaled(86);
  heading(s, "Arrangement");
  total_.setBounds(s.row());
  list_.setBounds(s.row(list_.heightFor()));
  labelled(s.row(), sceneLabel_, scene_, lw);
  labelled(s.row(), barsLabel_, bars_, lw);
  labelled(s.row(), repeatsLabel_, repeats_, lw);
  grid(s, {&add_, &remove_, &up_, &down_, &chain_, &autoFill_}, 2);
  {
    const int lines = notes_.getText().isEmpty() ? 0 : 1 + static_cast<int>(notes_.getText().retainCharacters("\n").length());
    notes_.setBounds(s.row(lines == 0 ? Stack::scaled(2) : Stack::scaled(20) * lines));
  }
  heading(s, "Options");
  grid(s, {&fade_, &tail_, &midi_, &markers_}, 2);
  labelled(s.row(), transitionLabel_, transition_, lw);
  heading(s, "Export");
  labelled(s.row(), nameLabel_, name_, lw);
  progressBar_.setBounds(s.row().reduced(0, Stack::scaled(6)));
  status_.setBounds(s.row(Stack::scaled(34)));
  {
    auto r = s.row();
    export_.setBounds(r);
    cancel_.setBounds(r);
  }
  body_.repaint();
  return s.height();
}
