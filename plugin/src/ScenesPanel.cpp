#include "ScenesPanel.h"

#include <cmath>

namespace cf = chopfractal;
using namespace perform_ui;

// ---------------------------------------------------------------------------------------------
// SceneStrip
// ---------------------------------------------------------------------------------------------

SceneStrip::SceneStrip() {
  setComponentID("canvas-scenes");
  setWantsKeyboardFocus(true);
}

void SceneStrip::set(std::vector<Tile> tiles, int selected, const juce::String& emptyText) {
  tiles_ = std::move(tiles);
  selected_ = selected;
  emptyText_ = emptyText;
  repaint();
}

int SceneStrip::heightFor(int width) const {
  const int tile = std::max(24, (width - 7 * Stack::scaled(4)) / columns());
  const int rows = std::max(1, (static_cast<int>(tiles_.size()) + columns() - 1) / columns());
  return rows * (tile + Stack::scaled(4)) - Stack::scaled(4);
}

juce::Rectangle<int> SceneStrip::tileBounds(int i) const {
  const int gap = Stack::scaled(4);
  const int tile = std::max(24, (getWidth() - (columns() - 1) * gap) / columns());
  const int col = i % columns(), row = i / columns();
  return {col * (tile + gap), row * (tile + gap), tile, tile};
}

void SceneStrip::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  const float s = cdl::uiScale();
  if (tiles_.empty()) {
    g.setColour(p.bg);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.f);
    g.setColour(p.inkMuted);
    g.setFont(cdl::font(cdl::FontRole::Hint, s));
    g.drawFittedText(emptyText_, getLocalBounds().reduced(8), juce::Justification::centred, 3);
    return;
  }
  for (int i = 0; i < count(); ++i) {
    const auto& t = tiles_[static_cast<std::size_t>(i)];
    const auto r = tileBounds(i).toFloat();
    g.setColour(t.playing ? p.sprayWash : p.knobBottom);
    g.fillRoundedRectangle(r, 3.f);
    g.setColour(i == selected_ ? p.ink : (t.playing ? p.spray : p.edgeHi));
    g.drawRoundedRectangle(r.reduced(0.5f), 3.f, i == selected_ || t.playing ? 2.f : 1.f);
    if (t.armed) {  // "next up" marker: a small triangle in the corner, not colour alone
      juce::Path tri;
      tri.addTriangle(r.getRight() - 10.f * s, r.getY() + 2.f, r.getRight() - 2.f, r.getY() + 2.f, r.getRight() - 2.f, r.getY() + 10.f * s);
      g.setColour(p.spray);
      g.fillPath(tri);
    }
    g.setColour(t.fits ? p.ink : p.warn);
    g.setFont(cdl::font(cdl::FontRole::ValueLg, s));
    g.drawText(juce::String(t.number), r.toNearestInt(), juce::Justification::centred, false);
    g.setColour(p.inkMuted);
    g.setFont(cdl::font(cdl::FontRole::MonoXs, s));
    g.drawText("x" + juce::String(t.loops), r.toNearestInt().reduced(3, 2), juce::Justification::bottomRight, false);
    if (!t.fits) g.drawText("!", r.toNearestInt().reduced(3, 2), juce::Justification::topLeft, false);
  }
  if (hasKeyboardFocus(false)) {
    g.setColour(p.rain);
    g.drawRect(getLocalBounds(), 1);
  }
}

void SceneStrip::mouseDown(const juce::MouseEvent& e) {
  if (isShowing()) grabKeyboardFocus();
  for (int i = 0; i < count(); ++i)
    if (tileBounds(i).contains(e.getPosition())) {
      if (onSelect) onSelect(i);
      return;
    }
}

bool SceneStrip::keyPressed(const juce::KeyPress& k) {
  if (count() == 0) return false;
  int next = selected_;
  if (k == juce::KeyPress::leftKey) next = std::max(0, selected_ - 1);
  else if (k == juce::KeyPress::rightKey) next = std::min(count() - 1, selected_ + 1);
  else return false;
  if (onSelect) onSelect(next);
  return true;
}

// ---------------------------------------------------------------------------------------------
// ScenesPanel
// ---------------------------------------------------------------------------------------------

