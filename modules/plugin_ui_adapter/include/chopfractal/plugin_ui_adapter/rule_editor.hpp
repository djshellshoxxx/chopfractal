#pragma once
// Headless model of the rule editor (G2 / F06): the user's draft of the rule set, kept apart from the applied rules until Apply.
// It builds rules from the module's templates, keeps each rule's scope consistent with its action, lists the rows in the engine's
// evaluation order and turns validation results into issue lines. No JUCE; UI thread only.
#include <chopfractal/chop_contracts/ids.hpp>
#include <chopfractal/chop_roles_grammar/grammar.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::ui {

enum class TemplateKind : std::uint8_t { KeepFirst, NoRepeats, NoRoleRepeats, NeedInFinalBeat, PreferAtBoundaries, ExcludeFromBars };

struct RuleIssueView {
  std::uint32_t rule = 0;  // 0 = not about one rule
  bool error = false;      // else a warning
  std::string text;        // "rule 4: count limits are evaluated per bar; use Bar scope"
};
struct RuleRowView {
  std::uint32_t id = 0;
  bool enabled = true;
  std::string text;     // "[x] #3  Forbid hat in bars 3-4 P10" plus "  Error" or "  Warning" when the rule has an issue
  std::string tooltip;  // roles::describe(rule)
  int priority = 0;
  bool hasError = false, hasWarning = false;
};

class RuleEditorModel {
 public:
  void reset(const roles::RuleSet& applied, const roles::RoleMap& roles, int patternBars, const std::vector<ChopId>& chops);
  const roles::RuleSet& draft() const { return draft_; }
  const roles::RuleSet& applied() const { return applied_; }
  bool dirty() const;     // draft differs from the applied rules, field by field
  bool canApply() const;  // dirty and no error issue
  bool canAdd() const { return draft_.rules.size() < roles::kMaxRules; }
  // Marks the draft as applied (after ProjectSession::setRules succeeded).
  void markApplied() { applied_ = draft_; }
  void setRoles(const roles::RoleMap& roles) { roles_ = roles; }  // roles changed in the Roles tab: warnings follow

  std::uint32_t addRule();  // Prefer, weight 2.0, empty condition; 0 at the limit
  std::uint32_t addFromTemplate(TemplateKind, const roles::RoleId& role, int barFrom1, int barTo1);  // 1-based bars; 0 when refused
  std::uint32_t duplicate(std::uint32_t id);  // copy gets a new id; label + " (copy)" cut to 128 characters; 0 when refused
  bool remove(std::uint32_t id);
  bool update(const roles::Rule& edited);  // derives scope from the action; false for an unknown id
  const roles::Rule* find(std::uint32_t id) const;
  std::vector<RuleRowView> rows() const;  // engine order: priority descending, then id
  std::vector<RuleIssueView> issues() const;  // validateRules(draft, roles) plus the two UI-level warnings
  std::uint32_t nextId() const;               // never reuses an id handed out in this session
  // "No problems", "1 problem (1 error)", "3 problems (2 errors)".
  std::string issueSummary() const;
  static roles::RuleScope scopeFor(roles::ActionKind);
  static const char* actionName(roles::ActionKind);  // "Keep", "Require", "Forbid", "Limit repeats", "Limit count per bar", "Prefer / avoid"

 private:
  roles::RuleSet applied_, draft_;
  roles::RoleMap roles_;
  int patternBars_ = 0;
  std::vector<ChopId> chops_;
  std::uint32_t highWater_ = 0;
};

}  // namespace chopfractal::ui
