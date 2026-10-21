#include <chopfractal/plugin_ui_adapter/rule_editor.hpp>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::ui;

namespace {
roles::RoleMap someRoles() {
  roles::RoleMap m;
  m.assignRole(ChopId{1}, "kick");
  m.assignRole(ChopId{2}, "hat");
  return m;
}
RuleEditorModel fresh(int bars = 4) {
  RuleEditorModel m;
  m.reset({}, someRoles(), bars, {ChopId{1}, ChopId{2}});
  return m;
}
void sameFields(const roles::Rule& a, const roles::Rule& b) {
  CHECK(a.scope == b.scope && a.when.role == b.when.role && a.when.position == b.when.position && a.when.barMin == b.when.barMin && a.when.barMax == b.when.barMax);
  CHECK(a.action.kind == b.action.kind && a.action.count == b.action.count && a.action.repeatBy == b.action.repeatBy && a.weight == b.weight && a.label == b.label);
}
}  // namespace

CHOP_TEST(add_rule_uses_next_free_id_and_never_reuses_after_delete_within_session) {
  auto m = fresh();
  CHECK(m.addRule() == 1 && m.addRule() == 2 && m.addRule() == 3);
  CHECK(m.remove(2));
  CHECK(m.addRule() == 4);
  CHECK(m.remove(4));
  CHECK(m.addRule() == 5);  // the high-water mark survives deleting the newest rule
  CHECK(!m.remove(99));
}

CHOP_TEST(templates_build_the_same_rules_as_the_module_builders) {
  auto m = fresh();
  const auto a = m.addFromTemplate(TemplateKind::KeepFirst, "kick", 1, 2);
  const auto b = m.addFromTemplate(TemplateKind::NoRepeats, "kick", 1, 2);
  const auto c = m.addFromTemplate(TemplateKind::NoRoleRepeats, "kick", 1, 2);
  const auto d = m.addFromTemplate(TemplateKind::NeedInFinalBeat, "snare", 1, 2);
  const auto e = m.addFromTemplate(TemplateKind::PreferAtBoundaries, "snare", 1, 2);
  const auto f = m.addFromTemplate(TemplateKind::ExcludeFromBars, "hat", 3, 4);
  CHECK(a && b && c && d && e && f);
  sameFields(*m.find(a), roles::templates::preserveFirst(a, "kick"));
  sameFields(*m.find(b), roles::templates::avoidAdjacentRepeats(b, roles::RepeatBy::SameChop));
  sameFields(*m.find(c), roles::templates::avoidAdjacentRepeats(c, roles::RepeatBy::SameRole));
  sameFields(*m.find(d), roles::templates::requireInFinalBeat(d, "snare"));
  sameFields(*m.find(e), roles::templates::preferAtPhraseBoundary(e, "snare", 3.0));
  sameFields(*m.find(f), roles::templates::excludeRole(f, "hat", 2, 3));
}

CHOP_TEST(exclude_template_converts_one_based_bars_and_rejects_bad_ranges) {
  auto m = fresh(8);
  const auto id = m.addFromTemplate(TemplateKind::ExcludeFromBars, "hat", 1, 2);
  CHECK(m.find(id)->when.barMin == 0 && m.find(id)->when.barMax == 1);
  CHECK(m.addFromTemplate(TemplateKind::ExcludeFromBars, "hat", 3, 2) == 0);
  CHECK(m.addFromTemplate(TemplateKind::ExcludeFromBars, "hat", 0, 2) == 0);
  CHECK(m.addFromTemplate(TemplateKind::ExcludeFromBars, "hat", 1, 9) == 0);
  CHECK(m.draft().rules.size() == 1);
}

CHOP_TEST(update_derives_scope_from_action) {
  auto m = fresh();
  const auto id = m.addRule();
  roles::Rule r = *m.find(id);
  const std::pair<roles::ActionKind, roles::RuleScope> expect[] = {
      {roles::ActionKind::Preserve, roles::RuleScope::Bar},  {roles::ActionKind::Require, roles::RuleScope::Beat},
      {roles::ActionKind::Forbid, roles::RuleScope::Region}, {roles::ActionKind::MaxConsecutiveRepeats, roles::RuleScope::Phrase},
      {roles::ActionKind::MaxCount, roles::RuleScope::Bar},  {roles::ActionKind::Prefer, roles::RuleScope::Event}};
  for (const auto& e : expect) {
    r.action.kind = e.first;
    r.scope = roles::RuleScope::Phrase;  // whatever the caller sends
    CHECK(m.update(r));
    CHECK(m.find(id)->scope == e.second);
  }
  r.id = 77;
  CHECK(!m.update(r));
}

