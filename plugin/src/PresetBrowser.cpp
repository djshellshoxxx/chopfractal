#include "PresetBrowser.h"

#include <chopfractal/plugin_host_adapter/parameters.hpp>
#include <cmath>
#include <initializer_list>

#include "CdlLookAndFeel.h"

namespace cf = chopfractal;
namespace {
juce::String choiceName(const char* id, double index) {
  const auto* d = cf::host::findParam(id);
  if (!d || !d->choices) return {};
  const auto items = juce::StringArray::fromTokens(d->choices, "|", "");
  return items[juce::jlimit(0, std::max(0, items.size() - 1), static_cast<int>(std::lround(index)))];
}
double paramOf(const cf::presets::Preset& p, const char* id, double fallback) {
  for (const auto& v : p.params)
    if (v.id == id) return v.value;
  return fallback;
}
juce::String pct(double v) { return juce::String(static_cast<int>(std::lround(v * 100.0))) + "%"; }
}  // namespace

PresetBrowser::PresetBrowser(EditorModel& m, EditorCommands& c) : EditorPanelBase(m, c, "PresetBrowser") {
  search_.setTitle("Search presets");
  search_.setTooltip("Type to filter by name, style or description, Up and Down browse, Enter loads");
  search_.setTextToShowWhenEmpty("Search presets", cdl::pal().inkMuted);
  search_.setSelectAllWhenFocused(true);
  search_.setEscapeAndReturnKeysConsumed(false);
  search_.addListener(this);
  search_.addKeyListener(this);
  addAndMakeVisible(search_);

  int group = 7101;
  for (auto* b : {&all_, &factory_, &user_, &favs_}) {
    b->setClickingTogglesState(true);
    b->setRadioGroupId(group);
    b->setTitle(b->getButtonText() + " presets");
    addAndMakeVisible(*b);
  }
  all_.setToggleState(true, juce::dontSendNotification);
  all_.setTooltip("Show every factory and user preset");
  factory_.setTooltip("Show only the built-in break styles");
  user_.setTooltip("Show only the presets you saved yourself");
  favs_.setTooltip("Show only the presets you starred");
  all_.onClick = [this] { setScope(cf::ui::PresetScope::All); };
  factory_.onClick = [this] { setScope(cf::ui::PresetScope::Factory); };
  user_.onClick = [this] { setScope(cf::ui::PresetScope::User); };
  favs_.onClick = [this] { setScope(cf::ui::PresetScope::Favorites); };

  list_.setRowHeight(40);
  list_.setTitle("Preset list");
  list_.setTooltip("Presets: Up and Down preview, Enter or double-click loads, click the star to favorite");
  list_.addKeyListener(this);
  addAndMakeVisible(list_);

  for (auto* b : {&load_, &save_, &rename_, &delete_, &favorite_, &undo_}) {
    b->setTitle(b->getButtonText());
    addAndMakeVisible(*b);
  }
  load_.setTooltip("Apply the selected preset to the controls; the pattern changes when you press Generate");
  save_.setTooltip("Save the current controls, seed, motif and rules as a new preset");
  rename_.setTooltip("Rename the selected preset of yours");
  delete_.setTooltip("Delete the selected preset of yours");
  favorite_.setTooltip("Star or unstar the selected preset");
  undo_.setTooltip("Put the controls back as they were before the last preset");
  load_.onClick = [this] { loadSelected(applyAndGenerate()); };
  save_.onClick = [this] { askSave(); };
  rename_.onClick = [this] { askRename(); };
  delete_.onClick = [this] { askDelete(); };
  favorite_.onClick = [this] { toggleFavorite(selectedKey()); };
  undo_.onClick = [this] { undoLast(); };
  autoGenerate_.setTitle("Apply and generate");
  autoGenerate_.setTooltip("Run Generate straight after loading a preset");
  addAndMakeVisible(autoGenerate_);
  reload();
}

PresetBrowser::~PresetBrowser() {
  search_.removeKeyListener(this);
  list_.removeKeyListener(this);
}

void PresetBrowser::lookAndFeelChanged() { search_.setTextToShowWhenEmpty("Search presets", cdl::pal().inkMuted); }

