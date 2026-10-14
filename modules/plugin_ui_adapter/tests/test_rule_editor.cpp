#include <chopfractal/plugin_ui_adapter/rule_editor.hpp>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::ui;
using namespace chopfractal::roles;

namespace {
RuleEditorModel fresh(int bars = 4) {
  RuleEditorModel m;
  m.reset(RuleSet{}, RoleMap{}, bars, {ChopId{1}, ChopId{2}});
  return m;
}
}  // namespace

CHOP_TEST(add_rule_uses_next_free_id_and_never_reuses_after_delete) {
  auto m = fresh();
  CHECK_EQ(m.addRule(), 1u);
  CHECK_EQ(m.addRule(), 2u);
  CHECK_EQ(m.addRule(), 3u);
  CHECK(m.remove(2));
  CHECK(!m.remove(2));
  CHECK_EQ(m.addRule(), 4u);
  CHECK_EQ(m.find(4)->action.kind, ActionKind::Prefer);
  CHECK_EQ(m.find(4)->weight, 2.0);
}

CHOP_TEST(templates_build_the_same_rules_as_the_module_builders) {
  auto m = fresh();
  const auto a = m.addFromTemplate(TemplateKind::KeepFirst, "kick", 1, 2);
  const auto b = m.addFromTemplate(TemplateKind::NoRepeats, "kick", 1, 2);
  const auto c = m.addFromTemplate(TemplateKind::NoRoleRepeats, "kick", 1, 2);
  const auto d = m.addFromTemplate(TemplateKind::NeedInFinalBeat, "snare", 1, 2);
  const auto e = m.addFromTemplate(TemplateKind::PreferAtBoundaries, "hat", 1, 2);
  const auto f = m.addFromTemplate(TemplateKind::ExcludeFromBars, "hat", 3, 4);
  CHECK(describe(*m.find(a)) == describe(templates::preserveFirst(a, "kick")));
  CHECK(m.find(b)->action.repeatBy == RepeatBy::SameChop && m.find(c)->action.repeatBy == RepeatBy::SameRole);
  CHECK(describe(*m.find(d)) == "Require snare in the final beat");
  CHECK(m.find(e)->weight == 3.0);
  CHECK(m.find(f)->when.barMin == 2 && m.find(f)->when.barMax == 3);  // one-based 3-4 stored zero-based
  CHECK(m.issues().empty() || m.canApply());
}

CHOP_TEST(exclude_template_rejects_bad_bar_ranges) {
  auto m = fresh();
  CHECK_EQ(m.addFromTemplate(TemplateKind::ExcludeFromBars, "hat", 3, 2), 0u);
  CHECK_EQ(m.addFromTemplate(TemplateKind::ExcludeFromBars, "hat", 0, 2), 0u);
  CHECK_EQ(m.addFromTemplate(TemplateKind::ExcludeFromBars, "hat", 1, 9), 0u);
  CHECK(m.draft().rules.empty());
}

CHOP_TEST(update_derives_scope_from_action_and_rows_are_in_engine_order) {
  auto m = fresh();
  const auto a = m.addRule(), b = m.addRule(), c = m.addRule();
  Rule r = *m.find(b);
  r.priority = 10;
  r.action.kind = ActionKind::Forbid;
  r.when.role = "hat";
  CHECK(m.update(r));
  CHECK(m.find(b)->scope == RuleScope::Region);
  r.id = 99;
  CHECK(!m.update(r));
  Rule r3 = *m.find(c);
  r3.priority = -5;
  m.update(r3);
  const auto rows = m.rows();
  CHECK_EQ(rows.size(), 3u);
  CHECK_EQ(rows[0].id, b);  // priority 10 first
  CHECK_EQ(rows[1].id, a);
  CHECK_EQ(rows[2].id, c);
  CHECK(rows[0].text.rfind("[x] #" + std::to_string(b) + "  Forbid hat P10", 0) == 0);
  CHECK(rows[0].tooltip == "Forbid hat");
}

CHOP_TEST(issues_include_module_errors_and_ui_warnings_and_gate_apply) {
  auto m = fresh(4);
  const auto id = m.addRule();
  CHECK(m.dirty());
  CHECK(m.canApply() || !m.issues().empty());
  Rule r = *m.find(id);
  r.action.kind = ActionKind::MaxCount;
  r.action.count = 0;
  m.update(r);
  bool error = false;
  for (const auto& i : m.issues()) error |= i.error;
  CHECK(error);
  CHECK(!m.canApply());
  CHECK(m.rows()[0].hasError && m.rows()[0].text.find("Error") != std::string::npos);
  r.action.count = 2;
  r.when.barMin = 5;  // outside a 4-bar pattern
  r.when.chop = ChopId{77};
  m.update(r);
  int warnings = 0;
  for (const auto& i : m.issues()) warnings += !i.error && (i.text.find("outside this 4-bar pattern") != std::string::npos || i.text.find("no longer exists") != std::string::npos);
  CHECK_EQ(warnings, 2);
  CHECK(m.canApply());
  m.markApplied();
  CHECK(!m.dirty() && !m.canApply());
}

CHOP_TEST(limit_of_64_disables_add_and_duplicate_truncates_label) {
  auto m = fresh();
  for (int i = 0; i < 64; ++i) CHECK(m.addRule() != 0);
  CHECK(!m.canAdd());
  CHECK_EQ(m.addRule(), 0u);
  CHECK_EQ(m.duplicate(1), 0u);
  CHECK(m.remove(64));
  Rule r = *m.find(1);
  r.label = std::string(125, 'x');
  m.update(r);
  const auto copy = m.duplicate(1);
  CHECK(copy != 0);
  CHECK_EQ(m.find(copy)->label.size(), 128u);
  CHECK(m.find(copy)->action.kind == m.find(1)->action.kind);
}
