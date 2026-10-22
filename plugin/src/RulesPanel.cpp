#include "RulesPanel.h"

#include <chopfractal/chop_roles_grammar/grammar.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "CdlLookAndFeel.h"

namespace cf = chopfractal;
using cf::roles::ActionKind;
using cf::ui::TemplateKind;

namespace {
constexpr int kBodyW = 400;
const ActionKind kActions[] = {ActionKind::Preserve, ActionKind::Require, ActionKind::Forbid, ActionKind::MaxConsecutiveRepeats, ActionKind::MaxCount, ActionKind::Prefer};
const char* const kPositions[] = {"Anywhere", "First in bar", "Final beat", "Phrase boundary"};

juce::String timeText(std::int64_t frame, int rate) {
  const double sec = static_cast<double>(frame) / std::max(1, rate);
  const int m = static_cast<int>(sec / 60.0);
  return juce::String(m) + ":" + juce::String(sec - m * 60.0, 3).paddedLeft('0', 6);
}
int parseInt(const juce::String& s, int fallback) { return s.trim().isEmpty() ? fallback : s.getIntValue(); }
}  // namespace

// ---------------------------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------------------------

void RulesPanel::TextList::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
  const auto& p = cdl::pal();
  if (selected) {
    g.setColour(p.spray.withAlpha(0.25f));
    g.fillRect(0, 0, w, h);
  }
  g.setColour(p.ink);
  g.setFont(cdl::font(cdl::FontRole::Body, cdl::uiScale()));
  g.drawText(text ? text(row) : juce::String(), 6, 0, w - 8, h, juce::Justification::centredLeft, true);
}

RulesPanel::RoleRow::RoleRow(RulesPanel& o, int i) : owner(o), index(i) {
  role.setEditableText(true);
  role.onChange = [this] {
    if (loading) return;
    const auto text = role.getText().trim();
    const juce::String value = text == "(none)" ? juce::String() : text.toLowerCase();
    owner.cmd_.setChopRole(chop, value);
  };
  audition.setTooltip("Play this chop through the output");
  audition.onClick = [this] { owner.cmd_.audition(chop); };
  addAndMakeVisible(role);
  addAndMakeVisible(audition);
}

void RulesPanel::RoleRow::resized() {
  auto a = getLocalBounds().reduced(2);
  a.removeFromLeft(190);
  audition.setBounds(a.removeFromRight(70));
  a.removeFromRight(4);
  role.setBounds(a);
}

void RulesPanel::RoleRow::paint(juce::Graphics& g) {
  g.setColour(cdl::pal().ink);
  g.setFont(cdl::font(cdl::FontRole::Body, cdl::uiScale()));
  g.drawText("Chop " + juce::String(index + 1), 6, 0, 56, getHeight(), juce::Justification::centredLeft, false);
  g.setColour(cdl::pal().inkMuted);
  g.setFont(cdl::font(cdl::FontRole::Hint, cdl::uiScale()));
  g.drawText(rangeText, 62, 0, 128, getHeight(), juce::Justification::centredLeft, true);
}

void RulesPanel::RoleRow::setChop(const cf::ChopInfo& c, const juce::String& range, const juce::String& current, const std::vector<juce::String>& choices) {
  loading = true;
  chop = c.id;
  rangeText = range;
  role.clear(juce::dontSendNotification);
  role.addItem("(none)", 1);
  for (std::size_t i = 0; i < choices.size(); ++i) role.addItem(choices[i], static_cast<int>(i) + 2);
  role.setText(current.isEmpty() ? juce::String("(none)") : current, juce::dontSendNotification);
  role.setTitle("Role for chop " + juce::String(index + 1));
  role.setTooltip("Role of this chop; type a custom id (lowercase letters, digits, _ and -)");
  audition.setTitle("Audition chop " + juce::String(index + 1));
  loading = false;
  repaint();
}

int RulesPanel::RoleModel::getNumRows() { return static_cast<int>(owner.snap_.chops.size()); }

juce::Component* RulesPanel::RoleModel::refreshComponentForRow(int row, bool, juce::Component* existing) {
  auto* r = dynamic_cast<RoleRow*>(existing);
  if (row < 0 || row >= static_cast<int>(owner.snap_.chops.size())) {
    delete existing;
    return nullptr;
  }
  if (!r || r->index != row) {
    delete existing;
    r = new RoleRow(owner, row);
  }
  const auto& c = owner.snap_.chops[static_cast<std::size_t>(row)];
  const int rate = owner.snap_.sampleRate;
  r->setChop(c, timeText(c.range.start, rate) + " - " + timeText(c.range.end, rate), juce::String(owner.snap_.roles.roleOf(c.id)), owner.roleChoices());
  return r;
}

