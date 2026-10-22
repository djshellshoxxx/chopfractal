// Rule editor drawer (G2 / F06) through the real editor: templates, Apply gating, Why?, the Roles tab and accessible titles.
#include <algorithm>
#include <memory>

#include "EditorModel.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "RulesPanel.h"
#include "shell_test_support.hpp"

using namespace shelltest;

namespace {
struct RulesFixture {
  ChopFractalProcessor p;
  std::unique_ptr<juce::AudioProcessorEditor> editor;
  ChopFractalEditor* ed = nullptr;
  RulesPanel* panel = nullptr;
  std::vector<cf::ChopId> chops;
  bool open() {
    prepare(p);
    makeSession(p);
    p.withSession([&](cf::composition::ProjectSession& s) {
      for (const auto& c : s.chops()->chops) chops.push_back(c.id);
      for (std::size_t i = 0; i < chops.size(); ++i) s.assignRole(chops[i], i == 0 ? "kick" : (i == 1 ? "hat" : "snare"));
    });
    editor.reset(p.createEditor());
    if (!editor) return false;
    editor->setSize(1100, 720);
    editor->setVisible(true);
    pump(300);
    ed = dynamic_cast<ChopFractalEditor*>(editor.get());
    if (!ed) return false;
    ed->openDrawer("rules");
    pump(100);
    panel = dynamic_cast<RulesPanel*>(findById(*editor, "RulesPanel"));
    return panel != nullptr;
  }
  std::size_t appliedRules() {
    std::size_t n = 0;
    p.withSession([&](cf::composition::ProjectSession& s) { n = s.rules().rules.size(); });
    return n;
  }
  juce::String status() {
    return ed->model().statusLine();  // the status bar shows the message on one elided line, so read the model
  }
};
}  // namespace

CHOP_TEST(rules_panel_template_apply_and_generate) {
  if (!haveDisplay()) return;
  RulesFixture f;
  CHECK(f.open());
  if (!f.panel) return;
  auto* role = findComboBox(*f.editor, "Template role");
  auto* from = findTextEditor(*f.editor, "Template bar from");
  auto* to = findTextEditor(*f.editor, "Template bar to");
  CHECK(role && from && to);
  if (!role || !from || !to) return;
  role->setText("hat", juce::dontSendNotification);
  from->setText("1", false);
  to->setText("2", false);
  findButton(*f.editor, "Exclude from bars")->onClick();
  pump(60);
  CHECK(f.panel->draft().rows().size() == 1);
  CHECK(f.panel->draft().issueSummary() == "No problems");
  CHECK(f.appliedRules() == 0);  // nothing reaches the session before Apply
  auto* apply = findButton(*f.editor, "Apply");
  CHECK(apply && apply->isEnabled());
  if (!apply) return;
  apply->onClick();
  pump(60);
  CHECK(f.appliedRules() == 1);
  f.p.withSession([&](cf::composition::ProjectSession& s) {
    const auto& r = s.rules().rules.front();
    CHECK(r.action.kind == cf::roles::ActionKind::Forbid && r.when.role == "hat" && r.when.barMin == 0 && r.when.barMax == 1);
  });
  CHECK(f.status().contains("Rules applied."));
  f.ed->commands().generate();
  pump(100);
  f.p.withSession([&](cf::composition::ProjectSession& s) {
    const cf::ChopId hat = f.chops.size() > 1 ? f.chops[1] : cf::ChopId{};
    for (std::size_t b = 0; b < s.pattern()->bars.size() && b < 2; ++b)
      for (const auto& beat : s.pattern()->bars[b].beats)
        for (const auto& e : beat.events) CHECK(e.chop != hat);
  });
  CHECK(!findButton(*f.editor, "Apply")->isEnabled());  // applied and unchanged
}

CHOP_TEST(invalid_rule_blocks_apply_and_revert_restores) {
  if (!haveDisplay()) return;
  RulesFixture f;
  CHECK(f.open());
  if (!f.panel) return;
  findButton(*f.editor, "Add Rule")->onClick();
  pump(60);
  CHECK(f.panel->draft().rows().size() == 1);
  auto* action = findComboBox(*f.editor, "Action");
  auto* count = findTextEditor(*f.editor, "Count");
  CHECK(action != nullptr);
  if (!action) return;
  action->setSelectedId(5, juce::sendNotificationSync);  // Limit count per bar
  pump(30);
  count = findTextEditor(*f.editor, "Count");
  CHECK(count != nullptr);
  if (!count) return;
  count->setText("0", true);
  pump(30);
  CHECK(f.panel->draft().issueSummary() == "1 problem (1 error)");
  bool found = false;
  for (const auto& i : f.panel->draft().issues()) found = found || i.text.find("count limit must be at least 1") != std::string::npos;
  CHECK(found);
  CHECK(!findButton(*f.editor, "Apply")->isEnabled());
  findButton(*f.editor, "Revert")->onClick();
  pump(60);
  CHECK(f.panel->draft().rows().empty());
  CHECK(f.appliedRules() == 0);
  CHECK(f.status().contains("Reverted to the applied rules."));
}