void PresetBrowser::reload() {
  const juce::String keep = selectedKey();
  entries_ = PresetManager::list();
  const juce::StringArray favs = PresetManager::favorites();
  items_.clear();
  for (const auto& e : entries_) {
    cf::ui::PresetItem it;
    it.key = e.key.toStdString();
    it.name = e.preset.name;
    it.category = e.preset.category;
    it.description = e.loadable ? e.preset.description : "Made by a newer ChopFractal and cannot be loaded";
    it.factory = e.factory;
    it.loadable = e.loadable;
    it.favorite = favs.contains(e.key);
    items_.push_back(std::move(it));
  }
  selected_ = -1;
  for (std::size_t i = 0; i < items_.size(); ++i)
    if (keep.isNotEmpty() && items_[i].key == keep.toStdString()) selected_ = static_cast<int>(i);
  refreshRows(true);
}

void PresetBrowser::refreshRows(bool keepSelection) {
  rows_ = cf::ui::filterPresets(items_, scope_, search_.getText().toStdString());
  if (!keepSelection || rowOfItem(selected_) < 0) selected_ = rows_.empty() ? -1 : (keepSelection ? selected_ : rows_.front());
  if (rowOfItem(selected_) < 0) selected_ = -1;
  list_.updateContent();
  settingSelection_ = true;
  if (selected_ >= 0) list_.selectRow(rowOfItem(selected_), false, true);
  else list_.deselectAllRows();
  settingSelection_ = false;
  list_.repaint();
  updateButtons();
  repaint();
}

int PresetBrowser::rowOfItem(int itemIndex) const {
  for (std::size_t r = 0; r < rows_.size(); ++r)
    if (rows_[r] == itemIndex) return static_cast<int>(r);
  return -1;
}

const cf::presets::Preset* PresetBrowser::presetOf(int i) const { return i >= 0 && i < static_cast<int>(entries_.size()) ? &entries_[static_cast<std::size_t>(i)].preset : nullptr; }

juce::String PresetBrowser::selectedKey() const { return selected_ >= 0 && selected_ < static_cast<int>(items_.size()) ? juce::String(items_[static_cast<std::size_t>(selected_)].key) : juce::String(); }

void PresetBrowser::focusSearch() { search_.grabKeyboardFocus(); }

void PresetBrowser::setScope(cf::ui::PresetScope s) {
  scope_ = s;
  juce::TextButton* b = s == cf::ui::PresetScope::Factory ? &factory_ : s == cf::ui::PresetScope::User ? &user_ : s == cf::ui::PresetScope::Favorites ? &favs_ : &all_;
  b->setToggleState(true, juce::dontSendNotification);
  refreshRows(true);
}

void PresetBrowser::setQuery(const juce::String& q) {
  search_.setText(q, false);
  refreshRows(true);
}

void PresetBrowser::textEditorTextChanged(juce::TextEditor&) { refreshRows(true); }

bool PresetBrowser::selectKey(const juce::String& key) {
  for (std::size_t i = 0; i < items_.size(); ++i)
    if (items_[i].key == key.toStdString()) {
      if (rowOfItem(static_cast<int>(i)) < 0) {  // hidden by the filter: show everything so the choice is visible
        search_.setText({}, false);
        setScope(cf::ui::PresetScope::All);
      }
      selected_ = static_cast<int>(i);
      settingSelection_ = true;
      list_.selectRow(rowOfItem(selected_), false, true);
      settingSelection_ = false;
      updateButtons();
      repaint();
      return true;
    }
  return false;
}

bool PresetBrowser::moveSelection(int delta) {
  const int row = cf::ui::stepRow(static_cast<int>(rows_.size()), rowOfItem(selected_), delta);
  if (row < 0) return false;
  selected_ = rows_[static_cast<std::size_t>(row)];
  settingSelection_ = true;
  list_.selectRow(row, false, true);
  settingSelection_ = false;
  updateButtons();
  repaint();
  return true;
}

bool PresetBrowser::loadKey(const juce::String& key, bool generate) { return selectKey(key) && loadSelected(generate); }

bool PresetBrowser::loadSelected(bool generate) {
  const auto* p = presetOf(selected_);
  if (!p || !items_[static_cast<std::size_t>(selected_)].loadable) {
    cmd_.say(p ? "\"" + juce::String(p->name) + "\" was made by a newer ChopFractal and cannot be loaded." : "Choose a preset first.");
    return false;
  }
  const bool ok = onApply ? onApply(*p, generate) : cmd_.applyPreset(*p, generate);
  if (ok) {
    currentName_ = juce::String(p->name);
    if (onApplied) onApplied(currentName_);
  }
  updateButtons();
  return ok;
}