std::string RulesPanel::signatureOf(const cf::roles::RuleSet& rules) { 
  const auto bytes = cf::roles::serialize(cf::roles::RoleMap{}, rules);
  return std::string(bytes.begin(), bytes.end());
}

std::vector<juce::String> RulesPanel::roleChoices() const {
  std::vector<juce::String> out;
  for (const auto& r : cf::roles::builtinRoles()) out.emplace_back(r);
  for (const auto& kv : snap_.roles.all()) {
    const juce::String r(kv.second);
    if (std::none_of(out.begin(), out.end(), [&](const juce::String& x) { return x == r; })) out.push_back(r);
  }
  return out;
}

// ---------------------------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------------------------

RulesPanel::RulesPanel(EditorModel& model, EditorCommands& commands) : EditorPanelBase(model, commands, "RulesPanel") {
  addAndMakeVisible(viewport_);
  viewport_.setViewedComponent(&body_, false);
  viewport_.setScrollBarsShown(true, false);
  viewport_.setComponentID("RulesViewport");

  for (juce::TextButton* b : {&rulesTab_btn_, &rolesTab_btn_}) {
    b->setClickingTogglesState(false);
    b->setTitle(b->getButtonText());
    body_.addAndMakeVisible(*b);
  }
  rulesTab_btn_.setTooltip("Rules that steer Generate, Mutate and Evolve");
  rolesTab_btn_.setTooltip("Give each chop a role such as kick or hat");
  rulesTab_btn_.onClick = [this] { showTab(true); };
  rolesTab_btn_.onClick = [this] { showTab(false); };

  // lists
  ruleModel_.count = [this] { return static_cast<int>(draft_.rows().size()); };
  ruleModel_.text = [this](int row) {
    const auto rows = draft_.rows();
    return row >= 0 && row < static_cast<int>(rows.size()) ? juce::String(rows[static_cast<std::size_t>(row)].text) : juce::String();
  };
  ruleModel_.select = [this](int row) {
    const auto rows = draft_.rows();
    if (row >= 0 && row < static_cast<int>(rows.size()) && rows[static_cast<std::size_t>(row)].id != selectedRule_) selectRule(rows[static_cast<std::size_t>(row)].id);
  };
  ruleList_.setModel(&ruleModel_);
  ruleList_.setTitle("Rule list");
  ruleList_.setRowHeight(22);
  problemModel_.count = [this] { return static_cast<int>(draft_.issues().size()); };
  problemModel_.text = [this](int row) {
    const auto all = draft_.issues();
    if (row < 0 || row >= static_cast<int>(all.size())) return juce::String();
    return juce::String(all[static_cast<std::size_t>(row)].error ? "Error: " : "Warning: ") + juce::String(all[static_cast<std::size_t>(row)].text);
  };
  problemModel_.select = [this](int row) {
    const auto all = draft_.issues();
    if (row >= 0 && row < static_cast<int>(all.size()) && all[static_cast<std::size_t>(row)].rule) selectRule(all[static_cast<std::size_t>(row)].rule);
  };
  problems_.setModel(&problemModel_);
  problems_.setTitle("Problems");
  problems_.setRowHeight(20);
  noteModel_.count = [this] { return std::max<int>(1, static_cast<int>(snap_.notes.size())); };
  noteModel_.text = [this](int row) {
    if (snap_.notes.empty()) return juce::String("Nothing was blocked by the last Generate, Mutate or Evolve step.");
    return row >= 0 && row < static_cast<int>(snap_.notes.size()) ? juce::String(snap_.notes[static_cast<std::size_t>(row)].text) : juce::String();
  };
  noteModel_.select = [this](int row) {
    if (row >= 0 && row < static_cast<int>(snap_.notes.size())) cmd_.selectBeat(snap_.notes[static_cast<std::size_t>(row)].bar, snap_.notes[static_cast<std::size_t>(row)].beat);
  };
  blocked_.setModel(&noteModel_);
  blocked_.setTitle("Last blocked changes");
  blocked_.setRowHeight(20);
  roleList_.setModel(&roleModel_);
  roleList_.setTitle("Chop roles");
  roleList_.setRowHeight(30);
  rolesEmpty_.setText("Detect chops first; roles are assigned to chops.", juce::dontSendNotification);
  for (juce::Component* c : std::initializer_list<juce::Component*>{&ruleList_, &problems_, &blocked_, &roleList_, &rolesEmpty_, &problemCount_, &blockedCount_}) body_.addAndMakeVisible(*c);
  problemCount_.setTitle("Problem count");
  blockedCount_.setText("Last blocked changes", juce::dontSendNotification);

  // list buttons and templates
  add_.setTooltip("Add a rule that prefers things twice as much");
  add_.onClick = [this] {
    const auto id = draft_.addRule();
    if (!id) cmd_.say("The limit is 64 rules.");
    else selectRule(id);
    refreshAll();
  };
  duplicate_.setTooltip("Copy the selected rule");
  duplicate_.onClick = [this] {
    const auto id = draft_.duplicate(selectedRule_);
    if (id) selectRule(id);
    refreshAll();
  };
  delete_.setTooltip("Remove the selected rule");
  delete_.onClick = [this] {
    if (draft_.remove(selectedRule_)) selectedRule_ = 0;
    refreshAll();
  };
  for (juce::TextButton* b : {&add_, &duplicate_, &delete_, &keepFirst_, &noRepeats_, &noRoleRepeats_, &needFinal_, &preferBoundary_, &excludeBars_}) {
    b->setTitle(b->getButtonText());
    body_.addAndMakeVisible(*b);
  }
  body_.addAndMakeVisible(templatesLabel_);
  templateRole_.setTitle("Template role");
  templateRole_.setTooltip("The role the template talks about");
  body_.addAndMakeVisible(templateRole_);
  for (juce::TextEditor* e : {&barFrom_, &barTo_}) {
    e->setInputRestrictions(1, "12345678");
    e->setSelectAllWhenFocused(true);
    body_.addAndMakeVisible(*e);
  }
  barFrom_.setText("1", false);
  barTo_.setText("2", false);
  barFrom_.setTitle("Template bar from");
  barTo_.setTitle("Template bar to");
  barFrom_.setTooltip("First bar for Exclude from bars (1 to 8)");
  barTo_.setTooltip("Last bar for Exclude from bars (1 to 8)");
  keepFirst_.setTooltip("Keep the first hit of the role in every bar when mutating");
  keepFirst_.onClick = [this] { addFromTemplate(TemplateKind::KeepFirst); };
  noRepeats_.setTooltip("Never play the same chop twice in a row");
  noRepeats_.onClick = [this] { addFromTemplate(TemplateKind::NoRepeats); };
  noRoleRepeats_.setTooltip("Never play the same role twice in a row");
  noRoleRepeats_.onClick = [this] { addFromTemplate(TemplateKind::NoRoleRepeats); };
  needFinal_.setTooltip("Keep at least one hit of the role in the final beat");
  needFinal_.onClick = [this] { addFromTemplate(TemplateKind::NeedInFinalBeat); };
  preferBoundary_.setTooltip("Favour the role at phrase boundaries");
  preferBoundary_.onClick = [this] { addFromTemplate(TemplateKind::PreferAtBoundaries); };
  excludeBars_.setTooltip("Keep the role out of the bars from and to");
  excludeBars_.onClick = [this] { addFromTemplate(TemplateKind::ExcludeFromBars); };

  // edit form
  enabled_.setTitle("Rule enabled");
  enabled_.setTooltip("Switch the rule off without deleting it");
  body_.addAndMakeVisible(enabled_);
  struct Field { juce::Label* l; const char* text; };
  for (const Field& f : {Field{&actionL_, "Action"}, Field{&roleL_, "Role"}, Field{&chopL_, "Chop"}, Field{&positionL_, "Position"}, Field{&barsL_, "Bars"}, Field{&beatsL_, "Beats"},
                         Field{&countL_, "Count"}, Field{&repeatL_, "Repeat by"}, Field{&weightL_, "Weight"}, Field{&priorityL_, "Priority"}, Field{&labelL_, "Label"}}) {
    f.l->setText(f.text, juce::dontSendNotification);
    f.l->setInterceptsMouseClicks(false, false);
    body_.addAndMakeVisible(*f.l);
  }
  action_.setTitle("Action");
  action_.setTooltip("What the rule does when its condition matches");
  for (std::size_t i = 0; i < 6; ++i) action_.addItem(cf::ui::RuleEditorModel::actionName(kActions[i]), static_cast<int>(i) + 1);
  ruleRole_.setTitle("Role");
  ruleRole_.setTooltip("The role the rule is about; (any role) matches every chop");
  ruleRole_.setEditableText(true);
  ruleChop_.setTitle("Chop");
  ruleChop_.setTooltip("Limit the rule to one chop");
  position_.setTitle("Position");
  position_.setTooltip("Where in the bar the rule applies");
  for (int i = 0; i < 4; ++i) position_.addItem(kPositions[i], i + 1);
  repeatBy_.setTitle("Repeat by");
  repeatBy_.setTooltip("Count repeats of the same chop or of the same role");
  repeatBy_.addItem("Same chop", 1);
  repeatBy_.addItem("Same role", 2);
  struct Edit { juce::TextEditor* e; const char* title; const char* tip; const char* chars; int maxLen; };
  for (const Edit& d : {Edit{&barMin_, "Bar from", "First bar the rule applies to (1 to 8); blank = from the start", "0123456789", 1},
                        Edit{&barMax_, "Bar to", "Last bar the rule applies to; blank = no end", "0123456789", 1},
                        Edit{&beatMin_, "Beat from", "First beat the rule applies to (1 to 32); blank = from the start", "0123456789", 2},
                        Edit{&beatMax_, "Beat to", "Last beat the rule applies to; blank = no end", "0123456789", 2},
                        Edit{&count_, "Count", "How many are allowed in a row or in a bar", "0123456789", 3}}) {
    d.e->setTitle(d.title);
    d.e->setTooltip(d.tip);
    d.e->setInputRestrictions(d.maxLen, d.chars);
    d.e->setSelectAllWhenFocused(true);
    body_.addAndMakeVisible(*d.e);
  }
  label_.setTitle("Label");
  label_.setTooltip("A name for the rule, shown in explanations");
  label_.setInputRestrictions(128);
  body_.addAndMakeVisible(label_);
  weight_.setSliderStyle(juce::Slider::LinearHorizontal);
  weight_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 20);
  weight_.setRange(0.1, 100.0, 0.1);
  weight_.setSkewFactorFromMidPoint(2.0);
  weight_.setTitle("Weight");
  weight_.setTooltip("Above 1 prefers, below 1 avoids");
  weight_.textFromValueFunction = [](double v) { return "x" + juce::String(v, 1); };
  weight_.valueFromTextFunction = [](const juce::String& t) { return t.retainCharacters("0123456789.").getDoubleValue(); };
  priority_.setSliderStyle(juce::Slider::LinearHorizontal);
  priority_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 20);
  priority_.setRange(-1000, 1000, 1);
  priority_.setTitle("Priority");
  priority_.setTooltip("Rules are checked highest priority first. A Require of higher priority overrides a Forbid of lower priority.");
  for (juce::Component* c : std::initializer_list<juce::Component*>{&action_, &ruleRole_, &ruleChop_, &position_, &repeatBy_, &weight_, &priority_}) body_.addAndMakeVisible(*c);

  auto changed = [this] { if (!loading_) commitForm(); };
  enabled_.onClick = changed;
  for (juce::ComboBox* c : {&action_, &ruleRole_, &ruleChop_, &position_, &repeatBy_}) c->onChange = changed;
  for (juce::TextEditor* e : {&barMin_, &barMax_, &beatMin_, &beatMax_, &count_, &label_}) e->onTextChange = changed;
  weight_.onValueChange = changed;
  priority_.onValueChange = changed;

  // footer and counts
  apply_.setTooltip("Store the rules in the project");
  apply_.onClick = [this] {
    if (!draft_.canApply()) return;
    if (cmd_.applyRules(draft_.draft())) {
      draft_.markApplied();
      appliedSignature_ = signatureOf(draft_.draft());
    }
    refreshAll();
  };
  revert_.setTooltip("Throw away unapplied changes");
  revert_.onClick = [this] {
    resync(false);
    cmd_.say("Reverted to the applied rules.");
  };
  applyMutate_.setTooltip("Apply, then mutate every unlocked beat so the rules act at once");
  applyMutate_.onClick = [this] {
    if (!draft_.canApply() && draft_.dirty()) return;
    if (draft_.dirty() && !cmd_.applyRules(draft_.draft())) return;
    draft_.markApplied();
    appliedSignature_ = signatureOf(draft_.draft());
    cmd_.mutateAll();
    refreshAll();
  };
  test_.setTooltip("Count what the rules would block with the current settings, without changing anything");
  test_.onClick = [this] { cmd_.testRules(draft_.draft()); };
  why_.setTooltip("Explain why the selected hit is there");
  why_.onClick = [this] { showWhy(); };
  for (juce::TextButton* b : {&apply_, &revert_, &applyMutate_, &test_, &why_}) {
    b->setTitle(b->getButtonText());
    body_.addAndMakeVisible(*b);
  }
  resync(false);
  showTab(true);
}

