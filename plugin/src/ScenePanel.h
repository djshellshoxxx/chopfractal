#pragma once
// In-editor overlay for Evolve scenes (docs/specs/roadmap/F09-evolve-scenes.md): the scene list, per-scene settings,
// capture / record / play / keep / export. A view over ProjectSession; no sequencing logic lives here.
#include <chopfractal/composition/project_session.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include <memory>

#include "RulesPanel.h"  // StringListModel

class ChopFractalProcessor;

class ScenePanel : public juce::Component, private juce::Timer {
 public:
  explicit ScenePanel(ChopFractalProcessor& processor);
  ~ScenePanel() override { stopTimer(); }
  void open();
  void close();
  void paint(juce::Graphics& g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;
  juce::String statusText() const { return status_.getText(); }
  int rowCount() const { return static_cast<int>(rows_.size()); }

 private:
  void timerCallback() override { refresh(); }
  void refresh();
  void refreshForm();
  void formChanged();
  void runAndReport(const std::function<chopfractal::Status(chopfractal::composition::ProjectSession&)>& action);
  std::uint32_t selectedId() const;
  void exportScenes();

  ChopFractalProcessor& proc_;
  StringListModel model_;
  juce::ListBox list_;
  std::vector<juce::String> rows_;
  std::vector<std::uint32_t> ids_;
  std::uint32_t selected_ = 0;
  bool loading_ = false;
  juce::Label header_, status_, details_, nameCaption_, loopsCaption_, afterCaption_, targetCaption_;
  juce::TextEditor name_;
  juce::Slider loops_;
  juce::ComboBox after_, target_;
  juce::TextButton capture_{"Capture Scene"}, update_{"Update Scene"}, keep_{"Keep Scene"}, up_{"Move Up"}, down_{"Move Down"}, delete_{"Delete Scene"},
      clear_{"Clear All"}, export_{"Export Scenes..."}, stop_{"Stop Scenes"}, close_{"Close Scenes"};
  juce::ToggleButton rec_{"Rec Scenes"}, play_{"Play Scenes"}, wrap_{"Wrap"};
  std::shared_ptr<juce::FileChooser> chooser_;
};
