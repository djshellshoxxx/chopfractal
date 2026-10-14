// Integration tests for the rule editor support (docs/specs/roadmap/F06-rule-editor.md).
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/state_codec/codec.hpp>
#include <cmath>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {

std::shared_ptr<const render::SourceData> loop() {
  auto d = std::make_shared<render::SourceData>();
  d->channels = 1;
  d->sampleRate = 48000;
  d->frames = 96000;
  d->samples.assign(96000, 0.f);
  Rng rng(3);
  for (int k = 0; k < 8; ++k)
    for (int i = 0; i < 5000; ++i)
      d->samples[static_cast<std::size_t>(k * 12000 + i)] = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * std::exp(-static_cast<float>(i) / 900.f) * 0.8f;
  return d;
}

pattern::Settings cfg(std::uint64_t seed = 1, int bars = 2) {
  pattern::Settings s;
  s.bars = bars;
  s.density = 0.8;
  s.seed = seed;
  return s;
}

bool ready(ProjectSession& s) {
  CHECK(s.loadSource(loop(), "loop", "/music/loop.wav").ok());
  DetectOptions d;
  d.analysis.sensitivity = 0.8;
  CHECK(s.applyChops(d).ok());
  return s.chops() && s.chops()->chops.size() >= 6;
}

}  // namespace

CHOP_TEST(explain_event_reports_weighting_protection_and_hand_placed_conflicts) {
  ProjectSession s;
  CHECK(ready(s));
  const auto& chops = s.chops()->chops;
  CHECK(s.assignRole(chops[0].id, "kick").ok());
  CHECK(s.assignRole(chops[1].id, "hat").ok());
  roles::RuleSet rs;
  roles::Rule prefer;
  prefer.id = 1;
  prefer.when.role = "kick";
  prefer.action.kind = roles::ActionKind::Prefer;
  prefer.weight = 2.0;
  prefer.label = "more kick";
  rs.rules.push_back(prefer);
  rs.rules.push_back(roles::templates::preserveFirst(2, "kick"));
  CHECK(s.setRules(rs).ok());
  CHECK(s.generate(cfg()).ok());
  EventId kickHit, otherHit;
  for (const auto& bar : s.pattern()->bars)
    for (const auto& bt : bar.beats)
      for (const Event& e : bt.events) {
        if (e.chop == chops[0].id && !kickHit.valid()) kickHit = e.id;
        if (e.chop != chops[0].id && !otherHit.valid()) otherHit = e.id;
      }
  if (kickHit.valid()) {
    auto x = s.explainEvent(kickHit);
    CHECK(x.ok());
    CHECK(x.value().text.rfind("Kept", 0) == 0);
    CHECK(x.value().text.find("Rule 1 multiplies its weight by x2") != std::string::npos);
    CHECK(!x.value().rules.empty() && std::is_sorted(x.value().rules.begin(), x.value().rules.end()));
  }
  if (otherHit.valid()) CHECK(s.explainEvent(otherHit).ok());
  CHECK(!s.explainEvent(EventId{999999}).ok());
  // a hat forbidden everywhere, but hand-placed: the explanation says Mutate keeps it
  roles::RuleSet forbid;
  forbid.rules.push_back(roles::templates::excludeRole(1, "hat", 0, 7));
  CHECK(s.setRules(forbid).ok());
  const Ticks g = gridTicks(s.pattern()->settings.grid);
  CHECK(s.edit([&](const pattern::Pattern& p, const ChopSnapshot& c) { return pattern::addEvent(p, 0, chops[1].id, g * 3, g, c); }).ok());
  EventId placed;
  for (const auto& bt : s.pattern()->bars[0].beats)
    for (const Event& e : bt.events)
      if (e.userOwned && e.chop == chops[1].id) placed = e.id;
  CHECK(placed.valid());
  auto x = s.explainEvent(placed);
  CHECK(x.ok());
  CHECK(!x.value().allowed && x.value().handPlaced);
  CHECK(x.value().text.find("You placed this hit by hand") != std::string::npos);
}

CHOP_TEST(explain_event_without_policy_says_nothing_constrains_it) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK(!s.explainEvent(EventId{1}).ok());  // no pattern yet
  CHECK(s.generate(cfg()).ok());
  const EventId id = s.pattern()->bars[0].beats[0].events.empty() ? s.pattern()->bars[0].beats[1].events.front().id : s.pattern()->bars[0].beats[0].events.front().id;
  auto x = s.explainEvent(id);
  CHECK(x.ok());
  CHECK(x.value().text == "No rules or roles are set, so nothing constrains this hit.");
}