RulesPanel::~RulesPanel() {
  for (juce::ListBox* l : {&ruleList_, &problems_, &blocked_, &roleList_}) l->setModel(nullptr);
}

// ---------------------------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------------------------

void RulesPanel::modelChanged(Change c) {
  if (c != Change::View && c != Change::Selection) return;
  const auto snap = model_.rulesSnapshot();
  const auto sig = signatureOf(snap.rules);
  if (sig != appliedSignature_) {  // rules changed underneath: a project was loaded
    const bool lost = draft_.dirty();
    resync(false);
    if (lost) cmd_.say("The project's rules were reloaded; unapplied changes were discarded.");
    return;
  }
  snap_ = snap;
  draft_.setRoles(snap_.roles);
  refreshProblems();
  refreshRoles();
  refreshFooter();
}

void RulesPanel::resync(bool) {
  snap_ = model_.rulesSnapshot();
  appliedSignature_ = signatureOf(snap_.rules);
  std::vector<cf::ChopId> ids;
  for (const auto& c : snap_.chops) ids.push_back(c.id);
  draft_.reset(snap_.rules, snap_.roles, snap_.patternBars, ids);
  if (!draft_.find(selectedRule_)) selectedRule_ = 0;
  refreshAll();
}

void RulesPanel::refreshAll() {
  refreshList();
  loadForm();
  refreshProblems();
  refreshRoles();
  refreshFooter();
}