bool PresetBrowser::toggleFavorite(const juce::String& key) {
  if (key.isEmpty()) return false;
  const bool now = !PresetManager::isFavorite(key);
  const auto st = PresetManager::setFavorite(key, now);
  if (!st.ok()) {
    cmd_.say("Could not write the preset folder: " + juce::String(st.error().message));
    return false;
  }
  reload();
  return true;
}

bool PresetBrowser::saveCurrent(const juce::String& rawName, bool overwrite) {
  const juce::String name = rawName.trim();
  const std::string bad = cf::ui::validatePresetName(name.toStdString());
  if (!bad.empty()) {
    cmd_.say(juce::String(bad));
    return false;
  }
  cf::presets::Preset p = onCapture ? onCapture(name) : cmd_.currentPreset(name, FractalUi{}, EvolveUi{});
  p.name = name.toStdString();
  const auto st = PresetManager::save(p, overwrite);
  if (!st.ok()) {
    cmd_.say(st.error().code == cf::ErrorCode::Conflict ? "A preset named \"" + name + "\" already exists. Replace it?" : "Could not write the preset folder: " + juce::String(st.error().message));
    return false;
  }
  cmd_.say("Preset \"" + name + "\" saved.");
  reload();
  selectKey("user:" + juce::String(cf::presets::fileNameFor(p.name)).upToLastOccurrenceOf(".cfpreset", false, false));
  return true;
}

bool PresetBrowser::renameSelected(const juce::String& rawName) {
  if (selected_ < 0 || items_[static_cast<std::size_t>(selected_)].factory) {
    cmd_.say("Factory presets cannot be renamed. Save a copy with Save as.");
    return false;
  }
  const juce::String name = rawName.trim();
  const std::string bad = cf::ui::validatePresetName(name.toStdString());
  if (!bad.empty()) {
    cmd_.say(juce::String(bad));
    return false;
  }
  const auto st = PresetManager::rename(entries_[static_cast<std::size_t>(selected_)].file, name);
  if (!st.ok()) {
    cmd_.say(st.error().code == cf::ErrorCode::Conflict ? "A preset named \"" + name + "\" already exists." : juce::String(st.error().message));
    return false;
  }
  const bool wasFav = PresetManager::isFavorite(selectedKey());
  if (wasFav) PresetManager::setFavorite(selectedKey(), false);
  cmd_.say("Preset renamed to \"" + name + "\".");
  reload();
  const juce::String newKey = "user:" + juce::String(cf::presets::fileNameFor(name.toStdString())).upToLastOccurrenceOf(".cfpreset", false, false);
  if (wasFav) PresetManager::setFavorite(newKey, true);
  reload();
  selectKey(newKey);
  return true;
}

bool PresetBrowser::deleteSelected() {
  if (selected_ < 0 || items_[static_cast<std::size_t>(selected_)].factory) {
    cmd_.say("Factory presets cannot be deleted.");
    return false;
  }
  const juce::String key = selectedKey();
  const auto file = entries_[static_cast<std::size_t>(selected_)].file;
  const auto st = PresetManager::remove(file);
  if (!st.ok()) {
    cmd_.say(juce::String(st.error().message));
    return false;
  }
  PresetManager::setFavorite(key, false);
  cmd_.say("Preset deleted.");
  selected_ = -1;
  reload();
  return true;
}

bool PresetBrowser::undoLast() {
  const bool ok = onUndo ? onUndo() : cmd_.undoPreset();
  if (ok) currentName_ = {};
  updateButtons();
  return ok;
}

void PresetBrowser::askSave() {
  auto* w = new juce::AlertWindow("Save preset", "Name for this preset:", juce::MessageBoxIconType::NoIcon, getTopLevelComponent());
  const auto* cur = presetOf(selected_);
  w->addTextEditor("name", juce::String(cf::ui::uniquePresetName(items_, cur && !items_[static_cast<std::size_t>(selected_)].factory ? cur->name : "My preset")));
  w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
  w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
  juce::Component::SafePointer<PresetBrowser> self(this);
  w->enterModalState(true, juce::ModalCallbackFunction::create([self, w](int r) {
                       if (r != 1 || !self) return;
                       const juce::String name = w->getTextEditorContents("name").trim();
                       bool exists = false;
                       for (const auto& it : self->items_) exists = exists || (!it.factory && juce::String(it.name).equalsIgnoreCase(name));
                       if (!exists) {
                         self->saveCurrent(name, false);
                         return;
                       }
                       juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                                        .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                                        .withTitle("Replace preset?")
                                                        .withMessage("A preset named \"" + name + "\" already exists. Replace it?")
                                                        .withButton("Replace")
                                                        .withButton("Cancel")
                                                        .withAssociatedComponent(self->getTopLevelComponent()),
                                                    [self, name](int rr) {
                                                      if (rr == 1 && self) self->saveCurrent(name, true);
                                                    });
                     }),
                     true);
}

