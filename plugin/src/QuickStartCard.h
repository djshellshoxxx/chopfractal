#pragma once
// First-run checklist card (F19): five steps that tick from real events, each with a button that does the step. Docked over the
// waveform while no loop is loaded; the status bar's Quick start button and the "Show quick start" command bring it back.
#include <array>
#include <functional>

#include "EditorPanels.h"
#include "QuickStartTracker.h"

class QuickStartCard : public EditorPanelBase {
 public:
  QuickStartCard(EditorModel&, EditorCommands&, QuickStartTracker&);

  std::function<void()> onExport;   // the editor opens the export flow
  std::function<void()> onChanged;  // visibility or the status bar button changed

  void setScale(float s);
  float scale() const { return scale_; }
  juce::Point<int> preferredSize() const;
  // The editor's 10 Hz tick, after the tracker observed the session: updates the rows and decides whether the card shows.
  void tick();
  bool wanted() const { return wanted_; }       // the card should be visible
  void showNow();                                // Quick start button / palette: shows it even with a loop loaded
  void skip();                                   // "Skip": hides it for this session
  void celebrate();                              // test hook: the "You are set" message is up
  bool celebrating() const { return celebrateTicks_ > 0; }
  void setHidden(bool hidden);                   // "Do not show again" (persisted through the tracker)
  juce::String rowTitle(int i) const;            // "Quick start step 1 of 5: Load a loop, done"
  juce::TextButton& stepButton(int i) { return rows_[static_cast<std::size_t>(i)].button; }
  juce::TextButton& skipButton() { return skip_; }
  juce::ToggleButton& hideToggle() { return hide_; }
  bool stepDone(int i) const { return tracker_.done(static_cast<QuickStep>(i)); }
  juce::String stepReason(int i) const;          // why the step's button is off, empty when it is on

  void paint(juce::Graphics&) override;
  void resized() override;

 private:
  struct Row {
    juce::TextButton button;
  };
  void modelChanged(Change) override {}
  void lookAndFeelChanged() override { repaint(); }
  void update(bool advance);
  void runStep(int i);
  void refreshRows();

  QuickStartTracker& tracker_;
  std::array<Row, kQuickSteps> rows_;
  juce::TextButton skip_{"Skip"};
  juce::ToggleButton hide_{"Do not show again"};
  float scale_ = 1.0f;
  bool wanted_ = false, forced_ = false, skipped_ = false, wasAllDone_ = false;
  int celebrateTicks_ = 0;
  std::array<juce::Rectangle<int>, kQuickSteps> rowAreas_;
};
