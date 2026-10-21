#pragma once
// Command palette overlay (F19): Ctrl/Cmd+K or the "?" button in the status bar. A search box, up to eight visible rows
// (Category, Name, value or shortcut) and a footer line. Enter runs the selected command; a parameter row turns the query into
// "<name> " and takes a number (or lists its choices); a disabled row says why in the footer. Esc or a click outside closes it.
#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

#include "CommandRegistry.h"
#include "QuickStartTracker.h"

class ChopFractalEditor;

class CommandPalette : public juce::Component, private juce::ListBoxModel, private juce::KeyListener, private juce::TextEditor::Listener {
 public:
  CommandPalette(ChopFractalEditor&, CommandRegistry&, QuickStartTracker&);
  ~CommandPalette() override;

  struct Row {
    const Command* cmd = nullptr;
    int choice = -1;                    // a choice sub-row of a parameter: the index it sets
    std::string title, right, reason;   // `reason` non-empty: disabled
    std::vector<std::uint8_t> hits;     // matched title characters, drawn bold
    bool header = false;                // "Recent" / "Suggested"
  };

  void open();
  void close();
  bool isOpen() const { return isVisible(); }
  juce::Rectangle<int> panelBounds() const { return panel_; }

  // ---- state and actions (public so tests drive the same paths as the keys) ----
  void setQuery(const juce::String&);
  juce::String query() const { return search_.getText(); }
  const std::vector<Row>& rows() const { return rows_; }
  int selectedRow() const { return selected_; }
  juce::String footer() const { return footerText_; }
  bool footerIsError() const { return footerError_; }
  bool moveSelection(int delta);
  bool runSelected();  // Enter
  juce::TextEditor& searchBox() { return search_; }
  juce::ListBox& list() { return list_; }

  void paint(juce::Graphics&) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent&) override;
  bool keyPressed(const juce::KeyPress&) override;
  void lookAndFeelChanged() override { repaint(); }

 private:
  int getNumRows() override { return static_cast<int>(rows_.size()); }
  void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
  juce::String getNameForRow(int row) override;
  juce::String getTooltipForRow(int row) override;
  void listBoxItemClicked(int row, const juce::MouseEvent&) override;
  void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
  bool keyPressed(const juce::KeyPress&, juce::Component*) override;
  void textEditorTextChanged(juce::TextEditor&) override { rebuild(); }

  void rebuild();
  void fillEmptyQuery();
  void fillArgument(const Command&, const std::string& arg);
  void fillSearch(const std::string& q);
  void select(int row);
  void setFooter(const juce::String&, bool error);
  void defaultFooter();
  void remember(const Command&);
  int firstSelectable(int from, int step) const;

  ChopFractalEditor& ed_;
  CommandRegistry& registry_;
  QuickStartTracker& tracker_;
  juce::TextEditor search_;
  juce::ListBox list_{"Command results", this};
  std::vector<Row> rows_;
  int selected_ = -1;
  const Command* argCmd_ = nullptr;
  std::string argText_;
  juce::String footerText_;
  bool footerError_ = false;
  juce::Rectangle<int> panel_;
  bool rebuilding_ = false;
  juce::Component::SafePointer<juce::Component> returnFocusTo_;
};
