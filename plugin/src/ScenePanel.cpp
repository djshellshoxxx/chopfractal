#include "ScenePanel.h"

#include <algorithm>
#include <initializer_list>

#include "PluginProcessor.h"

namespace cf = chopfractal;
namespace sc = chopfractal::scenes;

ScenePanel::ScenePanel(ChopFractalProcessor& processor) : proc_(processor) {
  setName("ScenePanel");
  setWantsKeyboardFocus(true);
  header_.setText("Evolve scenes", juce::dontSendNotification);
  header_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
  addAndMakeVisible(header_);
  model_.count = [this] { return static_cast<int>(rows_.size()); };
  model_.text = [this](int r) { return r < static_cast<int>(rows_.size()) ? rows_[static_cast<std::size_t>(r)] : juce::String(); };
  model_.onSelect = [this](int r) {
    if (r < static_cast<int>(ids_.size())) selected_ = ids_[static_cast<std::size_t>(r)];
    refreshForm();
  };
  list_.setModel(&model_);
  list_.setTitle("Scene list");
  list_.setName("Scene list");
  addAndMakeVisible(list_);

  name_.setTitle("Scene name");
  name_.setInputRestrictions(32);
  loops_.setTitle("Scene loops");
  loops_.setRange(1, sc::kMaxLoops, 1);
  loops_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
  after_.setTitle("After this scene");
  after_.addItemList({"Next scene", "Repeat this scene", "Stop", "Jump to..."}, 1);
  target_.setTitle("Jump target");
  for (juce::Component* c : std::initializer_list<juce::Component*>{&name_, &loops_, &after_, &target_, &details_, &status_}) addAndMakeVisible(*c);
  for (auto* l : {&nameCaption_, &loopsCaption_, &afterCaption_, &targetCaption_}) addAndMakeVisible(*l);
  nameCaption_.setText("Name", juce::dontSendNotification);
  loopsCaption_.setText("Loops", juce::dontSendNotification);
  afterCaption_.setText("After", juce::dontSendNotification);
  targetCaption_.setText("Jump to", juce::dontSendNotification);
  name_.onTextChange = [this] { formChanged(); };
  loops_.onValueChange = [this] { formChanged(); };
  after_.onChange = [this] { formChanged(); };
  target_.onChange = [this] { formChanged(); };

  for (juce::Button* b : std::initializer_list<juce::Button*>{&capture_, &update_, &keep_, &up_, &down_, &delete_, &clear_, &export_, &stop_, &close_, &rec_, &play_, &wrap_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  rec_.setTitle("Record scenes while Evolve runs");
  play_.setTitle("Play the scenes in sequence");
  wrap_.setToggleState(true, juce::dontSendNotification);
  capture_.onClick = [this] {
    runAndReport([](cf::composition::ProjectSession& s) {
      auto id = s.captureScene();
      return id.ok() ? cf::Status{} : cf::Status{id.error()};
    });
  };
  update_.onClick = [this] { runAndReport([id = selectedId()](cf::composition::ProjectSession& s) { return s.updateScene(id); }); };
  keep_.onClick = [this] { runAndReport([id = selectedId()](cf::composition::ProjectSession& s) { return s.keepScene(id); }); };
  up_.onClick = [this] {
    runAndReport([this](cf::composition::ProjectSession& s) { return s.moveScene(selectedId(), std::max(0, sc::indexOf(s.sceneList(), selectedId()) - 1)); });
  };
  down_.onClick = [this] {
    runAndReport([this](cf::composition::ProjectSession& s) { return s.moveScene(selectedId(), sc::indexOf(s.sceneList(), selectedId()) + 1); });
  };
  delete_.onClick = [this] {
    runAndReport([id = selectedId()](cf::composition::ProjectSession& s) { return s.removeScene(id); });
    selected_ = 0;
    refresh();
  };
  clear_.onClick = [this] {
    juce::WeakReference<ChopFractalProcessor> proc(&proc_);
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                     .withTitle("Delete all scenes?")
                                     .withMessage("This cannot be undone.")
                                     .withButton("Delete All")
                                     .withButton("Cancel"),
                                 [proc](int result) {
                                   if (proc != nullptr && result == 1) proc->withSession([](cf::composition::ProjectSession& s) { s.clearScenes(); });
                                 });
  };
  export_.onClick = [this] { exportScenes(); };
  stop_.onClick = [this] {
    runAndReport([](cf::composition::ProjectSession& s) { return s.stopScenes(); });
    play_.setToggleState(false, juce::dontSendNotification);
  };
  close_.onClick = [this] { close(); };
  rec_.onClick = [this] { runAndReport([on = rec_.getToggleState()](cf::composition::ProjectSession& s) { return s.setRecordScenes(on); }); };
  play_.onClick = [this] {
    if (play_.getToggleState()) {
      runAndReport([this](cf::composition::ProjectSession& s) { return s.playScenes(selected_, cf::composition::Quantize::LoopBoundary); });
    } else {
      runAndReport([](cf::composition::ProjectSession& s) { return s.stopScenes(); });
    }
  };
  wrap_.onClick = [this] { runAndReport([on = wrap_.getToggleState()](cf::composition::ProjectSession& s) { return s.setSceneWrap(on); }); };
  setVisible(false);
  startTimerHz(10);
}

std::uint32_t ScenePanel::selectedId() const { return selected_; }

void ScenePanel::open() {
  refresh();
  setVisible(true);
  toFront(true);
  grabKeyboardFocus();
}

void ScenePanel::close() { setVisible(false); }

bool ScenePanel::keyPressed(const juce::KeyPress& key) {
  if (key == juce::KeyPress::escapeKey) {
    close();
    return true;
  }
  return false;
}

void ScenePanel::runAndReport(const std::function<cf::Status(cf::composition::ProjectSession&)>& action) {
  cf::Status st;
  juce::String note;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    st = action(s);
    for (const auto& n : s.takeNotices()) note = juce::String(n.text);
  });
  proc_.setStatusMessage(st.ok() ? note : juce::String(st.error().message));
  refresh();
}

