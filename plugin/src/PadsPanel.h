#pragma once
#include "ParamTip.h"
// Pads drawer (F07 section 12): the on-screen 4x4 pads for the chops (velocity from where the pad is pressed), Poly / Mono /
// Gate and the velocity curve (host parameters), base note, MIDI channel, record take to the grid with quantize and merge,
// choke groups, and a MIDI display with Learn. Labels, flags and titles come from ui::buildPadView().
#include <array>
#include <chopfractal/plugin_ui_adapter/pads_view.hpp>

#include "DrawerPanel.h"
#include "PerformCommands.h"

class PadGrid;

// One pad. Mouse down plays the note (velocity 127 at the top edge, 40 at the bottom) and mouse up or exit releases it; Space
// and Return play it from the keyboard; right-click opens the choke group menu.
class PadCellComponent : public juce::Component, public juce::SettableTooltipClient {
 public:
  PadCellComponent(PerformCommands&, int index);
  ~PadCellComponent() override;
  void set(const chopfractal::ui::PadCell&);
  const chopfractal::ui::PadCell& cell() const { return cell_; }
  bool held() const { return held_; }
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;
  void mouseExit(const juce::MouseEvent&) override;
  bool keyPressed(const juce::KeyPress&) override;
  bool keyStateChanged(bool isKeyDown) override;
  void focusLost(FocusChangeType) override { release(); }

 private:
  void press(int velocity);
  void release();
  void chokeMenu();
  PerformCommands& perform_;
  chopfractal::ui::PadCell cell_;
  bool held_ = false;
  int heldNote_ = -1;
};

class PadGrid : public juce::Component {
 public:
  PadGrid(PerformCommands&);
  void set(const chopfractal::ui::PadView&);
  PadCellComponent& pad(int i) { return *pads_[static_cast<std::size_t>(i)]; }
  void paint(juce::Graphics&) override;
  void resized() override;

 private:
  std::array<std::unique_ptr<PadCellComponent>, chopfractal::ui::kPadsPerPage> pads_;
  juce::String emptyText_;
};

class PadsPanel : public DrawerPanel {
 public:
  PadsPanel(EditorModel&, EditorCommands&, PerformCommands&, juce::AudioProcessorValueTreeState&);
  ~PadsPanel() override;
  PadGrid& grid() { return grid_; }
  const chopfractal::ui::PadView& view() const { return view_; }
  bool learning() const { return learn_.getToggleState(); }
  int lastMidiNote() const { return lastNote_; }

 private:
  int layoutBody(int width) override;
  void tick() override;
  void visibilityChanged() override;
  void applyLearn(const std::array<std::uint32_t, 4>& activity);

  PerformCommands& perform_;
  chopfractal::ui::PadView view_;
  PadGrid grid_;
  int page_ = 0;
  bool updating_ = false;
  int lastNote_ = -1;
  std::array<int, 128> hold_{};
  juce::Label pageLabel_, modeLabel_, curveLabel_, baseLabel_, channelLabel_, quantizeLabel_, mergeLabel_, midiIn_, hint_;
  juce::ComboBox page_c_, mode_, curve_, channel_, quantize_, merge_;
  juce::Slider baseNote_;
  juce::ToggleButton record_{"Record Take"}, learn_{"Learn"};
  juce::TextButton discard_{"Discard Take"};
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttach_, curveAttach_;
  std::unique_ptr<ParamTip> tips_;
};
