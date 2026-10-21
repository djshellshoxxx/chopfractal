#pragma once
// Per-hit inspector drawer (G2): the selected hit or hits as numbers: source lane, role, level, pan, pitch, probability,
// retrigger, reverse, lock, mute, plus Duplicate, Delete, Reset and a shortcut to the effects rack. Reads
// EditorModel::inspector() and writes through the editor commands; every change is one undo step.
#include <functional>

#include "EditorPanels.h"
#include "ParamRow.h"

class InspectorPanel : public EditorPanelBase {
 public:
  InspectorPanel(EditorModel&, EditorCommands&);
  void resized() override;
  void paint(juce::Graphics&) override;
  std::function<void()> onOpenFx;

 private:
  void modelChanged(Change) override;
  void refresh();
  void rebuildLaneItems();
  std::vector<chopfractal::EventId> ids() const { return model_.targetHits(); }
  void setFlag(juce::ToggleButton& b, bool mixed, bool value, const juce::String& base);

  juce::Viewport viewport_;
  juce::Component body_;
  juce::Label title_, subtitle_, refusal_, sourceLabel_, roleLabel_;
  juce::ComboBox source_, role_;
  ParamRow level_, pan_, pitch_, probability_, retrigger_;
  juce::ToggleButton reverse_{"Reverse"}, lock_{"Lock"}, mute_{"Mute"};
  juce::TextButton duplicate_{"Duplicate"}, delete_{"Delete"}, reset_{"Reset"}, effects_{"Effects"};
  chopfractal::ui::InspectorView view_;
  int laneItems_ = -1;
  bool syncing_ = false;
};
