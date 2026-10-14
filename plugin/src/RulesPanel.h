#pragma once
// In-editor overlay for roles and grammar rules (docs/specs/roadmap/F06-rule-editor.md). A view over
// ui::RuleEditorModel (the draft) and ProjectSession (apply, test, explain); it holds no rule logic of its own.
#include <chopfractal/plugin_ui_adapter/rule_editor.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

class ChopFractalProcessor;

// A ListBox model over callbacks, so each list in the panel is a few lines.
class StringListModel : public juce::ListBoxModel {
 public:
  std::function<int()> count;
  std::function<juce::String(int)> text;
  std::function<juce::String(int)> tooltip;
  std::function<void(int)> onSelect;
  int getNumRows() override { return count ? count() : 0; }
  void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
  void selectedRowsChanged(int row) override {
    if (onSelect && row >= 0) onSelect(row);
  }
  juce::String getTooltipForRow(int row) override { return tooltip ? tooltip(row) : juce::String(); }
};

class RulesPanel : public juce::Component {
 public:
  struct Hooks {
    std::function<void()> applyAndMutate;                        // runs Mutate with amount 1.0 after Apply
    std::function<chopfractal::EventId()> primarySelection;      // the selected hit, or invalid
    std::function<void(int bar, int beat)> selectBeat;           // highlight a beat in the pattern view
    std::function<double()> densityHint;                         // density slider, for Test Rules without a pattern
  };

  RulesPanel(ChopFractalProcessor& processor, Hooks hooks);
  void open();   // loads the draft from the session if it has no unapplied edits, then shows the panel
  void close();
  bool hasUnapplied() const { return model_.dirty(); }
  const chopfractal::ui::RuleEditorModel& model() const { return model_; }
  void paint(juce::Graphics& g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;

 private:
  enum class Tab { Rules, Roles };

  void showTab(Tab tab);
  void reloadFromSession(bool announce, bool force = false);
  void refreshAll();
  void refreshForm();
  void formChanged();
  void updateFormVisibility();
  void addTemplate(chopfractal::ui::TemplateKind kind);
  void apply();
  bool applyOnly();
  void revert();
  void testRules();
  void why();
  void assignRoleFromCombo();
  void reloadRoleChoices();
  chopfractal::roles::RuleSet currentRulesOfSession() const;
  chopfractal::roles::Rule* selectedDraftRule();
  std::uint32_t selectedRuleId() const;
  void setStatus(const juce::String& text);

  ChopFractalProcessor& proc_;
  Hooks hooks_;
  chopfractal::ui::RuleEditorModel model_;
  chopfractal::roles::RuleSet lastSessionRules_;
  Tab tab_ = Tab::Rules;
  bool loading_ = false;
  std::vector<chopfractal::ui::RuleRowView> rows_;
  std::vector<chopfractal::ui::RuleIssueView> issues_;
  std::vector<juce::String> chopLabels_;
  std::vector<chopfractal::ChopId> chopIds_;
  std::uint32_t selectedId_ = 0;

  juce::Label header_;
  juce::TextButton rulesTab_{"Rules tab"}, rolesTab_{"Roles tab"};
  StringListModel ruleModel_, problemModel_, noteModel_, chopModel_;
  juce::ListBox ruleList_, problems_, notes_, chopList_;
  juce::Label problemCount_, noteCount_, emptyRoles_;
  juce::TextButton add_{"Add Rule"}, duplicate_{"Duplicate"}, delete_{"Delete"};
  juce::Label templatesLabel_;
  juce::ComboBox templateRole_;
  juce::TextEditor templateFrom_, templateTo_;
  juce::TextButton keepFirst_{"Keep first"}, noRepeats_{"No repeats"}, noRoleRepeats_{"No role repeats"}, needFinal_{"Need in final beat"},
      preferBoundaries_{"Prefer at boundaries"}, excludeBars_{"Exclude from bars"};

  juce::ToggleButton enabled_{"Rule enabled"};
  juce::ComboBox action_, role_, chop_, position_, repeatBy_;
  juce::TextEditor barFrom_, barTo_, beatFrom_, beatTo_, count_, label_;
  juce::Slider weight_, priority_;
  std::vector<std::unique_ptr<juce::Label>> captions_;
  juce::Label* weightCaption_ = nullptr;
  juce::Label* countCaption_ = nullptr;
  juce::Label* repeatCaption_ = nullptr;

  juce::ComboBox chopRole_;
  juce::TextButton audition_{"Audition chop"};

  juce::TextButton applyBtn_{"Apply"}, revertBtn_{"Revert"}, applyMutate_{"Apply and Mutate"}, test_{"Test Rules"}, why_{"Why?"}, close_{"Close"};
};