void RulesPanel::refreshList() {
  ruleList_.updateContent();
  const auto rows = draft_.rows();
  int sel = -1;
  for (std::size_t i = 0; i < rows.size(); ++i)
    if (rows[i].id == selectedRule_) sel = static_cast<int>(i);
  if (sel >= 0) ruleList_.selectRow(sel, false, false);
  else ruleList_.deselectAllRows();
  ruleList_.repaint();
  duplicate_.setEnabled(selectedRule_ != 0 && draft_.canAdd());
  delete_.setEnabled(selectedRule_ != 0);
  add_.setEnabled(draft_.canAdd());
  add_.setTooltip(draft_.canAdd() ? "Add a rule that prefers things twice as much" : "The limit is 64 rules.");
}

void RulesPanel::refreshProblems() {
  problems_.updateContent();
  problems_.repaint();
  problemCount_.setText(juce::String(draft_.issueSummary()), juce::dontSendNotification);
  blocked_.updateContent();
  blocked_.repaint();
  blockedCount_.setText("Last blocked changes: " + juce::String(static_cast<int>(snap_.notes.size())), juce::dontSendNotification);
  const auto roles = roleChoices();
  const auto current = templateRole_.getText();
  templateRole_.clear(juce::dontSendNotification);
  for (std::size_t i = 0; i < roles.size(); ++i) templateRole_.addItem(roles[i], static_cast<int>(i) + 1);
  templateRole_.setText(current.isEmpty() ? roles.front() : current, juce::dontSendNotification);
}