void PresetBrowser::askRename() {
  if (selected_ < 0 || items_[static_cast<std::size_t>(selected_)].factory) {
    cmd_.say("Factory presets cannot be renamed. Save a copy with Save as.");
    return;
  }
  auto* w = new juce::AlertWindow("Rename preset", "New name:", juce::MessageBoxIconType::NoIcon, getTopLevelComponent());
  w->addTextEditor("name", juce::String(items_[static_cast<std::size_t>(selected_)].name));
  w->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
  w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
  juce::Component::SafePointer<PresetBrowser> self(this);
  w->enterModalState(true, juce::ModalCallbackFunction::create([self, w](int r) {
                       if (r == 1 && self) self->renameSelected(w->getTextEditorContents("name"));
                     }),
                     true);
}

void PresetBrowser::askDelete() {
  if (selected_ < 0 || items_[static_cast<std::size_t>(selected_)].factory) {
    cmd_.say("Factory presets cannot be deleted.");
    return;
  }
  juce::Component::SafePointer<PresetBrowser> self(this);
  juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                   .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                   .withTitle("Delete preset?")
                                   .withMessage("Delete \"" + juce::String(items_[static_cast<std::size_t>(selected_)].name) + "\"? This cannot be undone.")
                                   .withButton("Delete")
                                   .withButton("Cancel")
                                   .withAssociatedComponent(getTopLevelComponent()),
                               [self](int r) {
                                 if (r == 1 && self) self->deleteSelected();
                               });
}

void PresetBrowser::updateButtons() {
  const bool have = selected_ >= 0;
  const bool mine = have && !items_[static_cast<std::size_t>(selected_)].factory;
  load_.setEnabled(have && items_[static_cast<std::size_t>(selected_)].loadable);
  rename_.setEnabled(mine);
  delete_.setEnabled(mine);
  favorite_.setEnabled(have);
  const bool fav = have && items_[static_cast<std::size_t>(selected_)].favorite;
  favorite_.setButtonText(fav ? "Unfavorite" : "Favorite");
  favorite_.setTitle(favorite_.getButtonText());
  undo_.setEnabled(cmd_.canUndoPreset());
}

// ---- list model ----
void PresetBrowser::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return;
  const auto& it = items_[static_cast<std::size_t>(rows_[static_cast<std::size_t>(row)])];
  const auto& p = cdl::pal();
  const float s = cdl::uiScale();
  if (selected) {
    g.setColour(p.spray.withAlpha(0.20f));
    g.fillRect(0, 0, w, h);
    g.setColour(p.spray);
    g.fillRect(0, 0, 2, h);
  }
  g.setColour(it.loadable ? p.ink : p.inkMuted);
  g.setFont(cdl::font(cdl::FontRole::Body, s));
  auto r = juce::Rectangle<int>(10, 3, w - 80, h / 2 - 2);
  g.drawText(juce::String(it.name), r, juce::Justification::centredLeft, true);
  g.setColour(p.inkMuted);
  g.setFont(cdl::font(cdl::FontRole::Hint, s));
  g.drawText(juce::String(cf::ui::presetSubtitle(it)), juce::Rectangle<int>(10, h / 2, w - 80, h / 2 - 3), juce::Justification::centredLeft, true);
  // Star: filled for a favorite, an outline otherwise on the selected row, so the state is never colour only.
  if (it.favorite || selected) {
    juce::Path star;
    star.addStar({static_cast<float>(w - 18), static_cast<float>(h) / 2.0f}, 5, 4.0f, 8.0f);
    g.setColour(it.favorite ? p.warn : p.inkMuted);
    if (it.favorite) g.fillPath(star);
    else g.strokePath(star, juce::PathStrokeType(1.2f));
  }
  g.setColour(p.edge);
  g.fillRect(0, h - 1, w, 1);
}

juce::String PresetBrowser::getNameForRow(int row) {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return {};
  const auto& it = items_[static_cast<std::size_t>(rows_[static_cast<std::size_t>(row)])];
  return juce::String(it.name) + ", " + juce::String(cf::ui::presetSubtitle(it)) + (it.favorite ? ", favorite" : "");
}

