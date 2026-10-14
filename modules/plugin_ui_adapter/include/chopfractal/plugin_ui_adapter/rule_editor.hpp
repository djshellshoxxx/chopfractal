#pragma once
// Headless draft model behind the rules editor: edits a copy of the applied RuleSet, derives each rule's scope from
// its action, lists rules in engine order, and reports module errors plus two UI-level warnings. No JUCE, no session.
#include <chopfractal/chop_roles_grammar/grammar.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::ui {

enum class TemplateKind : std::uint8_t { KeepFirst, NoRepeats, NoRoleRepeats, NeedInFinalBeat, PreferAtBoundaries, ExcludeFromBars };

struct RuleIssueView {
  std::uint32_t rule = 0;
  bool error = false;
  std::string text;
};

struct RuleRowView {
  std::uint32_t id = 0;
  bool enabled = true;
  std::string text;     // "[x] #3  Forbid hat in bars 3-4 P10" plus a trailing Error / Warning word
  std::string tooltip;  // roles::describe(rule)
  int priority = 0;
  bool hasError = false, hasWarning = false;
};

class RuleEditorModel {
 public:
  void reset(const roles::RuleSet& applied, const roles::RoleMap& roles, int patternBars, const std::vector<ChopId>& chops);
  void setRoles(const roles::RoleMap& roles) { roles_ = roles; }
  const roles::RuleSet& draft() const { return draft_; }
  const roles::RuleSet& applied() const { return applied_; }
  const roles::Rule* find(std::uint32_t id) const;
  bool dirty() const;
  bool canApply() const;
  bool canAdd() const { return draft_.rules.size() < roles::kMaxRules; }
  std::uint32_t addRule();  // Prefer, weight 2.0, empty condition; returns its id (0 at the limit)
  std::uint32_t addFromTemplate(TemplateKind kind, const roles::RoleId& role, int barFrom1, int barTo1);  // 1-based bars; 0 if refused
  std::uint32_t duplicate(std::uint32_t id);
  bool remove(std::uint32_t id);
  bool update(const roles::Rule& edited);  // false when the id is unknown
  void markApplied() { applied_ = draft_; }
  std::vector<RuleRowView> rows() const;
  std::vector<RuleIssueView> issues() const;
  std::uint32_t nextId() const;

  static roles::RuleScope scopeFor(roles::ActionKind kind);

 private:
  std::uint32_t takeId();

  roles::RuleSet applied_, draft_;
  roles::RoleMap roles_;
  int patternBars_ = 0;
  std::vector<ChopId> chops_;
  std::uint32_t counter_ = 1;
};

}  // namespace chopfractal::ui
