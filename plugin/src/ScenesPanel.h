#pragma once
// Scenes drawer (F09 and F10 section 12): the scene strip (capture, edit, order, chain, play), recording of the Evolve wander,
// and the A/B morph row (store, recall, morph slider with coalescing, commit as a variation). All wording, enable rules and
// the status line come from ui::buildSceneView() / ui::buildMorphView().
#include <chopfractal/plugin_ui_adapter/session_views.hpp>

#include "DrawerPanel.h"
#include "PerformCommands.h"

// A row of numbered tiles, one per scene: the playing scene is filled, the armed one is marked, the selected one has a heavier
// outline. Clicking selects; Left / Right move the selection.
class SceneStrip : public juce::Component, public juce::SettableTooltipClient {
 public:
  struct Tile {
    int number = 0, loops = 0;
    std::string name, text;
    bool playing = false, armed = false, fits = true;
  };
  SceneStrip();
  void set(std::vector<Tile> tiles, int selected, const juce::String& emptyText);
  int selected() const { return selected_; }
  int count() const { return static_cast<int>(tiles_.size()); }
  juce::Rectangle<int> tileBounds(int i) const;
  int heightFor(int width) const;
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
  bool keyPressed(const juce::KeyPress&) override;
  std::function<void(int)> onSelect;

 private:
  int columns() const { return 8; }
  std::vector<Tile> tiles_;
  int selected_ = -1;
  juce::String emptyText_;
};

class ScenesPanel : public DrawerPanel {
 public:
  ScenesPanel(EditorModel&, EditorCommands&, PerformCommands&);
  SceneStrip& strip() { return strip_; }
  chopfractal::scenes::SceneId selectedId() const { return selectedId_; }
  void select(int index);
  const chopfractal::ui::SceneView& sceneView() const { return sceneView_; }
  const chopfractal::ui::MorphView& morphView() const { return morphView_; }
  juce::Slider& morphSlider() { return morph_; }
  // Applies the coalesced morph value now (tests and the timer).
  void flushMorph(long long nowMs);

 private:
  int layoutBody(int width) override;
  void tick() override;
  void syncDetail(const PerformCommands::SceneState&);

  PerformCommands& perform_;
  PerformCommands::SceneState state_;
  chopfractal::ui::SceneView sceneView_;
  chopfractal::ui::MorphView morphView_;
  chopfractal::ui::MorphCoalescer coalescer_;
  chopfractal::scenes::SceneId selectedId_ = 0;
  bool updating_ = false;
  std::vector<chopfractal::scenes::SceneId> jumpIds_;

  juce::Label status_, selectedText_, nameLabel_, loopsLabel_, afterLabel_, jumpLabel_, info_, morphStatus_, morphState_;
  SceneStrip strip_;
  juce::TextEditor name_;
  juce::Slider loops_, morph_;
  juce::ComboBox after_, jump_;
  juce::TextButton capture_{"Capture Scene"}, update_{"Update Scene"}, keep_{"Keep Scene"}, delete_{"Delete Scene"}, up_{"Move Up"}, down_{"Move Down"},
      clear_{"Clear All"}, export_{"Export Scenes..."}, stop_{"Stop Scenes"}, arrange_{"Arrange..."};
  juce::ToggleButton play_{"Play Scenes"}, rec_{"Rec Scenes"}, wrap_{"Wrap"}, wav_{"Include WAV"}, morphOn_{"Morph"};
  juce::TextButton storeA_{"Store A"}, recallA_{"Recall A"}, storeB_{"Store B"}, recallB_{"Recall B"}, commit_{"Commit Morph"}, cancelMorph_{"Cancel Morph"};
};
