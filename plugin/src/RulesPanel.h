#pragma once
// Rule editor drawer (G2 / F06): roles per chop, the rule list with templates and an edit form, validation problems, the last
// blocked changes, and the Apply / Revert / Apply and Mutate / Test Rules / Why? footer. The draft lives in ui::RuleEditorModel;
// nothing reaches the session until Apply.
#include <chopfractal/plugin_ui_adapter/rule_editor.hpp>

#include <functional>
#include <memory>
#include <vector>

#include "EditorPanels.h"

class RulesPanel : public EditorPanelBase {
 public:
  RulesPanel(EditorModel&, EditorCommands&);
  ~RulesPanel() override;
  void resized() override;
  void paint(juce::Graphics&) override;
  const chopfractal::ui::RuleEditorModel& draft() const { return draft_; }

 private:
  // A ListBox model over strings (rule rows, problems, blocked changes).
  struct TextList : juce::ListBoxModel {
    std::function<int()> count;
    std::function<juce::String(int)> text;
    std::function<void(int)> select;
    int getNumRows() override { return count ? count() : 0; }
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override {
      if (select) select(row);
    }
    void selectedRowsChanged(int row) override {
      if (select && row >= 0) select(row);
    }
  };
  // One row of the Roles tab: chop number and range, an editable role box and Audition.
  struct RoleRow : juce::Component {
    RoleRow(RulesPanel& owner, int index);
    void resized() override;
    void paint(juce::Graphics&) override;
    void setChop(const chopfractal::ChopInfo&, const juce::String& range, const juce::String& role, const std::vector<juce::String>& choices);
    RulesPanel& owner;
    int index;
    chopfractal::ChopId chop;
    juce::String rangeText;
    juce::ComboBox role;
    juce::TextButton audition{"Audition"};
    bool loading = false;
  };
  struct RoleModel : juce::ListBoxModel {
    RulesPanel& owner;
    explicit RoleModel(RulesPanel& o) : owner(o) {}
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow(int row, bool selected, juce::Component* existing) override;
  };

  void modelChanged(Change) override;
  void showTab(bool rules);
  void resync(bool announce);  // re-read the session's rules (project load, Revert)
  void refreshAll();
  void refreshList();
  void refreshProblems();
  void refreshRoles();
  void refreshFooter();
  void loadForm();    // selected rule -> form widgets
  void commitForm();  // form widgets -> draft
  void layoutBody();
  void addFromTemplate(chopfractal::ui::TemplateKind);
  void selectRule(std::uint32_t id);
  void showWhy();
  std::vector<juce::String> roleChoices() const;
  static std::string signatureOf(const chopfractal::roles::RuleSet&);

  chopfractal::ui::RuleEditorModel draft_;
  EditorModel::RulesSnapshot snap_;
  std::string appliedSignature_;
  std::uint32_t selectedRule_ = 0;
  bool rulesTab_ = true, loading_ = false;

  juce::Viewport viewport_;
  juce::Component body_;
  juce::TextButton rulesTab_btn_{"Rules"}, rolesTab_btn_{"Roles"};

  // Rules tab
  TextList ruleModel_, problemModel_, noteModel_;
  juce::ListBox ruleList_{"Rule list"}, problems_{"Problems"}, blocked_{"Last blocked changes"};
  juce::TextButton add_{"Add Rule"}, duplicate_{"Duplicate"}, delete_{"Delete"};
  juce::Label templatesLabel_{{}, "Templates:"};
  juce::ComboBox templateRole_;
  juce::TextEditor barFrom_, barTo_;
  juce::TextButton keepFirst_{"Keep first"}, noRepeats_{"No repeats"}, noRoleRepeats_{"No role repeats"}, needFinal_{"Need in final beat"}, preferBoundary_{"Prefer at boundaries"}, excludeBars_{"Exclude from bars"};

  juce::ToggleButton enabled_{"Rule enabled"};
  juce::Label actionL_, roleL_, chopL_, positionL_, barsL_, beatsL_, countL_, repeatL_, weightL_, priorityL_, labelL_;
  juce::ComboBox action_, ruleRole_, ruleChop_, position_, repeatBy_;
  juce::TextEditor barMin_, barMax_, beatMin_, beatMax_, count_, label_;
  juce::Slider weight_, priority_;

  // Roles tab
  RoleModel roleModel_{*this};
  juce::ListBox roleList_{"Chop roles"};
  juce::Label rolesEmpty_;

  juce::Label problemCount_, blockedCount_;
  juce::TextButton apply_{"Apply"}, revert_{"Revert"}, applyMutate_{"Apply and Mutate"}, test_{"Test Rules"}, why_{"Why?"};
};