void PresetBrowser::listBoxItemClicked(int row, const juce::MouseEvent& e) {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return;
  if (e.x > list_.getRowPosition(row, true).getWidth() - 32) toggleFavorite(juce::String(items_[static_cast<std::size_t>(rows_[static_cast<std::size_t>(row)])].key));
}

void PresetBrowser::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return;
  selected_ = rows_[static_cast<std::size_t>(row)];
  loadSelected(applyAndGenerate());
}

void PresetBrowser::selectedRowsChanged(int row) {
  if (settingSelection_) return;
  selected_ = row >= 0 && row < static_cast<int>(rows_.size()) ? rows_[static_cast<std::size_t>(row)] : -1;
  updateButtons();
  repaint();
}

bool PresetBrowser::keyPressed(const juce::KeyPress& k, juce::Component*) {
  if (k == juce::KeyPress::downKey) return moveSelection(1);
  if (k == juce::KeyPress::upKey) return moveSelection(-1);
  if (k == juce::KeyPress::returnKey) return loadSelected(applyAndGenerate());
  return false;
}

juce::String PresetBrowser::detailsText() const {
  const auto* p = presetOf(selected_);
  if (!p) return "Choose a preset to see what it sets.";
  juce::String t;
  if (!p->description.empty()) t << juce::String(p->description) << "\n";
  t << "Profile " << choiceName("break_profile", paramOf(*p, "break_profile", 1)) << ", " << p->bars << (p->bars == 1 ? " bar, " : " bars, ") << "1/" << p->gridDivision << (p->gridTriplet ? "T" : "") << " grid\n";
  t << "Density " << pct(p->density) << ", swing " << pct(p->swing) << ", variation " << pct(p->variation) << ", chaos " << pct(p->chaos) << "\n";
  t << "Motif " << juce::String(p->motif) << ", depth " << p->depth;
  if (!p->rules.rules.empty()) t << ", " << static_cast<int>(p->rules.rules.size()) << (p->rules.rules.size() == 1 ? " rule" : " rules");
  return t;
}

void PresetBrowser::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  if (!currentName_.isEmpty()) {
    g.setColour(p.inkMuted);
    g.setFont(cdl::font(cdl::FontRole::MonoXs, cdl::uiScale()));
    g.drawText("LOADED " + currentName_.toUpperCase(), getLocalBounds().removeFromBottom(14), juce::Justification::centredLeft, true);
  }
  if (detailsArea_.isEmpty()) return;
  g.setColour(p.bg);
  g.fillRoundedRectangle(detailsArea_.toFloat(), 3.0f);
  g.setColour(p.edge);
  g.drawRoundedRectangle(detailsArea_.toFloat().reduced(0.5f), 3.0f, 1.0f);
  g.setColour(p.ink);
  g.setFont(cdl::font(cdl::FontRole::Hint, cdl::uiScale()));
  g.drawFittedText(detailsText(), detailsArea_.reduced(8, 6), juce::Justification::topLeft, 6);
}

void PresetBrowser::resized() {
  auto a = getLocalBounds().reduced(6);
  search_.setBounds(a.removeFromTop(28));
  a.removeFromTop(6);
  auto chips = a.removeFromTop(26);
  const int cw = chips.getWidth() / 4;
  for (auto* b : {&all_, &factory_, &user_, &favs_}) b->setBounds(chips.removeFromLeft(cw).reduced(2, 0));
  a.removeFromTop(6);
  a.removeFromBottom(14);
  auto buttons2 = a.removeFromBottom(26);
  a.removeFromBottom(4);
  auto buttons1 = a.removeFromBottom(26);
  a.removeFromBottom(4);
  autoGenerate_.setBounds(a.removeFromBottom(22));
  a.removeFromBottom(4);
  const int spare = a.getHeight() - (4 * list_.getRowHeight() + 6);  // the list keeps room for four rows; the details block only gets what is left
  if (spare >= 70) {
    detailsArea_ = a.removeFromBottom(std::min(112, spare));
    a.removeFromBottom(6);
  } else {
    detailsArea_ = {};
  }
  list_.setBounds(a);
  const int w1 = buttons1.getWidth() / 3;
  for (auto* b : {&load_, &save_, &favorite_}) b->setBounds(buttons1.removeFromLeft(w1).reduced(2, 0));
  const int w2 = buttons2.getWidth() / 3;
  for (auto* b : {&rename_, &delete_, &undo_}) b->setBounds(buttons2.removeFromLeft(w2).reduced(2, 0));
}
