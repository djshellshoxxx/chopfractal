#pragma once
// Chop roles and a small, serializable rule grammar. Rules are data, never scripts. The module owns
// role IDs, rule validation, and candidate evaluation; it does not own audio, generate patterns, or
// render. It plugs into generation through chop_contracts' ICandidatePolicy.
//
// Evaluation model (v1):
//   Hard prohibitions : Forbid, MaxConsecutiveRepeats, MaxCount (Bar scope)  -> reject a candidate
//   Soft preferences  : Prefer (rule weight multiplies the candidate weight)
//   Requirements      : Require (Beat scope; "this chop/role must appear in the beat")
//   Preservation      : Preserve (mutation keeps matching events)
// Conflict resolution is deterministic: rules are ordered by priority (high first) then id; a
// prohibition is overridden only by a matching Require rule of strictly higher priority.
// Threading: pure and thread-safe for const use; build a new policy object per generation.
#include <chopfractal/chop_contracts/ids.hpp>
#include <chopfractal/chop_contracts/policy.hpp>
#include <chopfractal/chop_contracts/result.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace chopfractal::roles {

constexpr std::uint32_t kSchemaVersion = 1;
constexpr std::size_t kMaxRules = 64;
constexpr std::size_t kMaxRoleLength = 32;
constexpr std::size_t kMaxRoleAssignments = 256;

using RoleId = std::string;  // lowercase [a-z0-9_-], 1..32 chars; custom IDs are allowed

const std::vector<RoleId>& builtinRoles();  // kick, snare, hat, cymbal, percussion, texture, accent, transition, other
bool isValidRoleId(const std::string& id);

enum class Position : std::uint8_t { Any, FirstInBar, FinalBeat, PhraseBoundary };
enum class RuleScope : std::uint8_t { Phrase, Bar, Beat, Event, Region };
enum class ActionKind : std::uint8_t { Preserve, Require, Forbid, MaxConsecutiveRepeats, MaxCount, Prefer };
enum class RepeatBy : std::uint8_t { SameChop, SameRole };

struct Condition {
  RoleId role;          // empty = any role
  ChopId chop;          // invalid = any chop
  Position position = Position::Any;
  int barMin = 0;       // inclusive bar range; barMax < 0 = unbounded
  int barMax = -1;
  int beatMin = 0;      // inclusive beat range; beatMax < 0 = unbounded
  int beatMax = -1;
};

struct Action {
  ActionKind kind = ActionKind::Prefer;
  int count = 1;                      // MaxConsecutiveRepeats / MaxCount limit (>= 1)
  RepeatBy repeatBy = RepeatBy::SameChop;
};

struct Rule {
  std::uint32_t id = 0;
  bool enabled = true;
  RuleScope scope = RuleScope::Event;
  Condition when;
  Action action;
  int priority = 0;
  double weight = 1.0;  // Prefer multiplier: > 1 prefers, < 1 avoids
  std::string label;    // shown in explanations
};

struct RuleSet {
  std::vector<Rule> rules;
};

struct Issue {
  enum class Severity : std::uint8_t { Error, Warning };
  Severity severity = Severity::Error;
  std::uint32_t rule = 0;
  std::string message;
};
struct ValidationReport {
  std::vector<Issue> issues;
  bool ok() const {
    for (const Issue& i : issues)
      if (i.severity == Issue::Severity::Error) return false;
    return true;
  }
};

class RoleMap {
 public:
  Status assignRole(ChopId chop, const RoleId& role);
  void clearRole(ChopId chop) { roles_.erase(chop.value); }
  RoleId roleOf(ChopId chop) const;  // empty when unassigned
  const std::map<std::uint64_t, RoleId>& all() const { return roles_; }
  // Drop assignments for chops that no longer exist.
  void retainOnly(const std::vector<ChopId>& chops);

 private:
  std::map<std::uint64_t, RoleId> roles_;
};

// Static checks; contradictory rules are Warnings (the engine keeps the previous valid event at run time).
ValidationReport validateRules(const RuleSet& rules, const RoleMap* roles = nullptr);

class GrammarPolicy : public ICandidatePolicy {
 public:
  // `chops` are the candidate chops in a stable order; role -> chop resolution takes the first match.
  GrammarPolicy(RoleMap roles, RuleSet rules, std::vector<ChopId> chops);

  Decision evaluate(const CandidateQuery& q) const override;
  bool isPreserved(const CandidateQuery& q) const override;
  std::vector<Requirement> required(const ScopeContext& scope, const std::vector<ChopId>& placedInScope) const override;

  // Human-readable "why this stayed / changed" text agreeing with the actual decision.
  std::string explain(const Decision& d) const;
  const RuleSet& rules() const { return rules_; }
  const RoleMap& roleMap() const { return roles_; }

 private:
  bool matchesChop(const Condition& c, ChopId chop) const;
  bool matchesPosition(const Condition& c, const ScopeContext& s, bool firstInBar) const;
  bool inRange(const Condition& c, const ScopeContext& s) const;
  std::vector<const Rule*> ordered() const;

  RoleMap roles_;
  RuleSet rules_;
  std::vector<ChopId> chops_;
};

// ---- guided templates: ready-made rules for the common cases ----
namespace templates {
Rule preserveFirst(std::uint32_t id, const RoleId& role);                        // keep the first <role> in each bar
Rule avoidAdjacentRepeats(std::uint32_t id, RepeatBy by = RepeatBy::SameChop);   // never the same thing twice in a row
Rule requireInFinalBeat(std::uint32_t id, const RoleId& role);                   // keep at least one <role> in the final beat
Rule preferAtPhraseBoundary(std::uint32_t id, const RoleId& role, double weight = 3.0);
Rule excludeRole(std::uint32_t id, const RoleId& role, int barMin, int barMax);  // exclude a role from selected bars
}  // namespace templates

// ---- module-owned versioned state (schema kSchemaVersion) ----
std::vector<std::uint8_t> serialize(const RoleMap& roles, const RuleSet& rules);
struct State {
  RoleMap roles;
  RuleSet rules;
};
Result<State> deserialize(const std::uint8_t* data, std::size_t size);

}  // namespace chopfractal::roles
