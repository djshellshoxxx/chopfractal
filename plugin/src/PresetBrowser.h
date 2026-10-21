#pragma once
// Preset browser drawer (docs/specs/roadmap/F03, gui-redesign 7.6): search, All / Factory / User / Favorites scopes, a
// list of break styles with a star for favorites, a details block for the selected preset and Load, Save as, Rename and
// Delete. Up / Down browse and preview without applying; Enter or double-click loads. Factory entries are read-only.
#include <chopfractal/plugin_ui_adapter/preset_browser.hpp>
#include <functional>
#include <vector>

#include "EditorPanels.h"
#include "PresetManager.h"

class PresetBrowser : public EditorPanelBase, private juce::ListBoxModel, private juce::KeyListener, private juce::TextEditor::Listener {
 public:
  PresetBrowser(EditorModel&, EditorCommands&);
  ~PresetBrowser() override;

  // Wired by the editor: applying goes through the feature panel (it also stops Evolve and shows the cadence), capturing needs
  // the Fractal and Evolve widgets.
  std::function<bool(const chopfractal::presets::Preset&, bool generate)> onApply;
  std::function<bool()> onUndo;
  std::function<chopfractal::presets::Preset(const juce::String& name)> onCapture;
  std::function<void(const juce::String& name)> onApplied;  // the top bar shows this name

  void reload();  // re-reads the preset folder and the favorites
  void focusSearch();
  void paint(juce::Graphics&) override;
  void resized() override;

  // ---- state and actions (public so tests and the command palette drive the same paths as the buttons) ----
  const std::vector<chopfractal::ui::PresetItem>& items() const { return items_; }
  const std::vector<int>& visibleRows() const { return rows_; }  // indices into items()
  chopfractal::ui::PresetScope scope() const { return scope_; }
  void setScope(chopfractal::ui::PresetScope);
  void setQuery(const juce::String&);
  juce::String query() const { return search_.getText(); }
  int selectedIndex() const { return selected_; }  // into items(), -1 none
  juce::String selectedKey() const;
  bool selectKey(const juce::String& key);
  bool moveSelection(int delta);                       // Up / Down: previews, never applies
  bool loadSelected(bool generate = false);            // Enter / Load
  bool loadKey(const juce::String& key, bool generate = false);
  bool toggleFavorite(const juce::String& key);
  bool saveCurrent(const juce::String& name, bool overwrite);  // false: invalid name or exists without overwrite
  bool renameSelected(const juce::String& newName);
  bool deleteSelected();
  void askSave();
  void askRename();
  void askDelete();
  bool undoLast();
  juce::String currentName() const { return currentName_; }
  bool applyAndGenerate() const { return autoGenerate_.getToggleState(); }
  juce::String detailsText() const;  // the text of the details block for the selected preset

 private:
  void modelChanged(Change) override {}
  void lookAndFeelChanged() override;
  int getNumRows() override { return static_cast<int>(rows_.size()); }
  void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
  juce::String getNameForRow(int row) override;
  void listBoxItemClicked(int row, const juce::MouseEvent&) override;
  void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
  void selectedRowsChanged(int lastRowSelected) override;
  void returnKeyPressed(int) override { loadSelected(applyAndGenerate()); }
  bool keyPressed(const juce::KeyPress&, juce::Component*) override;
  void textEditorTextChanged(juce::TextEditor&) override;
  void textEditorReturnKeyPressed(juce::TextEditor&) override { loadSelected(applyAndGenerate()); }
  void refreshRows(bool keepSelection);
  void updateButtons();
  const chopfractal::presets::Preset* presetOf(int itemIndex) const;
  int rowOfItem(int itemIndex) const;

  std::vector<PresetManager::Entry> entries_;
  std::vector<chopfractal::ui::PresetItem> items_;
  std::vector<int> rows_;
  chopfractal::ui::PresetScope scope_ = chopfractal::ui::PresetScope::All;
  int selected_ = -1;
  juce::String currentName_;
  bool settingSelection_ = false;

  juce::TextEditor search_;
  juce::TextButton all_{"All"}, factory_{"Factory"}, user_{"User"}, favs_{"Favorites"};
  juce::ListBox list_{"Presets", this};
  juce::TextButton load_{"Load"}, save_{"Save as"}, rename_{"Rename"}, delete_{"Delete"}, favorite_{"Favorite"}, undo_{"Undo preset"};
  juce::ToggleButton autoGenerate_{"Apply and generate"};
  juce::Rectangle<int> detailsArea_;
};
