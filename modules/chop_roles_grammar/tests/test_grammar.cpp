#include <chopfractal/chop_roles_grammar/grammar.hpp>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::roles;

namespace {

// Chops: 1=kick, 2=snare, 3=hat, 4=hat, 5=accent.
RoleMap makeRoles() {
  RoleMap m;
  m.assignRole(ChopId{1}, "kick");
  m.assignRole(ChopId{2}, "snare");
  m.assignRole(ChopId{3}, "hat");
  m.assignRole(ChopId{4}, "hat");
  m.assignRole(ChopId{5}, "accent");
  return m;
}
std::vector<ChopId> allChops() { return {ChopId{1}, ChopId{2}, ChopId{3}, ChopId{4}, ChopId{5}}; }

GrammarPolicy policyOf(std::vector<Rule> rules) {
  RuleSet rs;
  rs.rules = std::move(rules);
  return GrammarPolicy(makeRoles(), rs, allChops());
}

CandidateQuery query(std::uint64_t chop, int bar = 0, int beat = 0, bool firstInBar = false) {
  CandidateQuery q;
  q.chop = ChopId{chop};
  q.scope.bar = bar;
  q.scope.barCount = 4;
  q.scope.beat = beat;
  q.scope.beatsPerBar = 4;
  q.firstInBar = firstInBar;
  return q;
}

Rule rule(std::uint32_t id, ActionKind kind, const char* role = "", int priority = 0) {
  Rule r;
  r.id = id;
  r.action.kind = kind;
  r.when.role = role;
  r.priority = priority;
  return r;
}

}  // namespace

CHOP_TEST(role_ids_and_assignments_are_validated) {
  CHECK(isValidRoleId("kick") && isValidRoleId("my-custom_role2"));
  CHECK(!isValidRoleId("") && !isValidRoleId("Kick") && !isValidRoleId("has space") && !isValidRoleId(std::string(33, 'a')));
  for (const RoleId& r : builtinRoles()) CHECK(isValidRoleId(r));
  RoleMap m;
  CHECK(m.assignRole(ChopId{1}, "kick").ok());
  CHECK(!m.assignRole(ChopId{1}, "BAD ROLE").ok());
  CHECK(!m.assignRole(ChopId{}, "kick").ok());
  CHECK(m.roleOf(ChopId{1}) == "kick" && m.roleOf(ChopId{9}).empty());
  m.retainOnly({ChopId{2}});
  CHECK(m.roleOf(ChopId{1}).empty());
}

CHOP_TEST(rule_validation_flags_errors_and_warns_on_conflicts) {
  RuleSet rs;
  CHECK(validateRules(rs).ok());
  Rule bad = rule(0, ActionKind::Forbid);  // id 0 is invalid
  rs.rules = {bad};
  CHECK(!validateRules(rs).ok());
  Rule a = rule(1, ActionKind::Forbid, "hat");
  Rule dup = a;
  rs.rules = {a, dup};
  CHECK(!validateRules(rs).ok());

  Rule count = rule(2, ActionKind::MaxCount, "hat");
  count.action.count = 2;
  count.scope = RuleScope::Event;  // wrong scope
  rs.rules = {count};
  CHECK(!validateRules(rs).ok());
  count.scope = RuleScope::Bar;
  rs.rules = {count};
  CHECK(validateRules(rs).ok());

  Rule req = rule(3, ActionKind::Require, "kick");
  req.scope = RuleScope::Phrase;  // requirements are per beat
  rs.rules = {req};
  CHECK(!validateRules(rs).ok());
  req.scope = RuleScope::Beat;
  Rule roleless = rule(4, ActionKind::Require);
  roleless.scope = RuleScope::Beat;
  rs.rules = {roleless};
  CHECK(!validateRules(rs).ok());

  Rule weird = rule(5, ActionKind::Prefer, "BadRole");
  rs.rules = {weird};
  CHECK(!validateRules(rs).ok());
  weird = rule(5, ActionKind::Prefer, "hat");
  weird.weight = -1.0;
  rs.rules = {weird};
  CHECK(!validateRules(rs).ok());
  weird.weight = std::nan("");
  rs.rules = {weird};
  CHECK(!validateRules(rs).ok());

  Rule forbidKick = rule(6, ActionKind::Forbid, "kick");
  req = rule(7, ActionKind::Require, "kick");
  req.scope = RuleScope::Beat;
  rs.rules = {forbidKick, req};
  ValidationReport rep = validateRules(rs);
  bool warned = false;
  for (const Issue& i : rep.issues) warned = warned || i.severity == Issue::Severity::Warning;
  CHECK(rep.ok() && warned);  // contradictory, but runtime-safe: only a warning

  RuleSet many;
  for (std::uint32_t i = 1; i <= kMaxRules + 1; ++i) many.rules.push_back(rule(i, ActionKind::Prefer));
  CHECK(!validateRules(many).ok());
}

