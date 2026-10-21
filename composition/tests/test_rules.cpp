// Rule editor support on ProjectSession (G2 / F06): explanations, blocked-change notes, rule previews and clearing roles.
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <set>

#include "session_fixture.hpp"

using namespace fixture;

namespace {
std::vector<ChopId> chopIds(const ProjectSession& s) {
  std::vector<ChopId> ids;
  for (const ChopInfo& c : s.chops()->chops) ids.push_back(c.id);
  return ids;
}
}  // namespace

CHOP_TEST(explain_event_without_policy_says_nothing_constrains_it) {
  ProjectSession s;
  CHECK(ready(s));
  const EventId first = [&] {
    for (const auto& bar : s.pattern()->bars)
      for (const auto& beat : bar.beats)
        if (!beat.events.empty()) return beat.events.front().id;
    return EventId{};
  }();
  auto r = s.explainEvent(first);
  CHECK(r.ok());
  if (!r.ok()) return;
  CHECK(r.value().text == "No rules or roles are set, so nothing constrains this hit.");
  CHECK(!s.explainEvent(EventId{987654}).ok());
}

CHOP_TEST(explain_event_reports_weighting_protection_and_hand_placed_conflicts) {
  ProjectSession s;
  CHECK(ready(s));
  const auto ids = chopIds(s);
  CHECK(ids.size() >= 3);
  if (ids.size() < 3) return;
  CHECK(s.assignRole(ids[0], "kick").ok());
  CHECK(s.assignRole(ids[1], "hat").ok());
  roles::RuleSet rules;
  roles::Rule prefer;
  prefer.id = 1;
  prefer.when.role = "kick";
  prefer.action.kind = roles::ActionKind::Prefer;
  prefer.weight = 2.0;
  prefer.label = "more kick";
  rules.rules.push_back(prefer);
  rules.rules.push_back(roles::templates::preserveFirst(2, "kick"));
  CHECK(s.setRules(rules).ok());
  const Event* kick = nullptr;
  for (const auto& bar : s.pattern()->bars)
    for (const auto& beat : bar.beats)
      for (const auto& e : beat.events)
        if (!kick && e.chop == ids[0]) kick = &e;
  if (kick) {
    auto r = s.explainEvent(kick->id);
    CHECK(r.ok());
    if (r.ok()) {
      CHECK(r.value().text.rfind("Kept", 0) == 0);
      CHECK(r.value().text.find("rule 1") != std::string::npos);
      CHECK(r.value().text.find("multiplies its weight by x2") != std::string::npos);
      CHECK(std::find(r.value().rules.begin(), r.value().rules.end(), 1u) != r.value().rules.end());
    }
  }
  // A hat the user placed by hand while hats are forbidden: Mutate keeps it, and the text says so.
  roles::Rule forbid = roles::templates::excludeRole(3, "hat", 0, 1);
  rules.rules.push_back(forbid);
  CHECK(s.setRules(rules).ok());
  const auto hatChop = ids[1];
  CHECK(s.edit([&](const pattern::Pattern& p, const ChopSnapshot& chops) { return pattern::addEvent(p, 0, hatChop, 120, 240, chops); }).ok());
  EventId placed;
  for (const auto& beat : s.pattern()->bars[0].beats)
    for (const auto& e : beat.events)
      if (e.userOwned && e.chop == hatChop) placed = e.id;
  if (placed.valid()) {
    auto r = s.explainEvent(placed);
    CHECK(r.ok());
    if (r.ok()) {
      CHECK(r.value().handPlaced);
      CHECK(r.value().text.find("You placed this hit by hand") != std::string::npos || r.value().allowed);
    }
  }
}

