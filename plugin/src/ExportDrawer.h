#pragma once
// Export drawer (docs/specs/gui-redesign.md 7.7): WAV bit depth, rate and loops, Export WAV and Producer Kit, the MIDI
// options (base note, velocity curve, loops, channel), the three drag-out handles, with one progress bar and a Cancel button for the
// WAV or Producer Kit export that runs. The arrangement export lives in the Arrange drawer (ArrangementPanel). Settings are project state
// (extras.exportUi), so a reopened project exports the same way.
#include <chopfractal/plugin_ui_adapter/export_view.hpp>
#include <chopfractal/plugin_ui_adapter/session_views.hpp>
#include <memory>
#include <vector>

#include "DragOut.h"
#include "EditorPanels.h"

class ExportDrawer : public EditorPanelBase {
 public:
  ExportDrawer(EditorModel&, EditorCommands&);
  void resized() override;
  void tick();  // the editor's 10 Hz timer: progress and enables (also while the drawer is closed)

  // Read-only state for tests.
  const chopfractal::ui::ExportView& exportView() const { return exportView_; }
  double progress() const { return progressValue_; }
  juce::String progressText() const { return progressText_; }
  bool cancelVisible() const { return cancel_.isVisible(); }
  DragHandle& dragMidi() { return dragMidi_; }
  DragHandle& dragWav() { return dragWav_; }
  DragHandle& dragKit() { return dragKit_; }
  void refresh();  // re-read the settings from the session

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
  void cancelAny();

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
  // Progress strip
  juce::TextButton cancel_{"Cancel"};
  double progressValue_ = 0.0;
  juce::ProgressBar progressBar_{progressValue_};
  juce::String progressText_;

  chopfractal::ui::ExportView exportView_;
  chopfractal::ui::MidiOptionsView midiView_;
  juce::Rectangle<int> formatArea_, midiArea_, dragArea_, estimateArea_, tempoArea_;
  juce::String estimateText_, tempoText_, hintText_;
  bool syncing_ = false;
};