ScenesPanel::ScenesPanel(EditorModel& m, EditorCommands& c, PerformCommands& perform) : DrawerPanel(m, c, "ScenesPanel", 20), perform_(perform) {
  styleLabel(status_, "Scenes stopped", false);
  styleLabel(selectedText_, "");
  styleLabel(nameLabel_, "Name");
  styleLabel(loopsLabel_, "Scene loops");
  styleLabel(afterLabel_, "Then");
  styleLabel(jumpLabel_, "Jump to");
  styleLabel(info_, "");
  styleLabel(morphStatus_, "");
  styleLabel(morphState_, "A and B: not stored");
  selectedText_.setJustificationType(juce::Justification::topLeft);
  morphStatus_.setJustificationType(juce::Justification::topLeft);
  name_.setMultiLine(false);
  name_.setInputRestrictions(static_cast<int>(cf::scenes::kMaxNameLength));
  loops_.setSliderStyle(juce::Slider::LinearHorizontal);
  loops_.setTextBoxStyle(juce::Slider::TextBoxRight, false, static_cast<int>(44 * cdl::uiScale()), 22);
  loops_.setRange(1, cf::scenes::kMaxLoops, 1);
  after_.addItemList({"Next scene", "Repeat", "Stop", "Jump"}, 1);
  after_.setSelectedId(1, juce::dontSendNotification);
  morph_.setSliderStyle(juce::Slider::LinearHorizontal);
  morph_.setTextBoxStyle(juce::Slider::TextBoxRight, false, static_cast<int>(64 * cdl::uiScale()), 22);
  morph_.setRange(0.0, 1.0, 0.001);
  morph_.setValue(0.5, juce::dontSendNotification);
  morph_.setDoubleClickReturnValue(true, 0.5);
  morph_.textFromValueFunction = [](double v) { return juce::String::fromUTF8(cf::ui::morphAmountText(v).c_str()); };
  coalescer_.reset(0.5);

  describe(strip_, "Scene strip", "Click a scene to select it, Left and Right move the selection");
  describe(name_, "Scene name", "The selected scene's name");
  describe(loops_, "Scene loops", "How many loops the selected scene plays");
  describe(after_, "After this scene", "What follows the selected scene");
  describe(jump_, "Jump target", "The scene to jump to after the selected one");
  describe(capture_, "Capture Scene", "Store the current pattern as a new scene");
  describe(update_, "Update Scene", "Replace the selected scene with the current pattern");
  describe(keep_, "Keep Scene", "Add the selected scene to the variation tree");
  describe(delete_, "Delete Scene", "Remove the selected scene");
  describe(up_, "Move Up", "Move the selected scene earlier in the list");
  describe(down_, "Move Down", "Move the selected scene later in the list");
  describe(clear_, "Clear All", "Remove every scene from the list");
  describe(export_, "Export Scenes...", "Write the scene list and a MIDI file per scene to a folder");
  describe(arrange_, "Arrange", "Open the arrangement builder to turn the scenes into a finished piece");
  describe(wav_, "Include WAV", "Also render every scene end to end into one WAV");
  describe(play_, "Play Scenes", "Play the scenes in order, switching on the loop boundary");
  describe(stop_, "Stop Scenes", "Stop the scenes and go back to the pattern");
  describe(rec_, "Rec Scenes", "Capture a scene at each Evolve step while Evolve runs");
  describe(wrap_, "Wrap", "Start again from the first scene after the last one");
  describe(morphOn_, "Morph", "Blend the pattern between stores A and B with the slider");
  describe(morph_, "Morph amount", "How far the blend is from A toward B");
  describe(storeA_, "Store A", "Keep the current pattern as A");
  describe(recallA_, "Recall A", "Bring back the stored pattern A");
  describe(storeB_, "Store B", "Keep the current pattern as B");
  describe(recallB_, "Recall B", "Bring back the stored pattern B");
  describe(commit_, "Commit Morph", "Keep the blend as a new variation");
  describe(cancelMorph_, "Cancel Morph", "Drop the blend and go back to the pattern");
  describe(status_, "Scene status", "What the scenes are doing now");
  describe(morphStatus_, "Morph status", "What the morph is doing now");

  add({&status_, &strip_, &selectedText_, &nameLabel_, &name_, &loopsLabel_, &loops_, &afterLabel_, &after_, &jumpLabel_, &jump_, &info_, &capture_, &update_, &keep_, &delete_,
       &up_, &down_, &clear_, &export_, &arrange_, &wav_, &play_, &stop_, &rec_, &wrap_, &morphState_, &storeA_, &recallA_, &storeB_, &recallB_, &morphOn_, &morph_, &commit_, &cancelMorph_,
       &morphStatus_});

  strip_.onSelect = [this](int i) {
    select(i);
    tick();
  };
  name_.onReturnKey = [this] {
    if (selectedId_ && !updating_) perform_.editScene(selectedId_, [n = name_.getText().toStdString()](cf::scenes::Scene& s) { s.name = n; });
  };
  name_.onFocusLost = name_.onReturnKey;
  loops_.onDragEnd = [this] {
    if (selectedId_ && !updating_) perform_.editScene(selectedId_, [n = static_cast<int>(loops_.getValue())](cf::scenes::Scene& s) { s.loops = n; });
  };
  loops_.onValueChange = [this] {
    if (selectedId_ && !updating_ && !loops_.isMouseButtonDown()) perform_.editScene(selectedId_, [n = static_cast<int>(loops_.getValue())](cf::scenes::Scene& s) { s.loops = n; });
  };
  after_.onChange = [this] {
    if (!selectedId_ || updating_) return;
    // Choosing Jump needs a target at the same moment, so the first other scene is used until the person picks one.
    const cf::scenes::SceneId fallback = jumpIds_.empty() ? 0 : jumpIds_.front();
    perform_.editScene(selectedId_, [n = static_cast<cf::scenes::Next>(after_.getSelectedId() - 1), fallback](cf::scenes::Scene& s) {
      s.next = n;
      if (n == cf::scenes::Next::Jump && s.jumpTo == 0) s.jumpTo = fallback;
    });
    tick();
  };
  jump_.onChange = [this] {
    const int i = jump_.getSelectedId() - 1;
    if (!selectedId_ || updating_ || i < 0 || i >= static_cast<int>(jumpIds_.size())) return;
    perform_.editScene(selectedId_, [id = jumpIds_[static_cast<std::size_t>(i)]](cf::scenes::Scene& s) { s.jumpTo = id; });
  };
  capture_.onClick = [this] {
    perform_.captureScene();
    const auto st = perform_.sceneState();
    if (!st.scenes.empty()) select(static_cast<int>(st.scenes.size()) - 1);
    tick();
  };
  update_.onClick = [this] { perform_.updateScene(selectedId_); tick(); };
  keep_.onClick = [this] { perform_.keepScene(selectedId_); tick(); };
  delete_.onClick = [this] { perform_.removeScene(selectedId_); selectedId_ = 0; tick(); };
  up_.onClick = [this] { perform_.moveScene(selectedId_, -1); tick(); };
  down_.onClick = [this] { perform_.moveScene(selectedId_, 1); tick(); };
  clear_.onClick = [this] { perform_.clearScenes(); tick(); };
  export_.onClick = [this] { perform_.exportScenes(wav_.getToggleState()); };
  arrange_.onClick = [this] {
    if (perform_.openDrawer) perform_.openDrawer("arrange");
  };
  play_.onClick = [this] {
    if (play_.getToggleState()) {
      if (!perform_.playScenes(selectedId_)) play_.setToggleState(false, juce::dontSendNotification);  // refused: the message is in the status line
    } else {
      perform_.stopScenes();
    }
    tick();
  };
  stop_.onClick = [this] { perform_.stopScenes(); tick(); };
  rec_.onClick = [this] { perform_.setRecordScenes(rec_.getToggleState()); tick(); };
  wrap_.onClick = [this] { perform_.setSceneWrap(wrap_.getToggleState()); tick(); };

  storeA_.onClick = [this] { cmd_.storeSnapshot(0); tick(); };
  storeB_.onClick = [this] { cmd_.storeSnapshot(1); tick(); };
  recallA_.onClick = [this] { cmd_.recallSnapshot(0); tick(); };
  recallB_.onClick = [this] { cmd_.recallSnapshot(1); tick(); };
  morphOn_.onClick = [this] {
    if (morphOn_.getToggleState()) {
      if (perform_.beginMorph(morph_.getValue())) coalescer_.reset(morph_.getValue());
      else morphOn_.setToggleState(false, juce::dontSendNotification);
    } else {
      perform_.cancelMorph();
    }
    tick();
  };
  morph_.onValueChange = [this] {
    if (!updating_) coalescer_.setPending(morph_.getValue());
  };
  morph_.onDragEnd = [this] {
    coalescer_.release();
    flushMorph(juce::Time::getMillisecondCounter());
  };
  commit_.onClick = [this] { perform_.commitMorph(); tick(); };
  cancelMorph_.onClick = [this] { perform_.cancelMorph(); tick(); };
  tick();
}