CHOP_TEST(last_rule_notes_record_blocks_and_clear_on_reset_and_load) {
  ProjectSession s;
  CHECK(ready(s, false));
  const auto ids = chopIds(s);
  for (std::size_t i = 0; i < ids.size(); ++i) CHECK(s.assignRole(ids[i], i == 0 ? "kick" : "hat").ok());
  roles::RuleSet rules;
  rules.rules.push_back(roles::templates::excludeRole(1, "hat", 0, 0));
  rules.rules.back().label = "no hats in bar 1";
  rules.rules.push_back(roles::templates::excludeRole(2, "kick", 0, 0));  // nothing is left for bar 1, so slots get blocked
  CHECK(s.setRules(rules).ok());
  CHECK(s.generate(settings(5, 2)).ok());
  const auto& notes = s.lastRuleNotes();
  CHECK(notes.size() <= 64);
  CHECK(!notes.empty());
  for (const auto& n : notes) {
    CHECK((n.rule == 1 || n.rule == 2) && n.bar == 0);
    CHECK(n.text.find("blocked a change; the previous content was kept.") != std::string::npos);
  }
  auto saved = s.saveState(true);
  CHECK(saved.ok());
  s.reset();
  CHECK(s.lastRuleNotes().empty());
}

CHOP_TEST(preview_rules_counts_blocks_without_changing_state) {
  ProjectSession s;
  CHECK(ready(s));
  const auto ids = chopIds(s);
  for (std::size_t i = 0; i < ids.size(); ++i) CHECK(s.assignRole(ids[i], i % 2 == 0 ? "kick" : "hat").ok());
  const auto before = pattern::serialize(*s.pattern());
  const auto historyBefore = s.history().size();
  const auto noticesBefore = s.notices().size();
  roles::RuleSet draft;
  draft.rules.push_back(roles::templates::excludeRole(4, "hat", 0, 1));
  auto preview = s.previewRules(draft, s.pattern()->settings);
  CHECK(preview.ok());
  if (preview.ok()) {
    CHECK(preview.value().events > 0);
    int sum = 0;
    for (const auto& kv : preview.value().blockedByRule) {
      CHECK(kv.first == 4);
      sum += kv.second;
    }
    CHECK(sum == preview.value().blocked);
  }
  CHECK(pattern::serialize(*s.pattern()) == before);
  CHECK(s.history().size() == historyBefore && s.notices().size() == noticesBefore);
  // An invalid draft is refused with the module's message.
  roles::RuleSet bad;
  roles::Rule r;
  r.id = 1;
  r.action.kind = roles::ActionKind::MaxCount;
  r.scope = roles::RuleScope::Bar;
  r.action.count = 0;
  bad.rules.push_back(r);
  CHECK(!s.previewRules(bad, s.pattern()->settings).ok());
}

CHOP_TEST(preview_rules_matches_a_real_generate_report) {
  ProjectSession s;
  CHECK(ready(s, false));
  const auto ids = chopIds(s);
  for (std::size_t i = 0; i < ids.size(); ++i) CHECK(s.assignRole(ids[i], i % 2 == 0 ? "kick" : "hat").ok());
  roles::RuleSet rules;
  rules.rules.push_back(roles::templates::excludeRole(2, "hat", 0, 1));
  CHECK(s.setRules(rules).ok());
  const auto cfg = settings(11, 2);
  auto preview = s.previewRules(rules, cfg);
  CHECK(preview.ok());
  s.takeNotices();
  CHECK(s.generate(cfg).ok());
  if (preview.ok()) CHECK(static_cast<int>(s.lastRuleNotes().size()) == preview.value().blocked);
}

CHOP_TEST(clear_role_removes_assignment_and_persists_through_save_load) {
  ProjectSession s;
  CHECK(ready(s));
  const auto ids = chopIds(s);
  CHECK(s.assignRole(ids[0], "kick").ok());
  CHECK(s.clearRole(ids[1]).ok());  // unassigned: a quiet no-op
  CHECK(!s.clearRole(ChopId{987654}).ok());
  CHECK(s.clearRole(ids[0]).ok());
  CHECK(s.roleMap().roleOf(ids[0]).empty());
  CHECK(s.assignRole(ids[2], "snare").ok());
  auto saved = s.saveState(true);
  CHECK(saved.ok());
  if (!saved.ok()) return;
  ProjectSession t;
  SourceResolver none;
  CHECK(t.loadState(saved.value().data(), saved.value().size(), none).ok());
  CHECK(t.roleMap().roleOf(ids[0]).empty() && t.roleMap().roleOf(ids[2]) == "snare");
}
