#pragma once
// Sound drawer: the host parameters that shape how a pattern is built and how it sounds and that have no knob in the macro strip.
// Break engine (profile, backbeat, anchors, humanize, fill), Groove Lab, the Stutter, Roll, Echo and Tremolo-Pan generation
// effects, slice polish, and the level match and safety limiter settings. Every control is attached to the parameter tree, so
// automation, presets and a restored project move it, and its tooltip carries the value line from host::describeParam.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>
#include <vector>

#include "ParamTip.h"

class SoundPanel : public juce::Component {
 public:
  explicit SoundPanel(juce::AudioProcessorValueTreeState& apvts);
  void resized() override;

  // Parameter ids this panel binds (tests check that every manifest parameter is reachable from some panel).
  std::vector<std::string> boundIds() const;
  juce::Component* controlFor(const std::string& paramId) const;

 private:
  struct Body : juce::Component {
    SoundPanel& owner;
    explicit Body(SoundPanel& o) : owner(o) {}
    void paint(juce::Graphics& g) override;
  };
  struct Row {
    std::string id;
    std::unique_ptr<juce::Label> label;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::SliderParameterAttachment> sliderAttachment;
    std::unique_ptr<juce::ComboBoxParameterAttachment> comboAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> toggleAttachment;
    juce::Component* control() const;
  };
  struct Section {
    juce::String title;
    std::vector<std::size_t> rows;
    juce::Rectangle<int> area;
  };
  void addSection(const char* title, std::initializer_list<const char*> ids);
  void layoutBody(int width);

  juce::AudioProcessorValueTreeState& apvts_;
  ParamTip tips_;
  juce::Viewport viewport_;
  Body body_{*this};
  std::vector<Row> rows_;
  std::vector<Section> sections_;
};