void ScenesPanel::select(int index) {
  const auto st = perform_.sceneState();
  selectedId_ = index >= 0 && index < static_cast<int>(st.scenes.size()) ? st.scenes[static_cast<std::size_t>(index)].id : 0;
}

void ScenesPanel::flushMorph(long long nowMs) {
  double v = 0.0;
  if (!coalescer_.take(nowMs, v)) return;
  if (!perform_.setMorph(v)) return;  // the message is in the status line; the toggle stays on
}

void ScenesPanel::syncDetail(const PerformCommands::SceneState& st) {
  const int index = st.indexOf(selectedId_);
  const bool have = index >= 0;
  const auto* sc = have ? &st.scenes[static_cast<std::size_t>(index)] : nullptr;
  updating_ = true;
  if (sc) {
    if (!name_.hasKeyboardFocus(true) && name_.getText().toStdString() != sc->name) name_.setText(juce::String::fromUTF8(sc->name.c_str()), false);
    if (!loops_.isMouseButtonDown()) loops_.setValue(sc->loops, juce::dontSendNotification);
    after_.setSelectedId(static_cast<int>(sc->next) + 1, juce::dontSendNotification);
    jump_.clear(juce::dontSendNotification);
    jumpIds_.clear();
    int id = 1;
    for (std::size_t i = 0; i < st.scenes.size(); ++i) {
      if (st.scenes[i].id == sc->id) continue;
      jump_.addItem("Scene " + juce::String(static_cast<int>(i) + 1), id++);
      jumpIds_.push_back(st.scenes[i].id);
    }
    for (std::size_t i = 0; i < jumpIds_.size(); ++i)
      if (jumpIds_[i] == sc->jumpTo) jump_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
    info_.setText("Seed " + juce::String(static_cast<juce::int64>(sc->seed)) + ", " + juce::String(static_cast<int>(sc->eventCount)) + " hits, " + juce::String(sc->bars) +
                      (sc->bars == 1 ? " bar" : " bars"),
                  juce::dontSendNotification);
  } else {
    name_.setText("", false);
    info_.setText("", juce::dontSendNotification);
    jump_.clear(juce::dontSendNotification);
    jumpIds_.clear();
  }
  updating_ = false;
  const bool playing = st.status.running || st.status.holding;
  name_.setEnabled(have);
  loops_.setEnabled(have);
  after_.setEnabled(have);
  const bool jumpOn = have && sc->next == cf::scenes::Next::Jump && st.scenes.size() > 1;
  jump_.setEnabled(jumpOn);
  (void)playing;
}

