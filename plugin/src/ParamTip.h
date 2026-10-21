#pragma once
// Tooltips of host-parameter controls (F04): the one-clause help first, then a line with the value, range and default from
// host::describeParam. The value line is rebuilt when the pointer arrives or moves over the control, so it never lags a drag,
// an automation move or a preset load. Controls keep the help alone until the parameter exists in the tree.
#include <chopfractal/plugin_host_adapter/parameters.hpp>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

class ParamTip : public juce::MouseListener {
 public:
  explicit ParamTip(juce::AudioProcessorValueTreeState& apvts) : apvts_(apvts) {}
  ~ParamTip() override {
    for (auto& e : entries_)
      if (e.comp != nullptr) e.comp->removeMouseListener(this);
  }
  // Binds `c` (a Slider, Button or ComboBox) to the parameter `id`; the tooltip is applied at once and refreshed on mouse events.
  void bind(juce::Component& c, const char* id, const juce::String& help) {
    if (c.getComponentID().isEmpty()) c.setComponentID(juce::String("param:") + id);  // lets tests and tools find the control of a parameter
    entries_.push_back({juce::Component::SafePointer<juce::Component>(&c), id, help});
    c.addMouseListener(this, true);
    refresh(entries_.back());
  }
  // The tooltip text for a parameter at a plain value (also used by tests).
  static juce::String text(const juce::String& help, const chopfractal::host::ParamDef& def, double plain) {
    return help + "\n" + juce::String::fromUTF8(chopfractal::host::describeParam(def, plain).c_str());
  }

 private:
  struct Entry {
    juce::Component::SafePointer<juce::Component> comp;  // the control may be destroyed before this object
    const char* id;
    juce::String help;
  };
  void refresh(const Entry& e) {
    const auto* def = chopfractal::host::findParam(e.id);
    auto* client = dynamic_cast<juce::SettableTooltipClient*>(e.comp.getComponent());
    if (def == nullptr || client == nullptr) return;
    double plain = def->defaultValue;
    if (auto* p = apvts_.getParameter(e.id)) plain = p->convertFrom0to1(p->getValue());
    client->setTooltip(text(e.help, *def, plain));
  }
  void refreshFor(const juce::MouseEvent& ev) {
    for (const auto& e : entries_)
      if (e.comp != nullptr && (e.comp == ev.eventComponent || e.comp->isParentOf(ev.eventComponent))) refresh(e);
  }

 public:
  void mouseEnter(const juce::MouseEvent& ev) override { refreshFor(ev); }
  void mouseMove(const juce::MouseEvent& ev) override { refreshFor(ev); }
  void mouseDrag(const juce::MouseEvent& ev) override { refreshFor(ev); }
  void mouseUp(const juce::MouseEvent& ev) override { refreshFor(ev); }
  void mouseWheelMove(const juce::MouseEvent& ev, const juce::MouseWheelDetails&) override { refreshFor(ev); }


 private:
  juce::AudioProcessorValueTreeState& apvts_;
  std::vector<Entry> entries_;
};
