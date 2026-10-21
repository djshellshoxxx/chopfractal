#pragma once
// Effects rack drawer (G2): the seven per-hit effects (Filter, Glide, Crunch, Stutter, Roll, Echo, Trem-Pan) for the selected
// hits. Each effect has an on switch, an intensity and its own controls; the hit's chance sits at the bottom. Reads
// EditorModel::fxRack() and writes through EditorCommands::setHitsFxState, so every change is one undo step.
#include <array>
#include <map>
#include <memory>

#include "EditorPanels.h"
#include "ParamRow.h"

class FxRackPanel : public EditorPanelBase {
 public:
  FxRackPanel(EditorModel&, EditorCommands&);
  void resized() override;
  void paint(juce::Graphics&) override;

 private:
  struct Control {
    chopfractal::ui::FxControlSpec spec;
    std::unique_ptr<ParamRow> slider;
    std::unique_ptr<juce::Label> label;
    std::unique_ptr<juce::ComboBox> choice;
    std::unique_ptr<juce::ToggleButton> toggle;
    juce::Component* component() const;
  };
  struct Section {
    chopfractal::ui::FxModule module;
    std::unique_ptr<juce::ToggleButton> header;
    std::vector<Control> controls;
    bool expanded = false;
  };
  void modelChanged(Change) override;
  void refresh();
  void layoutBody();
  void setModule(int index, bool on);
  void setControl(int index, const std::string& id, double value);

  juce::Viewport viewport_;
  juce::Component body_;
  juce::Label title_, refusal_;
  std::array<Section, chopfractal::ui::kFxModuleCount> sections_;
  ParamRow chance_;
  juce::TextButton copy_{"Copy FX"}, paste_{"Paste FX"}, reset_{"Reset FX"};
  chopfractal::ui::FxRackView view_;
  std::map<int, chopfractal::ui::HitFx> remembered_;  // per module: the state it had when it was switched off (one toggle back)
  chopfractal::ui::HitFx clipboard_;
  bool haveClipboard_ = false, syncing_ = false;
};