CHOP_TEST(why_button_explains_the_selected_hit) {
  if (!haveDisplay()) return;
  RulesFixture f;
  CHECK(f.open());
  if (!f.panel) return;
  auto* why = findButton(*f.editor, "Why?");
  CHECK(why != nullptr);
  if (!why) return;
  why->onClick();
  CHECK(f.status().contains("Select a hit first; Why? explains that hit."));
  f.p.withSession([&](cf::composition::ProjectSession& s) {
    cf::roles::RuleSet rules;
    cf::roles::Rule prefer;
    prefer.id = 1;
    prefer.when.role = "kick";
    prefer.action.kind = cf::roles::ActionKind::Prefer;
    prefer.weight = 2.0;
    rules.rules.push_back(prefer);
    CHECK(s.setRules(rules).ok());
    CHECK(s.generate(s.pattern()->settings).ok());
  });
  pump(100);
  const auto rects = f.ed->hitRects();
  CHECK(!rects.empty());
  if (rects.empty()) return;
  f.ed->model().select(rects.front().id);
  pump(30);
  why->onClick();
  const auto text = f.status();
  CHECK(text.startsWith("Kept") || text.startsWith("Rejected"));
  f.ed->commands().say("");  // the callout stays open in the test; the next test builds a fresh editor
}

CHOP_TEST(roles_tab_assigns_and_clears) {
  if (!haveDisplay()) return;
  RulesFixture f;
  CHECK(f.open());
  if (!f.panel) return;
  findButton(*f.editor, "Roles")->onClick();
  pump(100);
  auto* box = findComboBox(*f.editor, "Role for chop 1");
  CHECK(box != nullptr);
  if (!box) return;
  box->setText("snare", juce::sendNotificationSync);
  pump(30);
  f.p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.roleMap().roleOf(f.chops[0]) == "snare"); });
  box = findComboBox(*f.editor, "Role for chop 1");
  CHECK(box != nullptr);
  if (!box) return;
  box->setText("(none)", juce::sendNotificationSync);
  pump(30);
  f.p.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.roleMap().roleOf(f.chops[0]).empty()); });
}

CHOP_TEST(every_rules_panel_control_has_an_accessible_title) {
  if (!haveDisplay()) return;
  RulesFixture f;
  CHECK(f.open());
  if (!f.panel) return;
  findButton(*f.editor, "Add Rule")->onClick();
  pump(60);
  int checked = 0;
  forEachControl(*f.panel, [&](juce::Component& c) {
    const bool control = dynamic_cast<juce::Button*>(&c) || dynamic_cast<juce::ComboBox*>(&c) || dynamic_cast<juce::Slider*>(&c) || dynamic_cast<juce::TextEditor*>(&c);
    if (!control || !c.isVisible()) return;
    ++checked;
    if (c.getTitle().isEmpty()) std::fprintf(stderr, "untitled control at %d,%d\n", c.getX(), c.getY());
    CHECK(c.getTitle().isNotEmpty());
  });
  CHECK(checked > 20);
}

// G2: unapplied draft rules are flagged in the status bar once the drawer is closed, and the flag goes with the draft.
CHOP_TEST(status_bar_flags_unapplied_rule_changes_only_while_the_drawer_is_closed) {
  if (!haveDisplay()) return;
  RulesFixture f;
  CHECK(f.open());
  if (!f.panel) return;
  CHECK(f.ed->model().statusSuffix().isEmpty());
  findButton(*f.editor, "Add Rule")->onClick();
  pump(60);
  CHECK(f.panel->draft().dirty());
  CHECK(f.ed->model().statusSuffix().isEmpty());  // the drawer is open: the editor itself shows it
  f.ed->closeDrawer();
  pump(100);
  CHECK(f.ed->model().statusSuffix() == "Unapplied rule changes");
  CHECK(f.ed->statusBar().displayedText().contains("Unapplied rule changes"));
  f.ed->openDrawer("rules");
  pump(100);
  CHECK(f.ed->model().statusSuffix().isEmpty());
  f.ed->closeDrawer();
  pump(100);
  CHECK(f.ed->model().statusSuffix() == "Unapplied rule changes");
  f.ed->openDrawer("rules");
  pump(100);
  findButton(*f.editor, "Revert")->onClick();
  pump(60);
  f.ed->closeDrawer();
  pump(100);
  CHECK(f.ed->model().statusSuffix().isEmpty());  // nothing left to apply
}
