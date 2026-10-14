#pragma once
// Performance pads (docs/specs/roadmap/F07-performance-pads.md): a 4x4 grid of on-screen pads that play the chops like
// MIDI notes, the pad settings, and take recording. A view over ProjectSession settings and the processor's PadInput.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <initializer_list>
#include <memory>
#include <vector>

class ChopFractalProcessor;

class PadButton : public juce::Button {
 public:
  PadButton(ChopFractalProcessor& processor, int index);
  void setNote(int note, const juce::String& caption, bool available);
  int note() const { return note_; }
  void light() { lit_ = 8; }
  bool decayLight() { return lit_ > 0 && --lit_ == 0; }
  bool isLit() const { return lit_ > 0; }
  void paintButton(juce::Graphics& g, bool over, bool down) override;
  void mouseDown(const juce::MouseEvent& e) override;
  void buttonStateChanged() override;
  std::function<void(PadButton&)> onContext;  // right click: choke group menu

 private:
  ChopFractalProcessor& proc_;
  int index_;
  int note_ = -1;
  int velocity_ = 100;
  bool sounding_ = false;
  int lit_ = 0;
  juce::String caption_;
};

class PadPanel : public juce::Component, private juce::Timer {
 public:
  explicit PadPanel(ChopFractalProcessor& processor);
  ~PadPanel() override;
  void open();
  void close();
  void paint(juce::Graphics& g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;

 private:
  void timerCallback() override;
  void refreshPads();
  void settingsChanged();
  void showChokeMenu(PadButton& pad);
  static juce::String noteName(int note);

  ChopFractalProcessor& proc_;
  std::vector<std::unique_ptr<PadButton>> pads_;
  juce::ComboBox page_, mode_, curve_, channel_, quantize_, merge_;
  juce::Slider baseNote_;
  juce::ToggleButton record_{"Record"};
  juce::TextButton discard_{"Discard Take"}, close_{"Close Pads"};
  juce::Label hits_, empty_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttach_, curveAttach_;
  int chopCount_ = 0;
  bool loading_ = false;
};