void ScenesPanel::tick() {
  state_ = perform_.sceneState();
  if (selectedId_ && state_.indexOf(selectedId_) < 0) selectedId_ = 0;
  if (!selectedId_ && !state_.scenes.empty()) selectedId_ = state_.scenes.front().id;
  const int selected = state_.indexOf(selectedId_);
  sceneView_ = perform_.sceneView(state_, selected);
  morphView_ = perform_.morphView(state_);
  const auto& v = sceneView_;
  status_.setText(juce::String::fromUTF8(v.statusText.c_str()), juce::dontSendNotification);
  std::vector<SceneStrip::Tile> tiles;
  for (std::size_t i = 0; i < state_.scenes.size(); ++i) {
    const auto& sc = state_.scenes[i];
    SceneStrip::Tile t;
    t.number = static_cast<int>(i) + 1;
    t.loops = sc.loops;
    t.name = sc.name;
    t.text = v.rows[i];
    t.playing = state_.status.playing == sc.id;
    t.armed = state_.status.armed == sc.id;
    t.fits = sc.fits;
    tiles.push_back(std::move(t));
  }
  const bool countChanged = strip_.count() != static_cast<int>(tiles.size());
  strip_.set(std::move(tiles), selected, juce::String::fromUTF8(v.emptyText.c_str()));
  selectedText_.setText(selected >= 0 ? juce::String::fromUTF8(v.rows[static_cast<std::size_t>(selected)].c_str()) : juce::String(), juce::dontSendNotification);
  strip_.setTooltip(selected >= 0 ? juce::String::fromUTF8(v.rows[static_cast<std::size_t>(selected)].c_str()) : juce::String("Click a scene to select it, Left and Right move the selection"));
  syncDetail(state_);

  const juce::String stop = juce::String::fromUTF8(v.editTooltip.c_str());
  capture_.setEnabled(v.captureEnabled);
  update_.setEnabled(v.updateEnabled);
  keep_.setEnabled(v.keepEnabled);
  delete_.setEnabled(v.deleteEnabled);
  up_.setEnabled(v.moveUpEnabled);
  down_.setEnabled(v.moveDownEnabled);
  clear_.setEnabled(v.clearEnabled);
  export_.setEnabled(v.exportEnabled);
  play_.setEnabled(v.playEnabled || v.playOn);
  stop_.setEnabled(v.stopEnabled);
  rec_.setEnabled(v.recEnabled);
  for (auto* b : std::initializer_list<juce::Button*>{&delete_, &up_, &down_, &clear_}) b->setTooltip(b->isEnabled() || stop.isEmpty() ? b->getTooltip() : stop);
  export_.setTooltip(juce::String::fromUTF8(v.exportTooltip.c_str()));
  const bool canArrange = cf::ui::arrangeButtonEnabled(static_cast<int>(state_.scenes.size()), state_.hasSource);
  arrange_.setEnabled(canArrange);
  arrange_.setTooltip(canArrange ? juce::String("Open the arrangement builder to turn the scenes into a finished piece") : juce::String::fromUTF8(cf::ui::arrangeButtonTooltip(static_cast<int>(state_.scenes.size())).c_str()));
  play_.setToggleState(v.playOn, juce::dontSendNotification);
  rec_.setToggleState(v.recOn, juce::dontSendNotification);
  wrap_.setToggleState(v.wrapOn, juce::dontSendNotification);

  // Morph row.
  const auto& mv = morphView_;
  morphState_.setText(juce::String(state_.hasA ? "A stored" : "A empty") + ", " + (state_.hasB ? "B stored" : "B empty"), juce::dontSendNotification);
  morphOn_.setEnabled(mv.toggleEnabled);
  morphOn_.setToggleState(mv.toggleOn, juce::dontSendNotification);
  morphOn_.setTooltip(mv.toggleEnabled ? juce::String("Blend the pattern between stores A and B with the slider") : juce::String::fromUTF8(mv.toggleTooltip.c_str()));
  morph_.setEnabled(mv.sliderEnabled);
  if (mv.sliderEnabled && !morph_.isMouseButtonDown()) {
    updating_ = true;
    morph_.setValue(state_.morphAmount, juce::dontSendNotification);
    updating_ = false;
  }
  commit_.setEnabled(mv.commitEnabled);
  cancelMorph_.setEnabled(mv.cancelEnabled);
  morphStatus_.setText(juce::String::fromUTF8(mv.status.c_str()), juce::dontSendNotification);
  flushMorph(juce::Time::getMillisecondCounter());
  if (countChanged) relayout();
}

