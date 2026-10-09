#include <iterator>
#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/chop_roles_grammar/grammar.hpp>

#include <algorithm>
#include <cmath>
#include <set>

namespace chopfractal::roles {
namespace {

bool positive(double v) { return v > 0.0 && std::isfinite(v); }

const char* actionName(ActionKind k) {
  switch (k) {
    case ActionKind::Preserve: return "preserve";
    case ActionKind::Require: return "require";
    case ActionKind::Forbid: return "forbid";
    case ActionKind::MaxConsecutiveRepeats: return "limit repeats of";
    case ActionKind::MaxCount: return "limit count of";
    case ActionKind::Prefer: return "prefer";
  }
  return "?";
}

// id 4 + enabled 1 + scope 1 + role str 4 + chop 8 + position 1 + four ints 16 + kind 1 + count 4 +
// repeatBy 1 + priority 4 + weight 8 + label str 4: the smallest serialized rule, used to bound allocations.
constexpr std::size_t kMinRuleBytes = 57;

}  // namespace

const std::vector<RoleId>& builtinRoles() {
  static const std::vector<RoleId> v{"kick", "snare", "hat", "cymbal", "percussion", "texture", "accent", "transition", "other"};
  return v;
}

bool isValidRoleId(const std::string& id) {
  if (id.empty() || id.size() > kMaxRoleLength) return false;
  for (char c : id)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  return true;
}

Status RoleMap::assignRole(ChopId chop, const RoleId& role) {
  if (!chop.valid()) return makeError(ErrorCode::InvalidArgument, "invalid chop id");
  if (!isValidRoleId(role)) return makeError(ErrorCode::InvalidArgument, "role ids use lowercase letters, digits, '_' and '-' (1-32 chars)");
  if (roles_.size() >= kMaxRoleAssignments && roles_.count(chop.value) == 0)
    return makeError(ErrorCode::LimitExceeded, "too many role assignments", static_cast<std::int64_t>(kMaxRoleAssignments));
  roles_[chop.value] = role;
  return {};
}

RoleId RoleMap::roleOf(ChopId chop) const {
  auto it = roles_.find(chop.value);
  return it == roles_.end() ? RoleId{} : it->second;
}

void RoleMap::retainOnly(const std::vector<ChopId>& chops) {
  std::set<std::uint64_t> keep;
  for (ChopId c : chops) keep.insert(c.value);
  for (auto it = roles_.begin(); it != roles_.end();) it = keep.count(it->first) ? std::next(it) : roles_.erase(it);
}

ValidationReport validateRules(const RuleSet& rules, const RoleMap* roles) {
  ValidationReport rep;
  auto err = [&](std::uint32_t id, std::string m) { rep.issues.push_back({Issue::Severity::Error, id, std::move(m)}); };
  auto warn = [&](std::uint32_t id, std::string m) { rep.issues.push_back({Issue::Severity::Warning, id, std::move(m)}); };
  if (rules.rules.size() > kMaxRules) err(0, "too many rules (maximum " + std::to_string(kMaxRules) + ")");
  std::set<std::uint32_t> ids;
  for (const Rule& r : rules.rules) {
    if (r.label.size() > 128) err(r.id, "the label is longer than 128 characters");
    if (r.id == 0 || !ids.insert(r.id).second) err(r.id, "rule id is zero or duplicated");
    if (!r.when.role.empty() && !isValidRoleId(r.when.role)) err(r.id, "malformed role id in condition");
    if (r.when.barMin < 0 || r.when.beatMin < 0 || (r.when.barMax >= 0 && r.when.barMax < r.when.barMin) ||
        (r.when.beatMax >= 0 && r.when.beatMax < r.when.beatMin))
      err(r.id, "invalid bar or beat range");
    if (!positive(r.weight) || r.weight > 100.0) err(r.id, "weight must be a positive number up to 100");
    if (r.priority < -1000 || r.priority > 1000) err(r.id, "priority out of range");
    switch (r.action.kind) {
      case ActionKind::MaxConsecutiveRepeats:
        if (r.action.count < 1 || r.action.count > 16) err(r.id, "repeat limit must be 1 to 16");
        break;
      case ActionKind::MaxCount:
        if (r.action.count < 1 || r.action.count > 512) err(r.id, "count limit must be at least 1");
        if (r.scope != RuleScope::Bar) err(r.id, "count limits are evaluated per bar; use Bar scope");
        break;
      case ActionKind::Require:
        if (r.scope != RuleScope::Beat) err(r.id, "requirements are evaluated per beat; use Beat scope");
        if (r.when.role.empty() && !r.when.chop.valid()) err(r.id, "a requirement needs a role or a chop");
        break;
      default: break;
    }
  }
  // Static conflict hints: forbidding and requiring the same thing.
  for (const Rule& a : rules.rules)
    for (const Rule& b : rules.rules) {
      if (!a.enabled || !b.enabled || a.action.kind != ActionKind::Forbid || b.action.kind != ActionKind::Require) continue;
      const bool sameTarget = (a.when.role == b.when.role && !a.when.role.empty()) || (a.when.chop == b.when.chop && a.when.chop.valid());
      if (sameTarget) warn(a.id, "rule " + std::to_string(a.id) + " forbids what rule " + std::to_string(b.id) + " requires");
    }
  if (roles)
    for (const Rule& r : rules.rules) {
      if (!r.enabled || r.when.role.empty()) continue;
      bool any = false;
      for (const auto& kv : roles->all()) any = any || kv.second == r.when.role;
      if (!any) warn(r.id, "no chop has the role '" + r.when.role + "'");
    }
  return rep;
}

GrammarPolicy::GrammarPolicy(RoleMap roles, RuleSet rules, std::vector<ChopId> chops)
    : roles_(std::move(roles)), rules_(std::move(rules)), chops_(std::move(chops)) {}

bool GrammarPolicy::matchesChop(const Condition& c, ChopId chop) const {
  if (c.chop.valid() && c.chop != chop) return false;
  if (!c.role.empty() && roles_.roleOf(chop) != c.role) return false;
  return true;
}

bool GrammarPolicy::inRange(const Condition& c, const ScopeContext& s) const {
  if (s.bar < c.barMin || (c.barMax >= 0 && s.bar > c.barMax)) return false;
  if (s.beat < c.beatMin || (c.beatMax >= 0 && s.beat > c.beatMax)) return false;
  return true;
}

bool GrammarPolicy::matchesPosition(const Condition& c, const ScopeContext& s, bool firstInBar) const {
  switch (c.position) {
    case Position::Any: return true;
    case Position::FirstInBar: return firstInBar;
    case Position::FinalBeat: return s.finalBeat();
    case Position::PhraseBoundary: return (s.bar == 0 && s.beat == 0) || (s.finalBar() && s.finalBeat());
  }
  return false;
}

std::vector<const Rule*> GrammarPolicy::ordered() const {
  std::vector<const Rule*> v;
  for (const Rule& r : rules_.rules)
    if (r.enabled) v.push_back(&r);
  std::stable_sort(v.begin(), v.end(), [](const Rule* a, const Rule* b) { return a->priority != b->priority ? a->priority > b->priority : a->id < b->id; });
  return v;
}

Decision GrammarPolicy::evaluate(const CandidateQuery& q) const {
  Decision d;
  const std::vector<const Rule*> rules = ordered();

  // A matching Require of strictly higher priority overrides a lower-priority prohibition.
  int maxRequirePriority = -1000000;
  bool anyRequire = false;
  for (const Rule* r : rules)
    if (r->action.kind == ActionKind::Require && matchesChop(r->when, q.chop) && inRange(r->when, q.scope) &&
        matchesPosition(r->when, q.scope, q.firstInBar)) {
      anyRequire = true;
      maxRequirePriority = std::max(maxRequirePriority, r->priority);
    }

  auto reject = [&](const Rule& r) {
    if (anyRequire && r.priority < maxRequirePriority) {
      d.trace.push_back({r.id, TraceEntry::Effect::Conflict});
      return;
    }
    d.allowed = false;
    if (d.blockedBy == 0) d.blockedBy = r.id;  // highest priority first, so the first rejecter is the decider
    d.trace.push_back({r.id, TraceEntry::Effect::Rejected});
  };

  for (const Rule* rp : rules) {
    const Rule& r = *rp;
    if (!inRange(r.when, q.scope)) continue;
    switch (r.action.kind) {
      case ActionKind::Forbid:
        if (matchesChop(r.when, q.chop) && matchesPosition(r.when, q.scope, q.firstInBar)) reject(r);
        break;
      case ActionKind::MaxConsecutiveRepeats: {
        if (!matchesChop(r.when, q.chop)) break;
        int run = 0;
        for (auto it = q.recent.rbegin(); it != q.recent.rend(); ++it) {
          const bool same = r.action.repeatBy == RepeatBy::SameChop ? *it == q.chop
                                                                    : (!roles_.roleOf(q.chop).empty() && roles_.roleOf(*it) == roles_.roleOf(q.chop));
          if (!same) break;
          ++run;
        }
        if (run >= r.action.count) reject(r);
        break;
      }
      case ActionKind::MaxCount: {
        if (!matchesChop(r.when, q.chop)) break;
        int n = 0;
        for (ChopId c : q.barSoFar)
          if (matchesChop(r.when, c)) ++n;
        if (n >= r.action.count) reject(r);
        break;
      }
      case ActionKind::Prefer:
        if (matchesChop(r.when, q.chop) && matchesPosition(r.when, q.scope, q.firstInBar)) {
          d.weight *= r.weight;
          d.trace.push_back({r.id, TraceEntry::Effect::Weighted});
        }
        break;
      case ActionKind::Preserve:
      case ActionKind::Require: break;  // handled by isPreserved() / required()
    }
  }
  if (!d.allowed) d.weight = 0.0;
  return d;
}

bool GrammarPolicy::isPreserved(const CandidateQuery& q) const {
  for (const Rule& r : rules_.rules)
    if (r.enabled && r.action.kind == ActionKind::Preserve && matchesChop(r.when, q.chop) && inRange(r.when, q.scope) &&
        matchesPosition(r.when, q.scope, q.firstInBar))
      return true;
  return false;
}

std::vector<Requirement> GrammarPolicy::required(const ScopeContext& scope, const std::vector<ChopId>& placedInScope) const {
  std::vector<Requirement> out;
  for (const Rule* rp : ordered()) {
    const Rule& r = *rp;
    if (r.action.kind != ActionKind::Require || !inRange(r.when, scope)) continue;
    // Beat-scope requirements: the position filter sees the beat, so "first in bar" means the first beat.
    if (!matchesPosition(r.when, scope, scope.beat == 0)) continue;
    // Already satisfied by something placed in this scope?
    bool satisfied = false;
    for (ChopId c : placedInScope) satisfied = satisfied || matchesChop(r.when, c);
    if (satisfied) continue;
    // Resolve to a concrete chop: the named chop, else the first chop holding the role.
    ChopId target = r.when.chop;
    if (!target.valid())
      for (ChopId c : chops_)
        if (roles_.roleOf(c) == r.when.role) {
          target = c;
          break;
        }
    if (target.valid()) out.push_back({target, r.id});
  }
  return out;
}

std::string GrammarPolicy::explain(const Decision& d) const {
  std::string text = d.allowed ? "kept" : "rejected";
  auto labelOf = [&](std::uint32_t id) {
    for (const Rule& r : rules_.rules)
      if (r.id == id) return std::string(actionName(r.action.kind)) + (r.label.empty() ? "" : " \"" + r.label + "\"");
    return std::string("unknown rule");
  };
  for (const TraceEntry& t : d.trace) {
    const char* effect = "";
    switch (t.effect) {
      case TraceEntry::Effect::Rejected: effect = "rejected it"; break;
      case TraceEntry::Effect::Weighted: effect = "weighted it"; break;
      case TraceEntry::Effect::Preserved: effect = "preserved it"; break;
      case TraceEntry::Effect::Required: effect = "required it"; break;
      case TraceEntry::Effect::Conflict: effect = "was overridden by a higher-priority requirement"; break;
    }
    text += "; rule " + std::to_string(t.rule) + " (" + labelOf(t.rule) + ") " + effect;
  }
  return text;
}

namespace templates {
Rule preserveFirst(std::uint32_t id, const RoleId& role) {
  Rule r;
  r.id = id;
  r.scope = RuleScope::Bar;
  r.when.role = role;
  r.when.position = Position::FirstInBar;
  r.action.kind = ActionKind::Preserve;
  r.label = "keep the first " + role + " in each bar";
  return r;
}
Rule avoidAdjacentRepeats(std::uint32_t id, RepeatBy by) {
  Rule r;
  r.id = id;
  r.scope = RuleScope::Phrase;
  r.action.kind = ActionKind::MaxConsecutiveRepeats;
  r.action.count = 1;
  r.action.repeatBy = by;
  r.label = by == RepeatBy::SameChop ? "no adjacent repeats of a chop" : "no adjacent repeats of a role";
  return r;
}
Rule requireInFinalBeat(std::uint32_t id, const RoleId& role) {
  Rule r;
  r.id = id;
  r.scope = RuleScope::Beat;
  r.when.role = role;
  r.when.position = Position::FinalBeat;
  r.action.kind = ActionKind::Require;
  r.label = "at least one " + role + " in the final beat";
  return r;
}
Rule preferAtPhraseBoundary(std::uint32_t id, const RoleId& role, double weight) {
  Rule r;
  r.id = id;
  r.scope = RuleScope::Event;
  r.when.role = role;
  r.when.position = Position::PhraseBoundary;
  r.action.kind = ActionKind::Prefer;
  r.weight = weight;
  r.label = "prefer " + role + " at phrase boundaries";
  return r;
}
Rule excludeRole(std::uint32_t id, const RoleId& role, int barMin, int barMax) {
  Rule r;
  r.id = id;
  r.scope = RuleScope::Region;
  r.when.role = role;
  r.when.barMin = barMin;
  r.when.barMax = barMax;
  r.action.kind = ActionKind::Forbid;
  r.priority = 10;
  r.label = "exclude " + role + " from selected bars";
  return r;
}
}  // namespace templates

std::vector<std::uint8_t> serialize(const RoleMap& roles, const RuleSet& rules) {
  std::vector<std::uint8_t> out;
  bytes::Writer w(out);
  w.u32(static_cast<std::uint32_t>(roles.all().size()));
  for (const auto& [chop, role] : roles.all()) {
    w.u64(chop);
    w.str(role);
  }
  w.u32(static_cast<std::uint32_t>(rules.rules.size()));
  for (const Rule& r : rules.rules) {
    w.u32(r.id);
    w.boolean(r.enabled);
    w.u8(static_cast<std::uint8_t>(r.scope));
    w.str(r.when.role);
    w.u64(r.when.chop.value);
    w.u8(static_cast<std::uint8_t>(r.when.position));
    w.i32(r.when.barMin);
    w.i32(r.when.barMax);
    w.i32(r.when.beatMin);
    w.i32(r.when.beatMax);
    w.u8(static_cast<std::uint8_t>(r.action.kind));
    w.i32(r.action.count);
    w.u8(static_cast<std::uint8_t>(r.action.repeatBy));
    w.i32(r.priority);
    w.f64(r.weight);
    w.str(r.label);
  }
  return out;
}

Result<State> deserialize(const std::uint8_t* data, std::size_t size) {
  bytes::Reader r(data, size);
  State s;
  const std::uint32_t nr = r.count(static_cast<std::uint32_t>(kMaxRoleAssignments), 12);
  for (std::uint32_t i = 0; i < nr; ++i) {
    const std::uint64_t chop = r.u64();
    const std::string role = r.str(kMaxRoleLength);
    if (!r.ok()) break;
    Status a = s.roles.assignRole(ChopId{chop}, role);
    if (!a.ok()) return makeError(ErrorCode::Corrupt, "role state is invalid: " + a.error().message);
  }
  const std::uint32_t n = r.count(static_cast<std::uint32_t>(kMaxRules), kMinRuleBytes);
  if (!r.ok()) return makeError(ErrorCode::Corrupt, "grammar state is malformed");
  for (std::uint32_t i = 0; i < n; ++i) {
    Rule rule;
    rule.id = r.u32();
    rule.enabled = r.boolean();
    const std::uint8_t scope = r.u8();
    rule.when.role = r.str(kMaxRoleLength);
    rule.when.chop = ChopId{r.u64()};
    const std::uint8_t position = r.u8();
    rule.when.barMin = r.i32();
    rule.when.barMax = r.i32();
    rule.when.beatMin = r.i32();
    rule.when.beatMax = r.i32();
    const std::uint8_t kind = r.u8();
    rule.action.count = r.i32();
    const std::uint8_t by = r.u8();
    rule.priority = r.i32();
    rule.weight = r.f64();
    rule.label = r.str(128);
    if (!r.ok()) return makeError(ErrorCode::Corrupt, "rule data is truncated");
    if (scope > static_cast<std::uint8_t>(RuleScope::Region) || position > static_cast<std::uint8_t>(Position::PhraseBoundary) ||
        kind > static_cast<std::uint8_t>(ActionKind::Prefer) || by > static_cast<std::uint8_t>(RepeatBy::SameRole))
      return makeError(ErrorCode::Corrupt, "rule uses an unknown enumeration value");
    rule.scope = static_cast<RuleScope>(scope);
    rule.when.position = static_cast<Position>(position);
    rule.action.kind = static_cast<ActionKind>(kind);
    rule.action.repeatBy = static_cast<RepeatBy>(by);
    s.rules.rules.push_back(std::move(rule));
  }
  if (r.remaining() != 0) return makeError(ErrorCode::Corrupt, "trailing bytes in grammar state");
  ValidationReport rep = validateRules(s.rules);
  if (!rep.ok()) return makeError(ErrorCode::Corrupt, "saved rules failed validation");
  return s;
}

}  // namespace chopfractal::roles