void RulesPanel::refreshRoles() {
  roleList_.updateContent();
  roleList_.repaint();
  rolesEmpty_.setVisible(!rulesTab_ && snap_.chops.empty());
}

void RulesPanel::refreshFooter() {
  const bool can = draft_.canApply();
  apply_.setEnabled(can);
  juce::String tip = "Store the rules in the project";
  if (!can && draft_.dirty())
    for (const auto& i : draft_.issues())
      if (i.error) {
        tip = juce::String(i.text);
        break;
      }
  apply_.setTooltip(tip);
  revert_.setEnabled(draft_.dirty());
  applyMutate_.setEnabled(!draft_.dirty() || can);
  why_.setEnabled(model_.hasSelection());
  updateStatusSuffix();
}

void RulesPanel::setDrawerOpen(bool open) {
  drawerOpen_ = open;
  updateStatusSuffix();
}

// The drawer shows its own Apply and Revert; once it is closed the status bar keeps the reminder.
void RulesPanel::updateStatusSuffix() { model_.setStatusSuffix(draft_.dirty() && !drawerOpen_ ? juce::String("Unapplied rule changes") : juce::String()); }

void RulesPanel::selectRule(std::uint32_t id) {
  selectedRule_ = id;
  refreshList();
  loadForm();
}

