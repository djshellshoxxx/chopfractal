#pragma once
// Export drawer (docs/specs/gui-redesign.md 7.7): WAV bit depth, rate and loops, Export WAV and Producer Kit, the MIDI
// options (base note, velocity curve, loops, channel), the three drag-out handles, and the arrangement export built on
// the F18 session API, with one progress bar and a Cancel button for whichever export runs. Settings are project state
// (extras.exportUi), so a reopened project exports the same way.
#include <chopfractal/plugin_ui_adapter/export_view.hpp>
#include <chopfractal/plugin_ui_adapter/session_views.hpp>
#include <memory>
#include <vector>

#include "DragOut.h"
#include "EditorPanels.h"

class ExportDrawer : public EditorPanelBase, private juce::ListBoxModel {
 public:
  ExportDrawer(EditorModel&, EditorCommands&);
  void resized() override;
  void tick();  // the editor's 10 Hz timer: progress, enables, finished arrangement jobs (also while the drawer is closed)

  // Read-only state for tests.
  const chopfractal::ui::ExportView& exportView() const { return exportView_; }
  const chopfractal::ui::ArrangementView& arrangementView() const { return arrangementView_; }
  double progress() const { return progressValue_; }
  juce::String progressText() const { return progressText_; }
  bool cancelVisible() const { return cancel_.isVisible(); }
  DragHandle& dragMidi() { return dragMidi_; }
  DragHandle& dragWav() { return dragWav_; }
  DragHandle& dragKit() { return dragKit_; }
  void refresh();  // re-read the settings and the arrangement from the session

 private:
  struct Body : juce::Component {
    ExportDrawer& owner;
    explicit Body(ExportDrawer& o) : owner(o) {}
    void paint(juce::Graphics& g) override { owner.paintBody(g); }
  };
  void paintBody(juce::Graphics&);
  void layoutBody(int width);
  void modelChanged(Change) override;
  void lookAndFeelChanged() override;
  void pushFormat();
  void pushMidi();
  void syncWidgets(bool settings);
  void syncArrangement();
  void editSelectedSection(const std::function<void(chopfractal::composition::ArrangementSection&)>&);
  void cancelAny();
  int getNumRows() override { return static_cast<int>(arrangementView_.rows.size()); }
  void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
  juce::String getNameForRow(int row) override;
  void selectedRowsChanged(int) override { updateArrangementButtons(); }
  void updateArrangementButtons();

  juce::Viewport viewport_;
  Body body_{*this};
  juce::ComboBox format_, rate_, loops_;
  juce::ToggleButton normalize_{"Normalize to -1 dB"};
  juce::TextButton exportWav_{"Export WAV"}, exportKit_{"Export Kit"};
  juce::Label formatLabel_{{}, "Bit depth"}, rateLabel_{{}, "Sample rate"}, loopsLabel_{{}, "Loops"};
  juce::Slider baseNote_;
  juce::ComboBox curve_, midiLoops_, channel_;
  juce::Slider fixedVelocity_;
  juce::Label baseNoteLabel_{{}, "Base note"}, curveLabel_{{}, "Velocity"}, fixedLabel_{{}, "Fixed"}, midiLoopsLabel_{{}, "MIDI loops"}, channelLabel_{{}, "Channel"};
  DragHandle dragMidi_{"Drag MIDI", [this] { return cmd_.makeDragFile(false, cmd_.exportUi()); }};
  DragHandle dragWav_{"Drag WAV", [this] { return cmd_.makeDragFile(true, cmd_.exportUi()); }};
  DragHandle dragKit_{"Drag Kit", [this] { return cmd_.makeKitDragSet(); }};
  // Arrangement
  juce::ListBox list_{"Arrangement", this};
  juce::ComboBox scene_, sectionBars_, repeats_, transition_;
  juce::TextButton add_{"Add"}, remove_{"Remove"}, up_{"Up"}, down_{"Down"}, chain_{"Use scene chain"}, autoFill_{"Auto fill 16 bars"}, exportArr_{"Export arrangement"};
  juce::ToggleButton fadeOut_{"Fade out"}, tail_{"Tail"}, midi_{"MIDI file"}, markers_{"Markers"};
  juce::Label sceneLabel_{{}, "Scene"}, barsLabel_{{}, "Section bars"}, repeatsLabel_{{}, "Repeats"}, transitionLabel_{{}, "Transition"};
  // Progress strip
  juce::TextButton cancel_{"Cancel"};
  double progressValue_ = 0.0;
  juce::ProgressBar progressBar_{progressValue_};
  juce::String progressText_;

  chopfractal::ui::ExportView exportView_;
  chopfractal::ui::ArrangementView arrangementView_;
  ArrangementState arrState_;
  chopfractal::ui::MidiOptionsView midiView_;
  juce::Rectangle<int> formatArea_, midiArea_, dragArea_, arrArea_, estimateArea_, tempoArea_, totalArea_;
  juce::String estimateText_, tempoText_, totalText_, hintText_, notesText_;
  bool syncing_ = false;
};