int ScenesPanel::layoutBody(int width) {
  beginLayout();
  Stack s(width);
  const int lw = Stack::scaled(86);
  heading(s, "Scenes");
  status_.setBounds(s.row());
  strip_.setBounds(s.row(strip_.heightFor(width)));
  selectedText_.setBounds(s.row(Stack::scaled(34)));
  labelled(s.row(), nameLabel_, name_, lw);
  labelled(s.row(), loopsLabel_, loops_, lw);
  labelled(s.row(), afterLabel_, after_, lw);
  labelled(s.row(), jumpLabel_, jump_, lw);
  info_.setBounds(s.row());
  grid(s, {&capture_, &update_, &keep_, &delete_, &up_, &down_, &clear_, &export_, &arrange_}, 2);
  grid(s, {&play_, &stop_, &rec_, &wrap_, &wav_}, 2);
  heading(s, "A/B morph");
  morphState_.setBounds(s.row());
  grid(s, {&storeA_, &recallA_, &storeB_, &recallB_}, 2);
  morphOn_.setBounds(s.row());
  morph_.setBounds(s.row());
  grid(s, {&commit_, &cancelMorph_}, 2);
  morphStatus_.setBounds(s.row(Stack::scaled(44)));
  body_.repaint();
  return s.height();
}