void RulesPanel::addFromTemplate(TemplateKind kind) {
  const juce::String role = templateRole_.getText().isEmpty() ? "kick" : templateRole_.getText();
  const int from = parseInt(barFrom_.getText(), 1), to = parseInt(barTo_.getText(), 2);
  const auto id = draft_.addFromTemplate(kind, role.toStdString(), from, to);
  if (!id) {
    cmd_.say(!draft_.canAdd() ? "The limit is 64 rules." : "Bar from must be 1 to 8 and not after Bar to.");
    return;
  }
  selectRule(id);
  refreshAll();
  label_.grabKeyboardFocus();
}

void RulesPanel::showTab(bool rules) {
  rulesTab_ = rules;
  rulesTab_btn_.setToggleState(rules, juce::dontSendNotification);
  rolesTab_btn_.setToggleState(!rules, juce::dontSendNotification);
  layoutBody();
  refreshRoles();
}

void RulesPanel::showWhy() {
  const auto text = cmd_.explainSelected();
  if (text.isEmpty()) return;
  auto content = std::make_unique<juce::Label>();
  content->setText(text, juce::dontSendNotification);
  content->setJustificationType(juce::Justification::topLeft);
  content->setSize(300, 110);
  content->setColour(juce::Label::textColourId, cdl::pal().ink);
  content->setColour(juce::Label::backgroundColourId, cdl::pal().panel);
  // Inside the editor, not a native window: hosts and test displays without a window manager dislike extra top-level windows.
  if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>()) juce::CallOutBox::launchAsynchronously(std::move(content), editor->getLocalArea(&why_, why_.getLocalBounds()), editor);
  else juce::CallOutBox::launchAsynchronously(std::move(content), why_.getScreenBounds(), nullptr);
}

// ---------------------------------------------------------------------------------------------
// form
// ---------------------------------------------------------------------------------------------

void RulesPanel::loadForm() {
  loading_ = true;
  const cf::roles::Rule* r = draft_.find(selectedRule_);
  const bool have = r != nullptr;
  // role and chop choices
  ruleRole_.clear(juce::dontSendNotification);
  ruleRole_.addItem("(any role)", 1);
  const auto roles = roleChoices();
  for (std::size_t i = 0; i < roles.size(); ++i) ruleRole_.addItem(roles[i], static_cast<int>(i) + 2);
  ruleChop_.clear(juce::dontSendNotification);
  ruleChop_.addItem("(any chop)", 1);
  for (std::size_t i = 0; i < snap_.chops.size(); ++i) ruleChop_.addItem("Chop " + juce::String(static_cast<int>(i) + 1), static_cast<int>(i) + 2);
  if (have) {
    enabled_.setToggleState(r->enabled, juce::dontSendNotification);
    int ai = 0;
    for (int i = 0; i < 6; ++i)
      if (kActions[i] == r->action.kind) ai = i;
    action_.setSelectedId(ai + 1, juce::dontSendNotification);
    ruleRole_.setText(r->when.role.empty() ? juce::String("(any role)") : juce::String(r->when.role), juce::dontSendNotification);
    int chopSel = 1;
    for (std::size_t i = 0; i < snap_.chops.size(); ++i)
      if (snap_.chops[i].id == r->when.chop) chopSel = static_cast<int>(i) + 2;
    ruleChop_.setSelectedId(chopSel, juce::dontSendNotification);
    position_.setSelectedId(static_cast<int>(r->when.position) + 1, juce::dontSendNotification);
    auto shown = [](int v, bool unboundedIsBlank) { return (unboundedIsBlank && v < 0) ? juce::String() : juce::String(v + 1); };
    barMin_.setText(r->when.barMin == 0 ? juce::String() : shown(r->when.barMin, false), false);
    barMax_.setText(shown(r->when.barMax, true), false);
    beatMin_.setText(r->when.beatMin == 0 ? juce::String() : shown(r->when.beatMin, false), false);
    beatMax_.setText(shown(r->when.beatMax, true), false);
    count_.setText(juce::String(r->action.count), false);
    repeatBy_.setSelectedId(r->action.repeatBy == cf::roles::RepeatBy::SameChop ? 1 : 2, juce::dontSendNotification);
    weight_.setValue(r->weight, juce::dontSendNotification);
    priority_.setValue(r->priority, juce::dontSendNotification);
    label_.setText(juce::String(r->label), false);
  } else {
    label_.setText({}, false);
  }
  for (juce::Component* c : std::initializer_list<juce::Component*>{&enabled_, &action_, &ruleRole_, &ruleChop_, &position_, &repeatBy_, &barMin_, &barMax_, &beatMin_, &beatMax_, &count_, &weight_, &priority_, &label_})
    c->setEnabled(have);
  loading_ = false;
  layoutBody();
}

