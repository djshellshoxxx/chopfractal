#pragma once
// Arrangement drawer (F18 section 12): build an 8 to 32 bar arrangement from the stored scenes (a list of sections, each a
// scene for some bars and repeats), fill it from the scene chain or automatically, set the transition and options, then
// export WAV, MIDI and markers on a worker with progress and Cancel. Texts and enable rules come from ui::buildArrangementView().
#include <chopfractal/plugin_ui_adapter/session_views.hpp>

#include "DrawerPanel.h"
#include "PerformCommands.h"

class SectionList : public juce::Component, public juce::SettableTooltipClient {
 public:
  SectionList();
  void set(std::vector<juce::String> rows, int selected, const juce::String& emptyText);
  int count() const { return static_cast<int>(rows_.size()); }
  int selected() const { return selected_; }
  int heightFor() const;
  juce::Rectangle<int> rowBounds(int i) const;
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
  bool keyPressed(const juce::KeyPress&) override;
  std::function<void(int)> onSelect;
  std::function<void()> onDelete;
  std::function<void(int)> onMove;  // -1 up, +1 down

 private:
  std::vector<juce::String> rows_;
  int selected_ = -1;
  juce::String empty_;
};

class ArrangementPanel : public DrawerPanel {
 public:
  ArrangementPanel(EditorModel&, EditorCommands&, PerformCommands&);
  SectionList& list() { return list_; }
  const chopfractal::ui::ArrangementView& view() const { return view_; }
  int selectedRow() const { return selected_; }
  void select(int row) { selected_ = row; tick(); }
  juce::String exportName() const { return name_.getText(); }

 private:
  int layoutBody(int width) override;
  void tick() override;
  void exportNow();

  PerformCommands& perform_;
  PerformCommands::ArrangementState state_;
  chopfractal::ui::ArrangementView view_;
  int selected_ = -1;
  bool updating_ = false;
  std::vector<int> barChoices_;
  juce::Label total_, status_, notes_, sceneLabel_, barsLabel_, repeatsLabel_, transitionLabel_, nameLabel_;
  SectionList list_;
  juce::ComboBox scene_, bars_, transition_;
  juce::Slider repeats_;
  juce::TextEditor name_;
  juce::TextButton add_{"Add Section"}, remove_{"Remove Section"}, up_{"Section Up"}, down_{"Section Down"}, chain_{"Use scene chain"}, autoFill_{"Auto fill 16 bars"},
      export_{"Export Arrangement"}, cancel_{"Cancel Arrangement"};
  juce::ToggleButton fade_{"Fade out last bar"}, tail_{"Add tail"}, midi_{"Write MIDI"}, markers_{"Write markers"};
  double progress_ = 0.0;
  juce::ProgressBar progressBar_{progress_};
};