CHOP_TEST(hard_constraints_reject_and_trace_the_deciding_rule) {
  GrammarPolicy p = policyOf({rule(1, ActionKind::Forbid, "hat")});
  Decision d = p.evaluate(query(3));
  CHECK(!d.allowed && d.blockedBy == 1 && d.weight == 0.0);
  CHECK(d.trace.size() == 1 && d.trace[0].effect == TraceEntry::Effect::Rejected);
  CHECK(p.evaluate(query(1)).allowed);
  CHECK(p.explain(d).find("rule 1") != std::string::npos && p.explain(d).find("rejected") != std::string::npos);

  // Region-limited exclusion: only bars 1..2.
  GrammarPolicy region = policyOf({templates::excludeRole(2, "snare", 1, 2)});
  CHECK(region.evaluate(query(2, 0)).allowed);
  CHECK(!region.evaluate(query(2, 1)).allowed && !region.evaluate(query(2, 2)).allowed);
  CHECK(region.evaluate(query(2, 3)).allowed);

  // Position-limited forbid: no kick in the final beat.
  Rule noKickFinal = rule(3, ActionKind::Forbid, "kick");
  noKickFinal.when.position = Position::FinalBeat;
  GrammarPolicy pos = policyOf({noKickFinal});
  CHECK(pos.evaluate(query(1, 0, 2)).allowed && !pos.evaluate(query(1, 0, 3)).allowed);
}

CHOP_TEST(adjacent_repeat_and_count_limits) {
  GrammarPolicy p = policyOf({templates::avoidAdjacentRepeats(1, RepeatBy::SameChop)});
  CandidateQuery q = query(3);
  q.recent = {ChopId{2}, ChopId{3}};
  CHECK(!p.evaluate(q).allowed);
  q.recent = {ChopId{3}, ChopId{2}};
  CHECK(p.evaluate(q).allowed);
  q.recent.clear();
  CHECK(p.evaluate(q).allowed);

  GrammarPolicy byRole = policyOf({templates::avoidAdjacentRepeats(1, RepeatBy::SameRole)});
  q = query(4);  // a different hat chop, but the same role as the previous one
  q.recent = {ChopId{3}};
  CHECK(!byRole.evaluate(q).allowed);
  q = query(1);
  q.recent = {ChopId{3}};
  CHECK(byRole.evaluate(q).allowed);

  Rule two = rule(2, ActionKind::MaxConsecutiveRepeats, "hat");
  two.action.count = 2;
  GrammarPolicy limit2 = policyOf({two});
  q = query(3);
  q.recent = {ChopId{3}};
  CHECK(limit2.evaluate(q).allowed);
  q.recent = {ChopId{3}, ChopId{3}};
  CHECK(!limit2.evaluate(q).allowed);

  Rule maxCount = rule(3, ActionKind::MaxCount, "hat");
  maxCount.scope = RuleScope::Bar;
  maxCount.action.count = 2;
  GrammarPolicy counted = policyOf({maxCount});
  q = query(3);
  q.barSoFar = {ChopId{3}, ChopId{1}};
  CHECK(counted.evaluate(q).allowed);
  q.barSoFar = {ChopId{3}, ChopId{4}};
  CHECK(!counted.evaluate(q).allowed);
  CHECK(counted.evaluate(query(1)).allowed);
}

CHOP_TEST(soft_preferences_multiply_deterministically_and_never_override_hard_rules) {
  Rule prefer = rule(1, ActionKind::Prefer, "snare");
  prefer.weight = 3.0;
  Rule avoid = rule(2, ActionKind::Prefer, "snare");
  avoid.weight = 0.5;
  GrammarPolicy p = policyOf({prefer, avoid});
  Decision d = p.evaluate(query(2));
  CHECK(d.allowed && d.weight == 1.5 && d.trace.size() == 2);
  CHECK(p.evaluate(query(2)).weight == d.weight);  // pure
  CHECK(p.evaluate(query(1)).weight == 1.0);

  GrammarPolicy both = policyOf({prefer, rule(3, ActionKind::Forbid, "snare")});
  CHECK(!both.evaluate(query(2)).allowed);

  GrammarPolicy boundary = policyOf({templates::preferAtPhraseBoundary(4, "accent", 4.0)});
  CHECK(boundary.evaluate(query(5, 0, 0)).weight == 4.0);   // phrase start
  CHECK(boundary.evaluate(query(5, 3, 3)).weight == 4.0);   // phrase end
  CHECK(boundary.evaluate(query(5, 1, 1)).weight == 1.0);   // mid-phrase
}