void RulesPanel::commitForm() {
  const cf::roles::Rule* cur = draft_.find(selectedRule_);
  if (!cur) return;
  cf::roles::Rule r = *cur;
  r.enabled = enabled_.getToggleState();
  const int ai = action_.getSelectedId() - 1;
  if (ai >= 0 && ai < 6) r.action.kind = kActions[ai];
  const auto roleText = ruleRole_.getText().trim();
  r.when.role = (roleText.isEmpty() || roleText == "(any role)") ? std::string() : roleText.toStdString();
  const int chopSel = ruleChop_.getSelectedId();
  r.when.chop = (chopSel >= 2 && chopSel - 2 < static_cast<int>(snap_.chops.size())) ? snap_.chops[static_cast<std::size_t>(chopSel - 2)].id : cf::ChopId{};
  r.when.position = static_cast<cf::roles::Position>(std::max(0, position_.getSelectedId() - 1));
  r.when.barMin = barMin_.getText().trim().isEmpty() ? 0 : barMin_.getText().getIntValue() - 1;
  r.when.barMax = barMax_.getText().trim().isEmpty() ? -1 : barMax_.getText().getIntValue() - 1;
  r.when.beatMin = beatMin_.getText().trim().isEmpty() ? 0 : beatMin_.getText().getIntValue() - 1;
  r.when.beatMax = beatMax_.getText().trim().isEmpty() ? -1 : beatMax_.getText().getIntValue() - 1;
  r.action.count = parseInt(count_.getText(), 0);
  r.action.repeatBy = repeatBy_.getSelectedId() == 2 ? cf::roles::RepeatBy::SameRole : cf::roles::RepeatBy::SameChop;
  r.weight = weight_.getValue();
  r.priority = static_cast<int>(std::lround(priority_.getValue()));
  r.label = label_.getText().toStdString();
  const auto oldKind = cur->action.kind;
  draft_.update(r);
  if (oldKind != r.action.kind) layoutBody();  // fields appear and disappear with the action
  refreshList();
  refreshProblems();
  refreshFooter();
}

// ---------------------------------------------------------------------------------------------
// layout
// ---------------------------------------------------------------------------------------------

void RulesPanel::paint(juce::Graphics& g) { g.fillAll(cdl::pal().panel); }

void RulesPanel::resized() {
  viewport_.setBounds(getLocalBounds());
  layoutBody();
}

