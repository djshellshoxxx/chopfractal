#pragma once
// Circuit Drift Labs look: knobs with a value arc and a drift tick, buttons, toggles, combos, tooltips, scrollbars,
// the chamfered window frame, stepped trace separators and the stencil title. Colours come from cdl::pal(); call
// applyTheme() after cdl::setCurrentTheme() and then sendLookAndFeelChange() on the editor.
#include "CdlTheme.h"

namespace cdl {
// A rotary slider property that holds the normalised value the drift tick marks (the value when the preset was applied).
inline const char* kDriftProperty = "cdlDrift";

// Window background: bg with the top-left corner cut by kNotch and a 2 px spray line along the cut.
void paintWindowFrame(juce::Graphics&, juce::Rectangle<int> bounds);
// 1 px trace with one 45 degree step, from x0 to x1 at y; the step sits at stepAt (absolute x).
void paintTrace(juce::Graphics&, float x0, float x1, float y, float stepAt);
// Plugin name: stencil title with the spray offset second pass.
void paintStencilTitle(juce::Graphics&, const juce::String& text, juce::Rectangle<int> area, float scale);
// Section header: spray bar (13 px tall hand mark) plus uppercase label.
void paintSectionLabel(juce::Graphics&, const juce::String& text, juce::Rectangle<int> area);  // scaled with cdl::uiScale()

class CdlLookAndFeel : public juce::LookAndFeel_V4 {
 public:
  CdlLookAndFeel();
  void applyTheme();

  void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
  void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max, juce::Slider::SliderStyle, juce::Slider&) override;
  void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool hover, bool down) override;
  void drawButtonText(juce::Graphics&, juce::TextButton&, bool hover, bool down) override;
  void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool hover, bool down) override;
  void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
  void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
  void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
  void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>&, bool sep, bool active, bool highlighted, bool ticked, bool hasSub, const juce::String& text,
                         const juce::String& shortcut, const juce::Drawable*, const juce::Colour*) override;
  void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
  int getDefaultScrollbarWidth() override { return 10; }
  void drawTextEditorOutline(juce::Graphics&, int w, int h, juce::TextEditor&) override;
  void fillTextEditorBackground(juce::Graphics&, int w, int h, juce::TextEditor&) override;
  juce::Rectangle<int> getTooltipBounds(const juce::String&, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
  void drawTooltip(juce::Graphics&, const juce::String&, int w, int h) override;
  juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
  juce::Font getComboBoxFont(juce::ComboBox&) override { return font(FontRole::Body, uiScale()); }
  juce::Font getLabelFont(juce::Label&) override { return font(FontRole::Body, uiScale()); }
  juce::Font getPopupMenuFont() override { return font(FontRole::Body, uiScale()); }
};
}  // namespace cdl
