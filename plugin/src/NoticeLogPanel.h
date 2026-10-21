#pragma once
// The notice log popup (F05): newest first, one row per notice as "HH:MM:SS  [!] text", with Clear and Copy buttons.
// Shown in a CallOutBox from the status bar.
#include <juce_gui_basics/juce_gui_basics.h>

#include "NoticeLog.h"

class NoticeLogPanel : public juce::Component, private juce::ListBoxModel {
 public:
  explicit NoticeLogPanel(NoticeLog&);
  void resized() override;
  void paint(juce::Graphics&) override;
  void refresh();                                   // re-read the log
  int rowCount() const { return static_cast<int>(rows_.size()); }
  juce::String rowText(int row) const;               // "12:30:05  [!] text", newest first
  juce::String allText() const;                      // what Copy puts on the clipboard
  void clearLog();
  void copyAll();
  juce::Button& clearButton() { return clear_; }
  juce::Button& copyButton() { return copy_; }

 private:
  int getNumRows() override { return static_cast<int>(rows_.size()); }
  void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
  juce::String getNameForRow(int row) override { return rowText(row); }

  NoticeLog& log_;
  std::vector<LogEntry> rows_;  // newest first
  juce::ListBox list_{"Notices", this};
  juce::TextButton clear_{"Clear"}, copy_{"Copy"};
};