void ScenePanel::refresh() {
  std::vector<juce::String> rows;
  std::vector<std::uint32_t> ids;
  cf::composition::SceneStatus st;
  bool wrap = true, havePattern = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    st = s.sceneStatus();
    wrap = s.sceneList().wrap;
    havePattern = s.pattern() != nullptr;
    const auto& list = s.sceneList();
    for (std::size_t i = 0; i < list.scenes.size(); ++i) {
      const sc::Scene& sn = list.scenes[i];
      juce::String after = sn.next == sc::Next::Advance ? "next" : sn.next == sc::Next::Repeat ? "repeat" : sn.next == sc::Next::Stop ? "stop"
                                                                                                         : "jump to " + juce::String(sc::indexOf(list, sn.jumpTo) + 1);
      juce::String text = juce::String(i + 1) + "  " + sn.name + "   " + juce::String(sn.loops) + (sn.loops == 1 ? " loop" : " loops") + "   > " + after;
      if (st.running && sn.id == st.playing) text += "  [Playing, loop " + juce::String(st.loopIndex + 1) + " of " + juce::String(st.loops) + "]";
      else if (st.running && sn.id == st.armed) text += "  [Next]";
      rows.push_back(text);
      ids.push_back(sn.id);
    }
  });
  const bool listChanged = rows != rows_;
  rows_ = std::move(rows);
  ids_ = std::move(ids);
  if (listChanged) list_.updateContent();
  if (!rows_.empty() || listChanged) list_.repaint();
  if (selected_ != 0 && std::find(ids_.begin(), ids_.end(), selected_) == ids_.end()) selected_ = 0;
  if (listChanged) {
    const auto it = std::find(ids_.begin(), ids_.end(), selected_);
    loading_ = true;
    if (it != ids_.end()) list_.selectRow(static_cast<int>(it - ids_.begin()), false, false);
    else list_.deselectAllRows();
    loading_ = false;
    refreshForm();
  }
  const bool has = selected_ != 0;
  update_.setEnabled(has && havePattern);
  keep_.setEnabled(has);
  up_.setEnabled(has && !st.running);
  down_.setEnabled(has && !st.running);
  delete_.setEnabled(has && !st.running);
  clear_.setEnabled(!rows_.empty() && !st.running);
  capture_.setEnabled(havePattern);
  rec_.setEnabled(havePattern);
  play_.setEnabled(!rows_.empty());
  export_.setEnabled(!rows_.empty());
  for (juce::Button* b : {&up_, &down_, &delete_, &clear_})
    if (st.running) b->setTooltip("Stop scene playback first.");
  if (rec_.getToggleState() != st.recording) rec_.setToggleState(st.recording, juce::dontSendNotification);
  if (play_.getToggleState() != st.running) play_.setToggleState(st.running, juce::dontSendNotification);
  if (wrap_.getToggleState() != wrap) wrap_.setToggleState(wrap, juce::dontSendNotification);
  juce::String text = "Scenes stopped";
  if (st.running) {
    const int number = [&] {
      for (std::size_t i = 0; i < ids_.size(); ++i)
        if (ids_[i] == st.playing) return static_cast<int>(i) + 1;
      return 0;
    }();
    text = st.waitingForTransport ? "Waiting for the host to play" : "Scene " + juce::String(number) + " of " + juce::String(rows_.size()) + ", loop " + juce::String(st.loopIndex + 1) + " of " + juce::String(st.loops);
  } else if (st.recording) {
    text = "Recording scenes: waiting for Evolve";
  } else if (rows_.empty()) {
    text = "No scenes yet. Press Capture Scene, or turn on Rec Scenes and start Evolve.";
  }
  if (status_.getText() != text) status_.setText(text, juce::dontSendNotification);
}