CHOP_TEST(rows_are_in_engine_order) {
  auto m = fresh();
  const auto a = m.addRule(), b = m.addRule(), c = m.addRule();
  roles::Rule r = *m.find(b);
  r.priority = 10;
  m.update(r);
  r = *m.find(c);
  r.priority = -5;
  m.update(r);
  const auto rows = m.rows();
  CHECK(rows.size() == 3 && rows[0].id == b && rows[1].id == a && rows[2].id == c);
  CHECK(rows[0].text.find("[x] #2") == 0 && rows[0].text.find(" P10") != std::string::npos);
}

CHOP_TEST(issues_include_module_errors_and_ui_warnings) {
  auto m = fresh(4);
  const auto id = m.addRule();
  roles::Rule r = *m.find(id);
  r.action.kind = roles::ActionKind::MaxCount;
  r.action.count = 0;
  m.update(r);
  bool found = false;
  for (const auto& i : m.issues()) found = found || (i.error && i.rule == id && i.text.find("count limit must be at least 1") != std::string::npos);
  CHECK(found);
  CHECK(m.issueSummary() == "1 problem (1 error)");
  CHECK(m.rows()[0].hasError && m.rows()[0].text.find("  Error") != std::string::npos);
  r.action.count = 2;
  r.when.barMin = 5;  // outside a 4-bar pattern
  m.update(r);
  bool outside = false;
  for (const auto& i : m.issues()) outside = outside || (!i.error && i.text.find("outside this 4-bar pattern") != std::string::npos);
  CHECK(outside);
  r.when.barMin = 0;
  r.when.chop = ChopId{9};  // not in the chop list
  m.update(r);
  bool gone = false;
  for (const auto& i : m.issues()) gone = gone || i.text.find("chop #9 no longer exists") != std::string::npos;
  CHECK(gone);
  CHECK(fresh().issueSummary() == "No problems");
}

CHOP_TEST(apply_is_blocked_by_errors_and_enabled_by_fixes) {
  auto m = fresh();
  CHECK(!m.canApply());  // nothing changed
  const auto id = m.addRule();
  CHECK(m.dirty() && m.canApply());
  roles::Rule r = *m.find(id);
  r.action.kind = roles::ActionKind::MaxConsecutiveRepeats;
  r.action.count = 0;
  m.update(r);
  CHECK(m.dirty() && !m.canApply());
  r.action.count = 1;
  m.update(r);
  CHECK(m.canApply());
  m.markApplied();
  CHECK(!m.dirty() && !m.canApply());
}

CHOP_TEST(limit_of_64_disables_add) {
  auto m = fresh();
  for (std::size_t i = 0; i < roles::kMaxRules; ++i) CHECK(m.addRule() != 0);
  CHECK(!m.canAdd() && m.addRule() == 0 && m.duplicate(1) == 0 && m.addFromTemplate(TemplateKind::KeepFirst, "kick", 1, 1) == 0);
  CHECK(m.remove(5) && m.canAdd());
}

CHOP_TEST(duplicate_copies_fields_and_truncates_label_to_128) {
  auto m = fresh();
  const auto id = m.addFromTemplate(TemplateKind::ExcludeFromBars, "hat", 2, 3);
  roles::Rule r = *m.find(id);
  r.label = std::string(125, 'x');
  r.priority = 7;
  m.update(r);
  const auto copy = m.duplicate(id);
  CHECK(copy != 0 && copy != id);
  const auto* c = m.find(copy);
  CHECK(c && c->label.size() == 128 && c->priority == 7 && c->when.role == "hat" && c->when.barMin == 1 && c->when.barMax == 2);
  CHECK(m.duplicate(99) == 0);
}