CHOP_TEST(last_rule_notes_record_blocks_and_clear_on_reset_and_load) {
  ProjectSession s;
  CHECK(ready(s));
  const auto& chops = s.chops()->chops;
  for (std::size_t i = 0; i < chops.size(); ++i) CHECK(s.assignRole(chops[i].id, "hat").ok());
  roles::RuleSet rs;
  roles::Rule r = roles::templates::excludeRole(4, "hat", 0, 0);
  r.label = "no hats first bar";
  rs.rules.push_back(r);
  CHECK(s.setRules(rs).ok());
  CHECK(s.generate(cfg(3)).ok());
  CHECK(!s.lastRuleNotes().empty());
  CHECK(s.lastRuleNotes().front().rule == 4);
  CHECK(s.lastRuleNotes().front().text.find("rule 4 (\"no hats first bar\") blocked a change") != std::string::npos);
  CHECK(s.lastRuleNotes().size() <= 64);
  auto saved = s.saveState(true);
  CHECK(saved.ok());
  CHECK(s.loadState(saved.value().data(), saved.value().size(), nullptr).ok());
  CHECK(s.lastRuleNotes().empty());
  CHECK(s.generate(cfg(3)).ok());
  CHECK(!s.lastRuleNotes().empty());
  s.reset();
  CHECK(s.lastRuleNotes().empty());
}

CHOP_TEST(preview_rules_counts_blocks_without_changing_state_and_matches_generate) {
  ProjectSession s;
  CHECK(ready(s));
  const auto& chops = s.chops()->chops;
  for (std::size_t i = 0; i < chops.size(); ++i) CHECK(s.assignRole(chops[i].id, "hat").ok());
  CHECK(s.generate(cfg(2)).ok());
  const auto bytes = pattern::serialize(*s.pattern());
  const std::size_t nodes = s.history().size();
  s.takeNotices();
  roles::RuleSet draft;
  draft.rules.push_back(roles::templates::excludeRole(9, "hat", 0, 1));
  auto pv = s.previewRules(draft, s.pattern()->settings);
  CHECK(pv.ok());
  CHECK(pv.value().blocked > 0);
  CHECK(pv.value().blockedByRule.size() == 1 && pv.value().blockedByRule[0].first == 9);
  CHECK(pattern::serialize(*s.pattern()) == bytes);
  CHECK_EQ(s.history().size(), nodes);
  CHECK(s.takeNotices().empty());
  // the same rules applied for real report the same number of blocked changes
  CHECK(s.setRules(draft).ok());
  CHECK(s.generate(s.pattern()->settings).ok());
  CHECK_EQ(static_cast<int>(s.lastRuleNotes().size()), pv.value().blocked);
  roles::RuleSet bad;
  roles::Rule r;
  r.id = 1;
  r.action.kind = roles::ActionKind::MaxCount;
  r.action.count = 0;
  bad.rules.push_back(r);
  CHECK(!s.previewRules(bad, cfg()).ok());
}

CHOP_TEST(clear_role_removes_assignment_and_persists_through_save_load) {
  ProjectSession s;
  CHECK(ready(s));
  const ChopId c = s.chops()->chops[0].id;
  CHECK(s.assignRole(c, "snare").ok());
  CHECK(s.roleMap().roleOf(c) == "snare");
  auto saved = s.saveState(true);
  CHECK(saved.ok());
  CHECK(s.clearRole(c).ok());
  CHECK(s.roleMap().roleOf(c).empty());
  CHECK(s.clearRole(c).ok());  // already empty: fine
  CHECK(s.clearRole(ChopId{9999}).error().code == ErrorCode::NotFound);
  ProjectSession t;
  CHECK(t.loadState(saved.value().data(), saved.value().size(), nullptr).ok());
  CHECK(t.roleMap().roleOf(c) == "snare");
}

CHOP_TEST(rules_state_bytes_are_unchanged_by_this_feature) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK(s.assignRole(s.chops()->chops[0].id, "kick").ok());
  roles::RuleSet rs;
  rs.rules.push_back(roles::templates::preserveFirst(1, "kick"));
  CHECK(s.setRules(rs).ok());
  auto saved = s.saveState(true);
  CHECK(saved.ok());
  auto dec = codec::decode(saved.value().data(), saved.value().size());
  CHECK(dec.ok());
  const auto it = dec.value().modules.find("chop_roles_grammar");
  CHECK(it != dec.value().modules.end());
  if (it != dec.value().modules.end()) CHECK(it->second.bytes == roles::serialize(s.roleMap(), s.rules()));
}