CHOP_TEST(priority_resolves_prohibition_versus_requirement_deterministically) {
  Rule forbid = rule(1, ActionKind::Forbid, "kick", /*priority=*/1);
  Rule require = templates::requireInFinalBeat(2, "kick");
  require.priority = 5;
  GrammarPolicy higherRequire = policyOf({forbid, require});
  Decision d = higherRequire.evaluate(query(1, 0, 3));
  CHECK(d.allowed);
  bool conflictTraced = false;
  for (const TraceEntry& t : d.trace) conflictTraced = conflictTraced || t.effect == TraceEntry::Effect::Conflict;
  CHECK(conflictTraced);
  CHECK(!higherRequire.evaluate(query(1, 0, 1)).allowed);  // outside the final beat the prohibition still holds

  require.priority = 1;  // equal priority: the prohibition wins
  GrammarPolicy tie = policyOf({forbid, require});
  CHECK(!tie.evaluate(query(1, 0, 3)).allowed);
  require.priority = 0;
  CHECK(!policyOf({forbid, require}).evaluate(query(1, 0, 3)).allowed);
}

CHOP_TEST(requirements_resolve_to_a_chop_only_when_unsatisfied_and_in_scope) {
  GrammarPolicy p = policyOf({templates::requireInFinalBeat(1, "accent")});
  ScopeContext sc;
  sc.level = ScopeLevel::Beat;
  sc.bar = 0;
  sc.barCount = 4;
  sc.beat = 3;
  sc.beatsPerBar = 4;
  auto req = p.required(sc, {ChopId{1}});
  CHECK(req.size() == 1 && req[0].chop == ChopId{5} && req[0].rule == 1);
  CHECK(p.required(sc, {ChopId{1}, ChopId{5}}).empty());  // already satisfied
  sc.beat = 1;
  CHECK(p.required(sc, {}).empty());                       // not the final beat

  GrammarPolicy noAccentChop(RoleMap{}, RuleSet{{templates::requireInFinalBeat(1, "accent")}}, allChops());
  sc.beat = 3;
  CHECK(noAccentChop.required(sc, {}).empty());  // no chop has the role, so nothing can be required

  Rule byChop = rule(2, ActionKind::Require);
  byChop.scope = RuleScope::Beat;
  byChop.when.chop = ChopId{4};
  CHECK(policyOf({byChop}).required(sc, {}).size() == 1);
}

CHOP_TEST(preserve_rules_keep_matching_events) {
  GrammarPolicy p = policyOf({templates::preserveFirst(1, "kick")});
  CHECK(p.isPreserved(query(1, 0, 0, /*firstInBar=*/true)));
  CHECK(!p.isPreserved(query(1, 0, 1, /*firstInBar=*/false)));
  CHECK(!p.isPreserved(query(2, 0, 0, /*firstInBar=*/true)));
  Rule disabled = templates::preserveFirst(2, "snare");
  disabled.enabled = false;
  CHECK(!policyOf({disabled}).isPreserved(query(2, 0, 0, true)));
}

CHOP_TEST(state_roundtrips_and_malformed_state_is_rejected) {
  RuleSet rs;
  rs.rules = {templates::preserveFirst(1, "kick"), templates::requireInFinalBeat(2, "accent"), templates::excludeRole(3, "hat", 1, 2),
              templates::avoidAdjacentRepeats(4)};
  rs.rules[2].weight = 2.5;
  const auto blob = serialize(makeRoles(), rs);
  auto back = deserialize(blob.data(), blob.size());
  CHECK(back.ok());
  if (back.ok()) {
    CHECK(serialize(back.value().roles, back.value().rules) == blob);
    CHECK(back.value().roles.roleOf(ChopId{1}) == "kick" && back.value().rules.rules.size() == 4);
    CHECK(back.value().rules.rules[2].weight == 2.5);
  }
  for (std::size_t cut = 0; cut < blob.size(); ++cut) CHECK(!deserialize(blob.data(), cut).ok());
  for (std::size_t i = 0; i < blob.size(); ++i) {
    auto bad = blob;
    bad[i] ^= 0x77;
    auto r = deserialize(bad.data(), bad.size());
    if (r.ok()) CHECK(validateRules(r.value().rules).ok());  // survivors are still valid rule sets
  }
}
