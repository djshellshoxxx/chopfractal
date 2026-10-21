#pragma once
// Shared base of the wave-two drawer panels (capture, tempo match, pads, scenes, outputs, arrangement, ...). A panel is a
// scrolling column of rows: derived classes add their widgets to body() and lay them out in layoutBody(), which returns the
// height the column needs; the viewport scrolls when the drawer is shorter. A 10 Hz timer runs only while the panel is
// showing and calls tick(). Panels read the EditorModel and call EditorCommands / PerformCommands, never the processor.
#include <functional>
#include <initializer_list>

#include <juce_gui_basics/juce_gui_basics.h>

#include "CdlLookAndFeel.h"
#include "CdlTheme.h"
#include "EditorPanels.h"

namespace perform_ui {
// Vertical stacking helper: rows are cut from the top of the body, scaled with the shell.
struct Stack {
  int width = 0, y = 0, gap = 6, rowH = 26;
  explicit Stack(int w) : width(w), gap(scaled(6)), rowH(scaled(26)) {}
  static int scaled(int v) { return std::max(1, static_cast<int>(std::lround(static_cast<float>(v) * cdl::uiScale()))); }
  juce::Rectangle<int> row(int h = 0) {
    const int hh = h > 0 ? h : rowH;
    juce::Rectangle<int> r(0, y, width, hh);
    y += hh + gap;
    return r;
  }
  void space(int h) { y += h; }
  int height() const { return y; }
};

// Label on the left, control on the right.
inline void labelled(juce::Rectangle<int> r, juce::Label& label, juce::Component& control, int labelWidth) {
  label.setBounds(r.removeFromLeft(labelWidth));
  control.setBounds(r);
}
inline void styleLabel(juce::Label& l, const juce::String& text, bool muted = true) {
  l.setText(text, juce::dontSendNotification);
  l.setMinimumHorizontalScale(0.8f);
  l.setColour(juce::Label::textColourId, muted ? cdl::pal().inkMuted : cdl::pal().ink);
}
// Places the components in a grid of `cols` equal columns, one row of the stack per line; hidden components keep their cell.
inline void grid(Stack& s, std::initializer_list<juce::Component*> cs, int cols, int h = 0) {
  int col = 0;
  juce::Rectangle<int> r;
  const int gap = Stack::scaled(6);
  for (auto* c : cs) {
    if (col == 0) r = s.row(h);
    const int cellW = (r.getWidth() - gap * (cols - 1)) / cols;
    c->setBounds(r.getX() + col * (cellW + gap), r.getY(), cellW, r.getHeight());
    col = (col + 1) % cols;
  }
}
// Every control gets a one-clause tooltip (no final period) and an accessible title.
inline void describe(juce::Component& c, const juce::String& title, const juce::String& tip) {
  c.setTitle(title);
  if (auto* t = dynamic_cast<juce::SettableTooltipClient*>(&c)) t->setTooltip(tip);
}
}  // namespace perform_ui

class DrawerPanel : public EditorPanelBase, private juce::Timer {
 public:
  ~DrawerPanel() override { stopTimer(); }
  void resized() override {
    viewport_.setBounds(getLocalBounds());
    relayout();
  }
  void paint(juce::Graphics& g) override { g.fillAll(cdl::pal().panel); }
  void visibilityChanged() override {
    if (isVisible()) {
      startTimerHz(hz_);
      tick();
      relayout();
    } else {
      stopTimer();
    }
  }
  void lookAndFeelChanged() override { relayout(); }
  juce::Component& body() { return body_; }
  void relayout() {
    if (getWidth() <= 0) return;
    const int w = std::max(60, viewport_.getMaximumVisibleWidth());
    const int h = layoutBody(w);
    body_.setSize(w, std::max(h, viewport_.getMaximumVisibleHeight()));
  }
  // Runs one refresh now (tests and event handlers use it instead of waiting for the timer).
  void refreshNow() { tick(); }

 protected:
  DrawerPanel(EditorModel& m, EditorCommands& c, const char* id, int hz = 10) : EditorPanelBase(m, c, id), hz_(hz) {
    viewport_.setViewedComponent(&body_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(8);
    addAndMakeVisible(viewport_);
  }
  void add(std::initializer_list<juce::Component*> cs) {
    for (auto* c : cs) body_.addAndMakeVisible(*c);
  }
  virtual int layoutBody(int width) = 0;
  virtual void tick() {}
  // Section heading drawn by hand on the body (uppercase via the section style).
  struct Body : public juce::Component {
    void paint(juce::Graphics& g) override {
      for (const auto& h : headings) cdl::paintSectionLabel(g, h.text, h.area);
    }
    struct Heading {
      juce::String text;
      juce::Rectangle<int> area;
    };
    std::vector<Heading> headings;
  };
  juce::Rectangle<int> heading(perform_ui::Stack& s, const juce::String& text) {
    auto r = s.row(perform_ui::Stack::scaled(18));
    body_.headings.push_back({text, r});
    return r;
  }
  void beginLayout() { body_.headings.clear(); }

  Body body_;
  juce::Viewport viewport_;

 private:
  void timerCallback() override {
    if (isShowing()) tick();
  }
  int hz_;
};