void RulesPanel::layoutBody() {
  const int w = std::max(300, viewport_.getMaximumVisibleWidth());
  int y = 8;
  const int pad = 10;
  auto row = [&](int h) {
    juce::Rectangle<int> r(pad, y, w - 2 * pad, h);
    y += h + 4;
    return r;
  };
  auto tabs = row(28);
  rulesTab_btn_.setBounds(tabs.removeFromLeft(tabs.getWidth() / 2 - 2));
  tabs.removeFromLeft(4);
  rolesTab_btn_.setBounds(tabs);

  const bool r = rulesTab_;
  for (juce::Component* c : std::initializer_list<juce::Component*>{&ruleList_, &add_, &duplicate_, &delete_, &templatesLabel_, &templateRole_, &barFrom_, &barTo_, &keepFirst_, &noRepeats_, &noRoleRepeats_, &needFinal_,
                                                                  &preferBoundary_, &excludeBars_, &enabled_, &action_, &ruleRole_, &ruleChop_, &position_, &repeatBy_, &barMin_, &barMax_, &beatMin_, &beatMax_,
                                                                  &count_, &weight_, &priority_, &label_, &actionL_, &roleL_, &chopL_, &positionL_, &barsL_, &beatsL_, &countL_, &repeatL_, &weightL_, &priorityL_, &labelL_})
    c->setVisible(r);
  roleList_.setVisible(!r);
  rolesEmpty_.setVisible(!r && snap_.chops.empty());

  if (r) {
    ruleList_.setBounds(row(124));
    auto b = row(28);
    add_.setBounds(b.removeFromLeft(b.getWidth() / 3 - 3));
    b.removeFromLeft(4);
    duplicate_.setBounds(b.removeFromLeft(b.getWidth() / 2 - 2));
    b.removeFromLeft(4);
    delete_.setBounds(b);
    templatesLabel_.setBounds(row(20));
    auto t = row(28);
    templateRole_.setBounds(t.removeFromLeft(150));
    t.removeFromLeft(6);
    barFrom_.setBounds(t.removeFromLeft(40));
    t.removeFromLeft(6);
    barTo_.setBounds(t.removeFromLeft(40));
    juce::TextButton* grid[3][2] = {{&keepFirst_, &noRepeats_}, {&noRoleRepeats_, &needFinal_}, {&preferBoundary_, &excludeBars_}};
    for (auto& line : grid) {
      auto tb = row(28);
      line[0]->setBounds(tb.removeFromLeft(tb.getWidth() / 2 - 2));
      tb.removeFromLeft(4);
      line[1]->setBounds(tb);
    }
    y += 4;
    enabled_.setBounds(row(24));
    const cf::roles::Rule* cur = draft_.find(selectedRule_);
    const auto kind = cur ? cur->action.kind : ActionKind::Prefer;
    auto field = [&](juce::Label& l, juce::Component& c, bool visible) {
      l.setVisible(visible && cur);
      c.setVisible(visible && cur);
      if (!visible) return;
      auto f = row(26);
      l.setBounds(f.removeFromLeft(84));
      c.setBounds(f);
    };
    field(actionL_, action_, true);
    field(roleL_, ruleRole_, true);
    field(chopL_, ruleChop_, true);
    field(positionL_, position_, true);
    {
      auto f = row(26);
      barsL_.setBounds(f.removeFromLeft(84));
      barMin_.setBounds(f.removeFromLeft(50));
      f.removeFromLeft(8);
      barMax_.setBounds(f.removeFromLeft(50));
    }
    {
      auto f = row(26);
      beatsL_.setBounds(f.removeFromLeft(84));
      beatMin_.setBounds(f.removeFromLeft(50));
      f.removeFromLeft(8);
      beatMax_.setBounds(f.removeFromLeft(50));
    }
    const bool limit = kind == ActionKind::MaxConsecutiveRepeats || kind == ActionKind::MaxCount;
    field(countL_, count_, limit);
    field(repeatL_, repeatBy_, kind == ActionKind::MaxConsecutiveRepeats);
    field(weightL_, weight_, kind == ActionKind::Prefer);
    field(priorityL_, priority_, true);
    field(labelL_, label_, true);
    y += 4;
  } else {
    roleList_.setBounds(row(std::clamp(static_cast<int>(snap_.chops.size()) * 30 + 4, 60, 300)));
    rolesEmpty_.setBounds(juce::Rectangle<int>(pad, y - 40, w - 2 * pad, 24));
  }
  problemCount_.setBounds(row(20));
  problems_.setBounds(row(84));
  blockedCount_.setBounds(row(20));
  blocked_.setBounds(row(84));
  auto f1 = row(30);
  apply_.setBounds(f1.removeFromLeft(f1.getWidth() / 3 - 3));
  f1.removeFromLeft(4);
  revert_.setBounds(f1.removeFromLeft(f1.getWidth() / 2 - 2));
  f1.removeFromLeft(4);
  applyMutate_.setBounds(f1);
  auto f2 = row(30);
  test_.setBounds(f2.removeFromLeft(f2.getWidth() / 2 - 2));
  f2.removeFromLeft(4);
  why_.setBounds(f2);
  body_.setSize(w, y + 8);
}
