#include "CdlLookAndFeel.h"

#include <cmath>

namespace cdl {
namespace {
constexpr float kPi = juce::MathConstants<float>::pi;
juce::Colour brighten(juce::Colour c, bool on) { return on ? c.brighter(0.08f) : c; }
}  // namespace

void paintWindowFrame(juce::Graphics& g, juce::Rectangle<int> b) {
  const auto& p = pal();
  g.fillAll(p.edge.darker(0.6f));  // shows only in the cut corner
  juce::Path body;
  const float n = kNotch, w = static_cast<float>(b.getWidth()), h = static_cast<float>(b.getHeight());
  body.startNewSubPath(n, 0);
  body.lineTo(w, 0);
  body.lineTo(w, h);
  body.lineTo(0, h);
  body.lineTo(0, n);
  body.closeSubPath();
  g.setColour(p.bg);
  g.fillPath(body);
  g.setColour(p.spray);
  g.drawLine(0.0f, n, n, 0.0f, 2.0f);
}

void paintTrace(juce::Graphics& g, float x0, float x1, float y, float stepAt) {
  juce::Path t;
  t.startNewSubPath(x0, y + 0.5f);
  t.lineTo(stepAt - 3.0f, y + 0.5f);
  t.lineTo(stepAt, y + 3.5f);
  t.lineTo(x1, y + 3.5f);
  g.setColour(pal().edge);
  g.strokePath(t, juce::PathStrokeType(1.0f));
}

void paintStencilTitle(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> a, float s) {
  const auto f = font(FontRole::Title, s);
  const auto o = std::max(2, juce::roundToInt(3.0f * s));
  g.setFont(f);
  g.setColour(pal().spray);
  g.drawText(text.toUpperCase(), a.translated(o, o), juce::Justification::centredLeft, false);
  g.setColour(pal().ink);
  g.drawText(text.toUpperCase(), a, juce::Justification::centredLeft, false);
}

void paintSectionLabel(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> a) {
  g.setColour(pal().spray);
  g.fillRect(a.getX(), a.getCentreY() - 6, 3, 13);  // 13 px, not 12
  g.setColour(pal().sprayHi);
  g.setFont(font(FontRole::Section));
  g.drawText(text.toUpperCase(), a.withTrimmedLeft(9), juce::Justification::centredLeft, false);
}

CdlLookAndFeel::CdlLookAndFeel() { applyTheme(); }

void CdlLookAndFeel::applyTheme() {
  const auto& p = pal();
  using J = juce::LookAndFeel;
  auto set = [this](int id, juce::Colour c) { setColour(id, c); };
  set(juce::ResizableWindow::backgroundColourId, p.bg);
  set(juce::TextButton::buttonColourId, p.panel);
  set(juce::TextButton::buttonOnColourId, p.sprayWash);
  set(juce::TextButton::textColourOffId, p.ink);
  set(juce::TextButton::textColourOnId, p.sprayHi);
  set(juce::ToggleButton::textColourId, p.ink);
  set(juce::ToggleButton::tickColourId, p.spray);
  set(juce::ToggleButton::tickDisabledColourId, p.edgeHi);
  set(juce::Label::textColourId, p.ink);
  set(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
  set(juce::Label::outlineColourId, juce::Colours::transparentBlack);
  set(juce::Slider::thumbColourId, p.spray);
  set(juce::Slider::trackColourId, p.spray);
  set(juce::Slider::backgroundColourId, p.edge);
  set(juce::Slider::rotarySliderFillColourId, p.spray);
  set(juce::Slider::rotarySliderOutlineColourId, p.edge);
  set(juce::Slider::textBoxTextColourId, p.ink);
  set(juce::Slider::textBoxBackgroundColourId, p.bg);
  set(juce::Slider::textBoxOutlineColourId, p.edgeHi);
  set(juce::Slider::textBoxHighlightColourId, p.sprayWash);
  set(juce::ComboBox::backgroundColourId, p.panel);
  set(juce::ComboBox::textColourId, p.ink);
  set(juce::ComboBox::outlineColourId, p.edgeHi);
  set(juce::ComboBox::arrowColourId, p.inkMuted);
  set(juce::ComboBox::focusedOutlineColourId, p.spray);
  set(juce::PopupMenu::backgroundColourId, p.panel);
  set(juce::PopupMenu::textColourId, p.ink);
  set(juce::PopupMenu::headerTextColourId, p.inkMuted);
  set(juce::PopupMenu::highlightedBackgroundColourId, p.sprayWash);
  set(juce::PopupMenu::highlightedTextColourId, p.ink);
  set(juce::TextEditor::backgroundColourId, p.bg);
  set(juce::TextEditor::textColourId, p.ink);
  set(juce::TextEditor::outlineColourId, p.edgeHi);
  set(juce::TextEditor::focusedOutlineColourId, p.spray);
  set(juce::TextEditor::highlightColourId, p.sprayWash);
  set(juce::TextEditor::highlightedTextColourId, p.ink);
  set(juce::CaretComponent::caretColourId, p.spray);
  set(juce::ScrollBar::thumbColourId, p.edgeHi);
  set(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
  set(juce::TooltipWindow::backgroundColourId, p.panel);
  set(juce::TooltipWindow::textColourId, p.ink);
  set(juce::TooltipWindow::outlineColourId, p.edgeHi);
  set(juce::AlertWindow::backgroundColourId, p.panel);
  set(juce::AlertWindow::textColourId, p.ink);
  set(juce::AlertWindow::outlineColourId, p.edgeHi);
  set(juce::DocumentWindow::backgroundColourId, p.bg);
  set(juce::ListBox::backgroundColourId, p.panel);
  set(juce::ListBox::textColourId, p.ink);
  (void)sizeof(J);
}

void CdlLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) {
  const auto& p = pal();
  const auto b = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
  const float d = std::min(b.getWidth(), b.getHeight());
  const auto c = b.getCentre();
  const float ringR = d * 0.5f - 3.0f;
  const float bodyR = ringR - 5.0f;
  const float a0 = start - 0.0105f;  // hand mark: the arc starts about 0.6 degrees early
  const float aEnd = end;
  const float aVal = a0 + pos * (aEnd - a0);
  const bool enabled = s.isEnabled();
  const float thick = std::max(2.0f, d * 0.06f);

  juce::Path track;
  track.addCentredArc(c.x, c.y, ringR, ringR, 0.0f, a0, aEnd, true);
  g.setColour(p.edge.interpolatedWith(p.edgeHi, 0.35f));
  g.strokePath(track, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  if (pos > 0.002f) {
    juce::Path val;
    val.addCentredArc(c.x, c.y, ringR, ringR, 0.0f, a0, aVal, true);
    g.setColour(enabled ? p.spray : p.edgeHi);
    g.strokePath(val, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  }
  // Drift tick: 1 px rain mark at the value the preset was applied with.
  const auto drift = s.getProperties()[kDriftProperty];
  if (!drift.isVoid()) {
    const float a = a0 + juce::jlimit(0.0f, 1.0f, static_cast<float>(drift)) * (aEnd - a0);
    const float r0 = ringR - thick * 0.5f - 3.0f, r1 = ringR + thick * 0.5f + 3.0f;
    g.setColour(p.rain);
    g.drawLine(c.x + r0 * std::sin(a), c.y - r0 * std::cos(a), c.x + r1 * std::sin(a), c.y - r1 * std::cos(a), 1.0f);
  }
  juce::ColourGradient grad(p.knobTop, c.x, c.y - bodyR, p.knobBottom, c.x, c.y + bodyR, false);
  g.setGradientFill(grad);
  g.fillEllipse(c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
  g.setColour(s.isMouseOverOrDragging() ? p.edgeHi : p.edge);
  g.drawEllipse(c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);
  // Pointer
  const float pr0 = bodyR * 0.35f, pr1 = bodyR * 0.85f;
  g.setColour(enabled ? p.ink : p.inkMuted);
  g.drawLine(c.x + pr0 * std::sin(aVal), c.y - pr0 * std::cos(aVal), c.x + pr1 * std::sin(aVal), c.y - pr1 * std::cos(aVal), 2.0f);
  if (s.hasKeyboardFocus(false)) {
    g.setColour(p.edgeHi);
    g.drawEllipse(c.x - ringR - 3, c.y - ringR - 3, (ringR + 3) * 2, (ringR + 3) * 2, 1.0f);
  }
  (void)kPi;
}

void CdlLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style, juce::Slider& s) {
  const auto& p = pal();
  if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar) {
    LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, style, s);
    return;
  }
  const float cy = static_cast<float>(y) + static_cast<float>(h) * 0.5f;
  g.setColour(p.edge);
  g.fillRoundedRectangle(static_cast<float>(x), cy - 2.0f, static_cast<float>(w), 4.0f, 2.0f);
  g.setColour(s.isEnabled() ? p.spray : p.edgeHi);
  g.fillRoundedRectangle(static_cast<float>(x), cy - 2.0f, std::max(0.0f, pos - static_cast<float>(x)), 4.0f, 2.0f);
  g.setGradientFill(juce::ColourGradient(p.knobTop, pos, cy - 7, p.knobBottom, pos, cy + 7, false));
  g.fillEllipse(pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
  g.setColour(p.edgeHi);
  g.drawEllipse(pos - 7.0f, cy - 7.0f, 14.0f, 14.0f, 1.0f);
}

void CdlLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) {
  const auto& p = pal();
  auto r = b.getLocalBounds().toFloat().reduced(0.5f);
  const bool on = b.getToggleState();
  const bool primary = static_cast<bool>(b.getProperties()["cdlPrimary"]);
  juce::Colour fill = primary ? p.spray : (on ? p.sprayWash.overlaidWith(p.panel).interpolatedWith(p.panel, 0.0f) : p.panel);
  if (on && !primary) fill = p.panel.overlaidWith(p.sprayWash);
  if (hover) fill = fill.brighter(0.08f);
  if (down) fill = fill.darker(0.1f);
  if (!b.isEnabled()) fill = fill.withMultipliedAlpha(0.5f);
  g.setColour(fill);
  g.fillRoundedRectangle(r, 4.0f);
  g.setColour(primary ? p.spray : (on ? p.spray : p.edgeHi));
  g.drawRoundedRectangle(r, 4.0f, 1.0f);
  if (b.hasKeyboardFocus(false)) {
    g.setColour(p.ink);
    g.drawRoundedRectangle(r.expanded(2.0f), 5.0f, 1.0f);
  }
  (void)brighten;
}

juce::Font CdlLookAndFeel::getTextButtonFont(juce::TextButton&, int) { return font(FontRole::Button); }

void CdlLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
  const auto& p = pal();
  const bool primary = static_cast<bool>(b.getProperties()["cdlPrimary"]);
  g.setFont(primary ? font(FontRole::Button, 1.3f) : getTextButtonFont(b, b.getHeight()));
  juce::Colour c = primary ? p.onSpray : (b.getToggleState() ? p.sprayHi : p.ink);
  if (!b.isEnabled()) c = p.inkMuted;
  g.setColour(c);
  g.drawText(b.getButtonText(), b.getLocalBounds().reduced(6, 1), juce::Justification::centred, true);
}

void CdlLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) {
  const auto& p = pal();
  const auto r = b.getLocalBounds().toFloat();
  const float box = std::min(14.0f, r.getHeight() - 4.0f);
  const auto bx = juce::Rectangle<float>(r.getX() + 2.0f, r.getCentreY() - box * 0.5f, box, box);
  const bool on = b.getToggleState();
  g.setColour(on ? p.sprayWash.overlaidWith(p.panel) : p.panel);
  g.fillRoundedRectangle(bx, 2.0f);
  g.setColour(hover ? p.ink : (on ? p.spray : p.edgeHi));
  g.drawRoundedRectangle(bx, 2.0f, 1.0f);
  if (on) {  // check mark is the shape cue, colour is not the only one
    juce::Path t;
    t.startNewSubPath(bx.getX() + box * 0.22f, bx.getCentreY());
    t.lineTo(bx.getX() + box * 0.43f, bx.getBottom() - box * 0.26f);
    t.lineTo(bx.getRight() - box * 0.2f, bx.getY() + box * 0.26f);
    g.setColour(p.spray);
    g.strokePath(t, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  }
  g.setColour(b.isEnabled() ? (on ? p.sprayHi : p.ink) : p.inkMuted);
  g.setFont(font(FontRole::Body));
  g.drawText(b.getButtonText(), r.withTrimmedLeft(box + 10.0f), juce::Justification::centredLeft, true);
  if (b.hasKeyboardFocus(false)) {
    g.setColour(p.ink);
    g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
  }
}

void CdlLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box) {
  const auto& p = pal();
  const auto r = juce::Rectangle<float>(0, 0, static_cast<float>(w), static_cast<float>(h)).reduced(0.5f);
  g.setColour(down ? p.panel.brighter(0.08f) : p.panel);
  g.fillRoundedRectangle(r, 4.0f);
  g.setColour(box.hasKeyboardFocus(true) ? p.spray : p.edgeHi);
  g.drawRoundedRectangle(r, 4.0f, 1.0f);
  juce::Path chev;
  const float cx = static_cast<float>(w) - 12.0f, cy = static_cast<float>(h) * 0.5f;
  chev.startNewSubPath(cx - 4, cy - 2);
  chev.lineTo(cx, cy + 2);
  chev.lineTo(cx + 4, cy - 2);
  g.setColour(box.isEnabled() ? p.inkMuted : p.edgeHi);
  g.strokePath(chev, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void CdlLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
  label.setBounds(6, 1, box.getWidth() - 26, box.getHeight() - 2);
  label.setFont(getComboBoxFont(box));
}

void CdlLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
  g.fillAll(pal().panel);
  g.setColour(pal().edgeHi);
  g.drawRect(0, 0, w, h, 1);
}

void CdlLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool sep, bool active, bool highlighted, bool ticked, bool hasSub,
                                       const juce::String& text, const juce::String& shortcut, const juce::Drawable*, const juce::Colour* tc) {
  const auto& p = pal();
  if (sep) {
    g.setColour(p.edge);
    g.fillRect(area.getX() + 6, area.getCentreY(), area.getWidth() - 12, 1);
    return;
  }
  if (highlighted && active) {
    g.setColour(p.sprayWash.overlaidWith(p.panel));
    g.fillRect(area);
  }
  auto col = tc != nullptr ? *tc : p.ink;
  if (!active) col = p.inkMuted;
  g.setColour(col);
  g.setFont(font(FontRole::Body));
  auto r = area.reduced(10, 0);
  if (ticked) {
    g.setColour(p.spray);
    g.fillEllipse(static_cast<float>(area.getX()) + 4.0f, static_cast<float>(area.getCentreY()) - 2.0f, 4.0f, 4.0f);
    g.setColour(col);
  }
  g.drawText(text, r.withTrimmedRight(shortcut.isNotEmpty() ? 60 : 0), juce::Justification::centredLeft, true);
  if (shortcut.isNotEmpty()) {
    g.setColour(p.inkMuted);
    g.setFont(font(FontRole::Hint));
    g.drawText(shortcut, r, juce::Justification::centredRight, true);
  }
  if (hasSub) {
    g.setColour(p.inkMuted);
    g.drawText(">", r, juce::Justification::centredRight, false);
  }
}

void CdlLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int ts, int tsz, bool over, bool down) {
  const auto& p = pal();
  g.setColour(p.edge.withAlpha(0.5f));
  g.fillRect(x, y, w, h);
  g.setColour(down || over ? p.inkMuted : p.edgeHi);
  const auto thumb = vertical ? juce::Rectangle<int>(x + 2, y + ts, w - 4, tsz) : juce::Rectangle<int>(x + ts, y + 2, tsz, h - 4);
  g.fillRoundedRectangle(thumb.toFloat(), 2.0f);
}

void CdlLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int w, int h, juce::TextEditor& e) {
  g.setColour(e.findColour(juce::TextEditor::backgroundColourId));
  g.fillRoundedRectangle(0, 0, static_cast<float>(w), static_cast<float>(h), 2.0f);
}

void CdlLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int w, int h, juce::TextEditor& e) {
  if (!e.isEnabled()) return;
  g.setColour(e.hasKeyboardFocus(true) && !e.isReadOnly() ? pal().spray : pal().edgeHi);
  g.drawRoundedRectangle(0.5f, 0.5f, static_cast<float>(w) - 1.0f, static_cast<float>(h) - 1.0f, 2.0f, 1.0f);
}

juce::Rectangle<int> CdlLookAndFeel::getTooltipBounds(const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) {
  juce::AttributedString as;
  as.append(text, font(FontRole::Body));
  juce::TextLayout tl;
  tl.createLayout(as, 280.0f);
  const int w = static_cast<int>(std::ceil(tl.getWidth())) + 18;
  const int h = static_cast<int>(std::ceil(tl.getHeight())) + 14;
  return juce::Rectangle<int>(pos.x > parent.getCentreX() ? pos.x - w - 12 : pos.x + 18, pos.y > parent.getCentreY() ? pos.y - h - 6 : pos.y + 20, w, h)
      .constrainedWithin(parent);
}

void CdlLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int w, int h) {
  const auto& p = pal();
  g.setColour(p.panel);
  g.fillRoundedRectangle(0, 0, static_cast<float>(w), static_cast<float>(h), 4.0f);
  g.setColour(p.edgeHi);
  g.drawRoundedRectangle(0.5f, 0.5f, static_cast<float>(w) - 1.0f, static_cast<float>(h) - 1.0f, 4.0f, 1.0f);
  juce::AttributedString as;
  as.append(text, font(FontRole::Body), p.ink);
  as.draw(g, juce::Rectangle<float>(9.0f, 7.0f, static_cast<float>(w) - 18.0f, static_cast<float>(h) - 14.0f));
}
}  // namespace cdl