void ScenePanel::refreshForm() {
  const sc::Scene* found = nullptr;
  sc::Scene copy;
  juce::StringArray targets;
  std::vector<std::uint32_t> targetIds;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    if (const sc::Scene* x = sc::find(s.sceneList(), selected_)) {
      copy = *x;
      found = &copy;
    }
    for (std::size_t i = 0; i < s.sceneList().scenes.size(); ++i) {
      targets.add("Scene " + juce::String(i + 1) + ": " + s.sceneList().scenes[i].name);
      targetIds.push_back(s.sceneList().scenes[i].id);
    }
  });
  for (juce::Component* c : std::initializer_list<juce::Component*>{&name_, &loops_, &after_, &target_}) c->setEnabled(found != nullptr);
  if (!found) {
    details_.setText({}, juce::dontSendNotification);
    return;
  }
  loading_ = true;
  name_.setText(copy.name, false);
  loops_.setValue(copy.loops, juce::dontSendNotification);
  after_.setSelectedItemIndex(static_cast<int>(copy.next), juce::dontSendNotification);
  target_.clear(juce::dontSendNotification);
  for (int i = 0; i < targets.size(); ++i) target_.addItem(targets[i], static_cast<int>(targetIds[static_cast<std::size_t>(i)]));
  target_.setSelectedId(static_cast<int>(copy.jumpTo), juce::dontSendNotification);
  target_.setVisible(copy.next == sc::Next::Jump);
  targetCaption_.setVisible(copy.next == sc::Next::Jump);
  details_.setText("seed " + juce::String(static_cast<juce::int64>(copy.seed)) + ", " + juce::String(copy.eventCount) + " events, " + juce::String(copy.bars) + " bars",
                   juce::dontSendNotification);
  loading_ = false;
}

void ScenePanel::formChanged() {
  if (loading_ || selected_ == 0) return;
  const juce::String name = name_.getText();
  const int loops = static_cast<int>(loops_.getValue());
  const int next = after_.getSelectedItemIndex();
  const std::uint32_t jump = static_cast<std::uint32_t>(target_.getSelectedId());
  cf::Status st;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    st = s.editScene(selected_, [&](sc::Scene& x) {
      x.name = name.isEmpty() ? x.name : name.toStdString();
      x.loops = loops;
      x.next = static_cast<sc::Next>(juce::jlimit(0, 3, next));
      x.jumpTo = x.next == sc::Next::Jump ? jump : 0;
    });
  });
  if (!st.ok()) proc_.setStatusMessage(st.error().message);
  target_.setVisible(next == 3);
  targetCaption_.setVisible(next == 3);
  refresh();
}

void ScenePanel::exportScenes() {
  chooser_ = std::make_shared<juce::FileChooser>("Export scenes to a folder", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory));
  chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
    const juce::File folder = fc.getResult();
    if (folder == juce::File()) return;
    cf::composition::SceneExportOptions o;
    o.bpm = proc_.activeBpm.load();
    o.wav = true;
    cf::Result<cf::composition::ExportReport> r = cf::makeError(cf::ErrorCode::InvalidArgument, "nothing exported");
    proc_.withSession([&](cf::composition::ProjectSession& s) { r = s.exportScenes(folder.getFullPathName().toStdString(), o); });
    proc_.setStatusMessage(r.ok() ? "Exported " + juce::String(rows_.size()) + " scenes to " + folder.getFullPathName() + "." : juce::String(r.error().message));
  });
}

void ScenePanel::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(0xf015181d));
  g.setColour(juce::Colours::white.withAlpha(0.25f));
  g.drawRect(getLocalBounds());
}

void ScenePanel::resized() {
  auto r = getLocalBounds().reduced(8);
  header_.setBounds(r.removeFromTop(26).removeFromLeft(200));
  auto bottom = r.removeFromBottom(30);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&rec_, &play_, &wrap_}) b->setBounds(bottom.removeFromLeft(110).reduced(2));
  stop_.setBounds(bottom.removeFromLeft(100).reduced(2));
  close_.setBounds(bottom.removeFromRight(110).reduced(2));
  status_.setBounds(bottom);
  auto buttons = r.removeFromBottom(30);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&capture_, &update_, &keep_, &up_, &down_, &delete_, &clear_, &export_}) b->setBounds(buttons.removeFromLeft(110).reduced(2));
  auto left = r.removeFromLeft(r.getWidth() * 6 / 10);
  list_.setBounds(left);
  r.removeFromLeft(10);
  auto row = [&](juce::Label& cap, juce::Component& c) {
    auto line = r.removeFromTop(28);
    cap.setBounds(line.removeFromLeft(70));
    c.setBounds(line);
  };
  row(nameCaption_, name_);
  row(loopsCaption_, loops_);
  row(afterCaption_, after_);
  row(targetCaption_, target_);
  details_.setBounds(r.removeFromTop(24));
}
