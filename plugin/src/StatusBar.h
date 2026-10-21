#pragma once
// Bottom status bar (F05): the latest message on one elided line (severity prefix, click or Enter opens the notice log),
// then, right to left, the version and tag, the seed, the Cancel Export button while exporting, the meter with its CLIP
// chip, the voice count and the sync box (tempo, meter, transport and whether they are the host's or fallbacks).
#include <functional>

#include "EditorPanels.h"
#include "MeterStrip.h"
#include "NoticeLogPanel.h"
#include "StatusReadout.h"

class StatusBar : public EditorPanelBase {
 public:
  StatusBar(EditorModel&, EditorCommands&);
  ~StatusBar() override;
  void resized() override;
  void paint(juce::Graphics&) override;
  void setScale(float s);
  void refreshReadouts();  // the editor's tick: seed, sync box, voices; repaints only what changed

  // The status line as shown, with its severity prefix.
  juce::String displayedText() const { return shown_; }
  // Opens the notice log popup (also what a click on the status text does) and returns its content for tests.
  NoticeLogPanel* showLog();
  MeterStrip& meter() { return meter_; }
  StatusReadout& voiceBox() { return voices_; }
  StatusReadout& syncBox() { return sync_; }
  bool compact() const { return compact_; }
  // F19: the "?" button opens the command palette; the Quick start button shows while the checklist has not been hidden for good.
  std::function<void()> onPalette, onQuickStart;
  juce::TextButton& paletteButton() { return palette_; }
  juce::TextButton& quickStartButton() { return quickStart_; }
  void setQuickStartVisible(bool);

 private:
  class MessageButton : public juce::Button {
   public:
    MessageButton() : juce::Button("Status messages") {}
    void paintButton(juce::Graphics&, bool over, bool down) override;
    juce::String text;
    juce::Colour colour;
    float scale = 1.0f;
  };
  void modelChanged(Change) override;
  void lookAndFeelChanged() override;
  void updateMessage();

  MessageButton message_;
  MeterStrip meter_;
  StatusReadout voices_{"Active voices"}, sync_{"Tempo and meter"};
  juce::TextButton palette_{"?"}, quickStart_{"Quick start"};
  juce::TextButton cancelExport_{"Cancel Export"};  // visible only while an export runs
  juce::String shown_, lastSeed_;
  juce::Component::SafePointer<NoticeLogPanel> log_;
  juce::Component::SafePointer<juce::CallOutBox> box_;
  float scale_ = 1.0f;
  bool compact_ = false;
  juce::Rectangle<int> seedArea_, versionArea_;
};
