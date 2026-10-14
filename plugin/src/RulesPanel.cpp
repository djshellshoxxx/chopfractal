#include "RulesPanel.h"

#include <algorithm>
#include <initializer_list>

#include "PluginProcessor.h"

namespace cf = chopfractal;
namespace rl = chopfractal::roles;
using cf::ui::TemplateKind;

namespace {

constexpr rl::ActionKind kActions[] = {rl::ActionKind::Preserve, rl::ActionKind::Require, rl::ActionKind::Forbid,
                                       rl::ActionKind::MaxConsecutiveRepeats, rl::ActionKind::MaxCount, rl::ActionKind::Prefer};

int actionIndex(rl::ActionKind k) {
  for (int i = 0; i < 6; ++i)
    if (kActions[i] == k) return i;
  return 5;
}

juce::String clock(double seconds) {
  const int ms = juce::roundToInt(seconds * 1000.0);
  return juce::String::formatted("%d:%02d.%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
}

int parseInt(const juce::String& s, int fallback) { return s.trim().isEmpty() ? fallback : s.getIntValue(); }

}  // namespace

void StringListModel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
  if (selected) g.fillAll(juce::Colour(0xff2f4f7a));
  g.setColour(juce::Colours::white.withAlpha(0.9f));
  g.setFont(juce::FontOptions(12.5f));
  g.drawText(text ? text(row) : juce::String(), 6, 0, width - 8, height, juce::Justification::centredLeft, true);
}

RulesPanel::RulesPanel(ChopFractalProcessor& processor, Hooks hooks) : proc_(processor), hooks_(std::move(hooks)) {
  setName("RulesPanel");
  setWantsKeyboardFocus(true);
  header_.setText("Rules and roles", juce::dontSendNotification);
  header_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
  addAndMakeVisible(header_);
  for (juce::TextButton* b : {&rulesTab_, &rolesTab_}) {
    b->setClickingTogglesState(true);
    b->setRadioGroupId(4101);
    b->setTitle(b == &rulesTab_ ? "Rules" : "Roles");
    addAndMakeVisible(*b);
  }
  rulesTab_.setButtonText("Rules");
  rolesTab_.setButtonText("Roles");
  rulesTab_.setToggleState(true, juce::dontSendNotification);
  rulesTab_.onClick = [this] { showTab(Tab::Rules); };
  rolesTab_.onClick = [this] { showTab(Tab::Roles); };

  // ---- lists ----
  ruleModel_.count = [this] { return static_cast<int>(rows_.size()); };
  ruleModel_.text = [this](int r) { return r < static_cast<int>(rows_.size()) ? juce::String(rows_[static_cast<std::size_t>(r)].text) : juce::String(); };
  ruleModel_.tooltip = [this](int r) { return r < static_cast<int>(rows_.size()) ? juce::String(rows_[static_cast<std::size_t>(r)].tooltip) : juce::String(); };
  ruleModel_.onSelect = [this](int r) {
    if (r < static_cast<int>(rows_.size())) selectedId_ = rows_[static_cast<std::size_t>(r)].id;
    refreshForm();
  };
  ruleList_.setModel(&ruleModel_);
  ruleList_.setTitle("Rule list");
  ruleList_.setName("Rule list");
  problemModel_.count = [this] { return static_cast<int>(issues_.size()); };
  problemModel_.text = [this](int r) {
    if (r >= static_cast<int>(issues_.size())) return juce::String();
    const auto& i = issues_[static_cast<std::size_t>(r)];
    const juce::String body(i.text);
    return juce::String(i.error ? "Error: " : "Warning: ") + (body.startsWith("Rule ") ? body : "rule " + juce::String(i.rule) + ": " + body);
  };
  problemModel_.onSelect = [this](int r) {
    if (r < static_cast<int>(issues_.size())) {
      selectedId_ = issues_[static_cast<std::size_t>(r)].rule;
      refreshAll();
      refreshForm();
    }
  };
  problems_.setModel(&problemModel_);
  problems_.setTitle("Problems");
  noteModel_.count = [this] {
    int n = 0;
    proc_.withSession([&](cf::composition::ProjectSession& s) { n = static_cast<int>(s.lastRuleNotes().size()); });
    return n;
  };
  noteModel_.text = [this](int r) {
    juce::String t;
    proc_.withSession([&](cf::composition::ProjectSession& s) {
      if (r < static_cast<int>(s.lastRuleNotes().size())) t = s.lastRuleNotes()[static_cast<std::size_t>(r)].text;
    });
    return t;
  };
  noteModel_.onSelect = [this](int r) {
    int bar = -1, beat = -1;
    proc_.withSession([&](cf::composition::ProjectSession& s) {
      if (r < static_cast<int>(s.lastRuleNotes().size())) {
        bar = s.lastRuleNotes()[static_cast<std::size_t>(r)].bar;
        beat = s.lastRuleNotes()[static_cast<std::size_t>(r)].beat;
      }
    });
    if (bar >= 0 && hooks_.selectBeat) hooks_.selectBeat(bar, beat);
  };
  notes_.setModel(&noteModel_);
  notes_.setTitle("Last blocked changes");
  for (juce::Component* c : std::initializer_list<juce::Component*>{&ruleList_, &problems_, &notes_, &problemCount_, &noteCount_}) addAndMakeVisible(*c);

  chopModel_.count = [this] { return static_cast<int>(chopLabels_.size()); };
  chopModel_.text = [this](int r) { return r < static_cast<int>(chopLabels_.size()) ? chopLabels_[static_cast<std::size_t>(r)] : juce::String(); };
  chopModel_.onSelect = [this](int) { reloadRoleChoices(); };
  chopList_.setModel(&chopModel_);
  chopList_.setTitle("Chop roles");
  addChildComponent(chopList_);
  addChildComponent(chopRole_);
  addChildComponent(audition_);
  addChildComponent(emptyRoles_);
  emptyRoles_.setText("Detect chops first; roles are assigned to chops.", juce::dontSendNotification);
  chopRole_.setEditableText(true);
  chopRole_.setTitle("Role for the selected chop");
  chopRole_.onChange = [this] { assignRoleFromCombo(); };
  audition_.setTitle("Audition chop");
  audition_.onClick = [this] {
    const int r = chopList_.getSelectedRow();
    if (r >= 0 && r < static_cast<int>(chopIds_.size())) proc_.audition(chopIds_[static_cast<std::size_t>(r)]);
  };

  // ---- rule buttons and templates ----
  for (juce::TextButton* b : {&add_, &duplicate_, &delete_, &keepFirst_, &noRepeats_, &noRoleRepeats_, &needFinal_, &preferBoundaries_, &excludeBars_,
                              &applyBtn_, &revertBtn_, &applyMutate_, &test_, &why_, &close_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  add_.onClick = [this] {
    if (!model_.canAdd()) return setStatus("The limit is 64 rules.");
    selectedId_ = model_.addRule();
    refreshAll();
    refreshForm();
  };
  duplicate_.onClick = [this] {
    const auto id = model_.duplicate(selectedId_);
    if (id) selectedId_ = id;
    refreshAll();
    refreshForm();
  };
  delete_.onClick = [this] {
    model_.remove(selectedId_);
    selectedId_ = 0;
    refreshAll();
    refreshForm();
  };
  keepFirst_.onClick = [this] { addTemplate(TemplateKind::KeepFirst); };
  noRepeats_.onClick = [this] { addTemplate(TemplateKind::NoRepeats); };
  noRoleRepeats_.onClick = [this] { addTemplate(TemplateKind::NoRoleRepeats); };
  needFinal_.onClick = [this] { addTemplate(TemplateKind::NeedInFinalBeat); };
  preferBoundaries_.onClick = [this] { addTemplate(TemplateKind::PreferAtBoundaries); };
  excludeBars_.onClick = [this] { addTemplate(TemplateKind::ExcludeFromBars); };
  applyBtn_.onClick = [this] { apply(); };
  revertBtn_.onClick = [this] { revert(); };
  applyMutate_.onClick = [this] {
    if (applyOnly() && hooks_.applyAndMutate) hooks_.applyAndMutate();
  };
  test_.onClick = [this] { testRules(); };
  why_.onClick = [this] { why(); };
  close_.onClick = [this] { close(); };
  templatesLabel_.setText("Templates:", juce::dontSendNotification);
  addAndMakeVisible(templatesLabel_);
  templateRole_.setTitle("Template role");
  templateFrom_.setTitle("Template bar from");
  templateTo_.setTitle("Template bar to");
  for (juce::TextEditor* e : {&templateFrom_, &templateTo_}) {
    e->setInputRestrictions(1, "12345678");
    addAndMakeVisible(*e);
  }
  templateFrom_.setText("1", false);
  templateTo_.setText("2", false);
  addAndMakeVisible(templateRole_);

  // ---- edit form ----
  enabled_.setTitle("Rule enabled");
  action_.setTitle("Action");
  action_.addItemList({"Keep", "Require", "Forbid", "Limit repeats", "Limit count per bar", "Prefer / avoid"}, 1);
  position_.setTitle("Position");
  position_.addItemList({"Anywhere", "First in bar", "Final beat", "Phrase boundary"}, 1);
  repeatBy_.setTitle("Repeat by");
  repeatBy_.addItemList({"Same chop", "Same role"}, 1);
  role_.setTitle("Role");
  role_.setEditableText(true);
  chop_.setTitle("Chop");
  barFrom_.setTitle("Bar from");
  barTo_.setTitle("Bar to");
  beatFrom_.setTitle("Beat from");
  beatTo_.setTitle("Beat to");
  count_.setTitle("Count");
  label_.setTitle("Label");
  label_.setInputRestrictions(128);
  for (juce::TextEditor* e : {&barFrom_, &barTo_, &beatFrom_, &beatTo_, &count_}) e->setInputRestrictions(3, "0123456789");
  weight_.setTitle("Weight");
  weight_.setRange(0.1, 10.0, 0.01);
  weight_.setSkewFactorFromMidPoint(1.0);
  weight_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
  weight_.textFromValueFunction = [](double v) { return "x" + juce::String(v, 1); };
  priority_.setTitle("Priority");
  priority_.setRange(-1000, 1000, 1);
  priority_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
  priority_.setTooltip("Rules are checked highest priority first. A Require of higher priority overrides a Forbid of lower priority.");
  auto caption = [this](const char* text) {
    auto l = std::make_unique<juce::Label>();
    l->setText(text, juce::dontSendNotification);
    addAndMakeVisible(*l);
    captions_.push_back(std::move(l));
    return captions_.back().get();
  };
  caption("Action");
  caption("Role");
  caption("Chop");
  caption("Position");
  caption("Bars");
  caption("Beats");
  countCaption_ = caption("Count");
  repeatCaption_ = caption("Repeat by");
  weightCaption_ = caption("Weight");
  caption("Priority");
  caption("Label");
  for (juce::Component* c : std::initializer_list<juce::Component*>{&enabled_, &action_, &role_, &chop_, &position_, &repeatBy_, &barFrom_, &barTo_, &beatFrom_,
                                                                     &beatTo_, &count_, &label_, &weight_, &priority_})
    addAndMakeVisible(*c);
  action_.onChange = [this] { formChanged(); updateFormVisibility(); };
  for (juce::ComboBox* c : {&role_, &chop_, &position_, &repeatBy_}) c->onChange = [this] { formChanged(); };
  enabled_.onClick = [this] { formChanged(); };
  for (juce::TextEditor* e : {&barFrom_, &barTo_, &beatFrom_, &beatTo_, &count_, &label_}) e->onTextChange = [this] { formChanged(); };
  weight_.onValueChange = [this] { formChanged(); };
  priority_.onValueChange = [this] { formChanged(); };
  setVisible(false);
  showTab(Tab::Rules);
}

void RulesPanel::setStatus(const juce::String& text) { proc_.setStatusMessage(text); }

rl::RuleSet RulesPanel::currentRulesOfSession() const {
  rl::RuleSet rs;
  const_cast<ChopFractalProcessor&>(proc_).withSession([&](cf::composition::ProjectSession& s) { rs = s.rules(); });
  return rs;
}

void RulesPanel::open() {
  reloadFromSession(false);
  setVisible(true);
  toFront(true);
  grabKeyboardFocus();
}

void RulesPanel::close() { setVisible(false); }

bool RulesPanel::keyPressed(const juce::KeyPress& key) {
  if (key == juce::KeyPress::escapeKey) {
    close();
    return true;
  }
  return false;
}

void RulesPanel::reloadFromSession(bool announce, bool force) {
  rl::RuleSet rules;
  rl::RoleMap roleMap;
  int bars = 0;
  std::vector<cf::ChopId> ids;
  double rate = 48000.0;
  std::vector<cf::SampleRange> ranges;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    rules = s.rules();
    roleMap = s.roleMap();
    if (s.pattern()) bars = static_cast<int>(s.pattern()->bars.size());
    if (s.chops()) {
      rate = s.chops()->sampleRate > 0 ? s.chops()->sampleRate : 48000.0;
      for (const auto& c : s.chops()->chops) {
        ids.push_back(c.id);
        ranges.push_back(c.range);
      }
    }
  });
  if (!model_.dirty() || announce || force) {
    model_.reset(rules, roleMap, bars, ids);
    if (announce) setStatus("The project's rules were reloaded; unapplied changes were discarded.");
  } else {
    model_.setRoles(roleMap);
  }
  lastSessionRules_ = rules;
  chopIds_ = ids;
  chopLabels_.clear();
  for (std::size_t i = 0; i < ids.size(); ++i)
    chopLabels_.push_back("Chop " + juce::String(i + 1) + "   " + clock(static_cast<double>(ranges[i].start) / rate) + " - " + clock(static_cast<double>(ranges[i].end) / rate) +
                          "   " + (roleMap.roleOf(ids[i]).empty() ? juce::String("(none)") : juce::String(roleMap.roleOf(ids[i]))));
  templateRole_.clear(juce::dontSendNotification);
  chop_.clear(juce::dontSendNotification);
  role_.clear(juce::dontSendNotification);
  role_.addItem("(any role)", 1);
  int id = 2;
  std::vector<juce::String> names;
  for (const auto& r : rl::builtinRoles()) names.push_back(r);
  for (const auto& kv : roleMap.all())
    if (std::find(names.begin(), names.end(), juce::String(kv.second)) == names.end()) names.push_back(kv.second);
  for (const auto& n : names) {
    templateRole_.addItem(n, id);
    role_.addItem(n, id + 100);
    ++id;
  }
  templateRole_.setSelectedId(2, juce::dontSendNotification);
  chop_.addItem("(any chop)", 1);
  for (std::size_t i = 0; i < ids.size(); ++i) chop_.addItem("Chop " + juce::String(i + 1), static_cast<int>(i) + 2);
  selectedId_ = model_.find(selectedId_) ? selectedId_ : 0;
  chopList_.updateContent();
  reloadRoleChoices();
  refreshAll();
  refreshForm();
}

void RulesPanel::reloadRoleChoices() {
  chopRole_.clear(juce::dontSendNotification);
  chopRole_.addItem("(none)", 1);
  int id = 2;
  std::vector<juce::String> names;
  for (const auto& r : rl::builtinRoles()) names.push_back(r);
  std::string current;
  const int row = chopList_.getSelectedRow();
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    for (const auto& kv : s.roleMap().all())
      if (std::find(names.begin(), names.end(), juce::String(kv.second)) == names.end()) names.push_back(kv.second);
    if (row >= 0 && row < static_cast<int>(chopIds_.size())) current = s.roleMap().roleOf(chopIds_[static_cast<std::size_t>(row)]);
  });
  for (const auto& n : names) chopRole_.addItem(n, id++);
  chopRole_.setText(current.empty() ? juce::String("(none)") : juce::String(current), juce::dontSendNotification);
}

void RulesPanel::assignRoleFromCombo() {
  const int row = chopList_.getSelectedRow();
  if (row < 0 || row >= static_cast<int>(chopIds_.size())) return;
  const juce::String text = chopRole_.getText().trim();
  cf::Status st;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    const cf::ChopId chop = chopIds_[static_cast<std::size_t>(row)];
    st = (text.isEmpty() || text == "(none)") ? s.clearRole(chop) : s.assignRole(chop, text.toStdString());
  });
  setStatus(st.ok() ? juce::String("Role updated. It takes effect at the next Generate or Mutate.") : juce::String(st.error().message));
  const int keep = row;
  reloadFromSession(false);
  chopList_.selectRow(keep);
}

void RulesPanel::showTab(Tab tab) {
  tab_ = tab;
  const bool rules = tab == Tab::Rules;
  for (juce::Component* c : std::initializer_list<juce::Component*>{&ruleList_, &add_, &duplicate_, &delete_, &templatesLabel_, &templateRole_, &templateFrom_, &templateTo_, &keepFirst_,
                                                                     &noRepeats_, &noRoleRepeats_, &needFinal_, &preferBoundaries_, &excludeBars_, &enabled_, &action_, &role_, &chop_,
                                                                     &position_, &barFrom_, &barTo_, &beatFrom_, &beatTo_, &label_, &priority_})
    c->setVisible(rules);
  for (auto& c : captions_) c->setVisible(rules);
  chopList_.setVisible(!rules && !chopIds_.empty());
  chopRole_.setVisible(!rules && !chopIds_.empty());
  audition_.setVisible(!rules && !chopIds_.empty());
  emptyRoles_.setVisible(!rules && chopIds_.empty());
  if (rules) updateFormVisibility();
  else for (juce::Component* c : std::initializer_list<juce::Component*>{&count_, &repeatBy_, &weight_}) c->setVisible(false);
  resized();
  repaint();
}

void RulesPanel::updateFormVisibility() {
  if (tab_ != Tab::Rules) return;
  const rl::ActionKind k = kActions[juce::jlimit(0, 5, action_.getSelectedItemIndex())];
  const bool showCount = k == rl::ActionKind::MaxConsecutiveRepeats || k == rl::ActionKind::MaxCount;
  count_.setVisible(showCount);
  countCaption_->setVisible(showCount);
  repeatBy_.setVisible(k == rl::ActionKind::MaxConsecutiveRepeats);
  repeatCaption_->setVisible(k == rl::ActionKind::MaxConsecutiveRepeats);
  weight_.setVisible(k == rl::ActionKind::Prefer);
  weightCaption_->setVisible(k == rl::ActionKind::Prefer);
}

rl::Rule* RulesPanel::selectedDraftRule() {
  return const_cast<rl::Rule*>(model_.find(selectedId_));
}

void RulesPanel::refreshAll() {
  rows_ = model_.rows();
  issues_ = model_.issues();
  ruleList_.updateContent();
  problems_.updateContent();
  notes_.updateContent();
  int selectedRow = -1;
  for (std::size_t i = 0; i < rows_.size(); ++i)
    if (rows_[i].id == selectedId_) selectedRow = static_cast<int>(i);
  loading_ = true;
  if (selectedRow >= 0) ruleList_.selectRow(selectedRow, false, false);
  else ruleList_.deselectAllRows();
  loading_ = false;
  int errors = 0;
  for (const auto& i : issues_) errors += i.error;
  problemCount_.setText(issues_.empty() ? "No problems"
                                        : juce::String(issues_.size()) + (issues_.size() == 1 ? " problem" : " problems") + " (" + juce::String(errors) + (errors == 1 ? " error)" : " errors)"),
                        juce::dontSendNotification);
  int noteCount = noteModel_.getNumRows();
  noteCount_.setText(noteCount == 0 ? "Nothing was blocked by the last Generate, Mutate or Evolve step." : juce::String(noteCount) + " blocked change(s) in the last step",
                     juce::dontSendNotification);
  applyBtn_.setEnabled(model_.canApply());
  revertBtn_.setEnabled(model_.dirty());
  juce::String firstError;
  for (const auto& i : issues_)
    if (i.error) {
      firstError = i.text;
      break;
    }
  applyBtn_.setTooltip(firstError.isNotEmpty() ? firstError : juce::String("Apply the edited rules. They take effect at the next Generate or Mutate."));
  add_.setEnabled(model_.canAdd());
  add_.setTooltip(model_.canAdd() ? juce::String("Add a new rule.") : juce::String("The limit is 64 rules."));
  duplicate_.setEnabled(model_.find(selectedId_) != nullptr && model_.canAdd());
  delete_.setEnabled(model_.find(selectedId_) != nullptr);
  repaint();
}

void RulesPanel::refreshForm() {
  const rl::Rule* r = model_.find(selectedId_);
  const bool has = r != nullptr;
  for (juce::Component* c : std::initializer_list<juce::Component*>{&enabled_, &action_, &role_, &chop_, &position_, &repeatBy_, &barFrom_, &barTo_, &beatFrom_, &beatTo_, &count_, &label_, &weight_, &priority_})
    c->setEnabled(has);
  if (!has) return;
  loading_ = true;
  enabled_.setToggleState(r->enabled, juce::dontSendNotification);
  action_.setSelectedItemIndex(actionIndex(r->action.kind), juce::dontSendNotification);
  role_.setText(r->when.role.empty() ? juce::String("(any role)") : juce::String(r->when.role), juce::dontSendNotification);
  int chopId = 1;
  for (std::size_t i = 0; i < chopIds_.size(); ++i)
    if (chopIds_[i] == r->when.chop) chopId = static_cast<int>(i) + 2;
  chop_.setSelectedId(chopId, juce::dontSendNotification);
  position_.setSelectedItemIndex(static_cast<int>(r->when.position), juce::dontSendNotification);
  repeatBy_.setSelectedItemIndex(static_cast<int>(r->action.repeatBy), juce::dontSendNotification);
  barFrom_.setText(juce::String(r->when.barMin + 1), false);
  barTo_.setText(r->when.barMax < 0 ? juce::String() : juce::String(r->when.barMax + 1), false);
  beatFrom_.setText(juce::String(r->when.beatMin + 1), false);
  beatTo_.setText(r->when.beatMax < 0 ? juce::String() : juce::String(r->when.beatMax + 1), false);
  count_.setText(juce::String(r->action.count), false);
  label_.setText(r->label, false);
  weight_.setValue(juce::jlimit(0.1, 10.0, r->weight), juce::dontSendNotification);
  priority_.setValue(r->priority, juce::dontSendNotification);
  loading_ = false;
  updateFormVisibility();
}

void RulesPanel::formChanged() {
  if (loading_) return;
  const rl::Rule* cur = model_.find(selectedId_);
  if (!cur) return;
  rl::Rule r = *cur;
  r.enabled = enabled_.getToggleState();
  r.action.kind = kActions[juce::jlimit(0, 5, action_.getSelectedItemIndex())];
  const juce::String roleText = role_.getText().trim();
  r.when.role = (roleText == "(any role)") ? std::string() : roleText.toStdString();
  const int chopSel = chop_.getSelectedId();
  r.when.chop = (chopSel >= 2 && chopSel - 2 < static_cast<int>(chopIds_.size())) ? chopIds_[static_cast<std::size_t>(chopSel - 2)] : cf::ChopId{};
  r.when.position = static_cast<rl::Position>(juce::jlimit(0, 3, position_.getSelectedItemIndex()));
  r.action.repeatBy = static_cast<rl::RepeatBy>(juce::jlimit(0, 1, repeatBy_.getSelectedItemIndex()));
  r.when.barMin = parseInt(barFrom_.getText(), 1) - 1;
  r.when.barMax = barTo_.getText().trim().isEmpty() ? -1 : parseInt(barTo_.getText(), 1) - 1;
  r.when.beatMin = parseInt(beatFrom_.getText(), 1) - 1;
  r.when.beatMax = beatTo_.getText().trim().isEmpty() ? -1 : parseInt(beatTo_.getText(), 1) - 1;
  r.action.count = parseInt(count_.getText(), 0);
  r.label = label_.getText().toStdString();
  r.weight = r.action.kind == rl::ActionKind::Prefer ? weight_.getValue() : r.weight;
  r.priority = static_cast<int>(priority_.getValue());
  model_.update(r);
  refreshAll();
}

void RulesPanel::addTemplate(TemplateKind kind) {
  const rl::RoleId role = templateRole_.getText().trim().toStdString();
  const int from = parseInt(templateFrom_.getText(), 1), to = parseInt(templateTo_.getText(), 1);
  const auto id = model_.addFromTemplate(kind, role, from, to);
  if (!id) {
    setStatus(kind == TemplateKind::ExcludeFromBars ? "Bar from must be 1 to 8 and not after Bar to." : "The limit is 64 rules.");
    return;
  }
  selectedId_ = id;
  refreshAll();
  refreshForm();
  label_.grabKeyboardFocus();
}

bool RulesPanel::applyOnly() {
  if (!model_.canApply() && model_.dirty()) {
    for (const auto& i : model_.issues())
      if (i.error) setStatus(juce::String(i.text));
    return false;
  }
  cf::Status st;
  proc_.withSession([&](cf::composition::ProjectSession& s) { st = s.setRules(model_.draft()); });
  if (!st.ok()) {
    setStatus(st.error().message);
    return false;
  }
  model_.markApplied();
  proc_.withSession([&](cf::composition::ProjectSession& s) { lastSessionRules_ = s.rules(); });
  setStatus("Rules applied. They take effect at the next Generate or Mutate.");
  refreshAll();
  return true;
}

void RulesPanel::apply() { applyOnly(); }

void RulesPanel::revert() {
  reloadFromSession(false, true);
  selectedId_ = 0;
  refreshAll();
  refreshForm();
  setStatus("Reverted to the applied rules.");
}

void RulesPanel::testRules() {
  cf::Result<cf::composition::RulePreview> pv = cf::makeError(cf::ErrorCode::InvalidArgument, "load a source first");
  bool defaults = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    cf::pattern::Settings settings;
    if (s.pattern()) settings = s.pattern()->settings;
    else {
      defaults = true;
      if (hooks_.densityHint) settings.density = hooks_.densityHint();
    }
    pv = s.previewRules(model_.draft(), settings);
  });
  if (!pv.ok()) return setStatus(pv.error().message);
  juce::String text = defaults ? "Test (default settings): " : "Test: ";
  if (pv.value().blocked == 0) text += "no rule blocks anything with the current settings.";
  else {
    text += juce::String(pv.value().blocked) + (pv.value().blocked == 1 ? " change would be blocked by " : " changes would be blocked by ");
    for (std::size_t i = 0; i < pv.value().blockedByRule.size(); ++i) {
      const auto& [rule, n] = pv.value().blockedByRule[i];
      text += (i ? ", " : "") + juce::String(n) + " by " + (rule ? "rule " + juce::String(rule) : juce::String("a rule"));
    }
    text += ". Nothing was changed.";
  }
  setStatus(text);
}

void RulesPanel::why() {
  const cf::EventId id = hooks_.primarySelection ? hooks_.primarySelection() : cf::EventId{};
  if (!id.valid()) return setStatus("Select a hit first; Why? explains that hit.");
  cf::Result<cf::composition::EventExplanation> ex = cf::makeError(cf::ErrorCode::NotFound, "that hit does not exist");
  proc_.withSession([&](cf::composition::ProjectSession& s) { ex = s.explainEvent(id); });
  if (!ex.ok()) return setStatus(ex.error().message);
  const juce::String text(ex.value().text);
  setStatus(text);
}

void RulesPanel::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(0xf015181d));
  g.setColour(juce::Colours::white.withAlpha(0.25f));
  g.drawRect(getLocalBounds());
}

void RulesPanel::resized() {
  auto r = getLocalBounds().reduced(8);
  auto top = r.removeFromTop(26);
  header_.setBounds(top.removeFromLeft(150));
  rulesTab_.setBounds(top.removeFromLeft(80).reduced(2));
  rolesTab_.setBounds(top.removeFromLeft(80).reduced(2));
  auto footer = r.removeFromBottom(30);
  const int fw[] = {70, 70, 130, 100, 70, 70};
  juce::TextButton* fb[] = {&applyBtn_, &revertBtn_, &applyMutate_, &test_, &why_, &close_};
  for (int i = 0; i < 6; ++i) fb[i]->setBounds(footer.removeFromLeft(fw[i]).reduced(2));
  auto bottom = r.removeFromBottom(112);
  auto problemsArea = bottom.removeFromLeft(bottom.getWidth() / 2 - 4);
  problemCount_.setBounds(problemsArea.removeFromTop(18));
  problems_.setBounds(problemsArea);
  bottom.removeFromLeft(8);
  noteCount_.setBounds(bottom.removeFromTop(18));
  notes_.setBounds(bottom);
  r.removeFromBottom(4);

  auto left = r.removeFromLeft(370);
  r.removeFromLeft(10);
  auto btnRow = left.removeFromTop(26);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&add_, &duplicate_, &delete_}) b->setBounds(btnRow.removeFromLeft(88).reduced(2));
  auto tmpl = left.removeFromBottom(112);
  ruleList_.setBounds(left);
  templatesLabel_.setBounds(tmpl.removeFromTop(22).removeFromLeft(80));
  auto trow = tmpl.removeFromTop(26);
  templateRole_.setBounds(trow.removeFromLeft(130).reduced(2));
  templateFrom_.setBounds(trow.removeFromLeft(40).reduced(2));
  templateTo_.setBounds(trow.removeFromLeft(40).reduced(2));
  auto t1 = tmpl.removeFromTop(26);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&keepFirst_, &noRepeats_, &noRoleRepeats_}) b->setBounds(t1.removeFromLeft(118).reduced(2));
  auto t2 = tmpl.removeFromTop(26);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&needFinal_, &preferBoundaries_, &excludeBars_}) b->setBounds(t2.removeFromLeft(118).reduced(2));

  // The edit form (Rules tab) or the chop list (Roles tab) fill the right-hand side.
  if (tab_ == Tab::Roles) {
    auto area = left;
    (void)area;
  }
  auto form = r;
  auto row = [&](int index, juce::Component& c, int captionIndex, int width = -1) {
    auto line = form.removeFromTop(26);
    if (captionIndex >= 0) captions_[static_cast<std::size_t>(captionIndex)]->setBounds(line.removeFromLeft(80));
    c.setBounds(width > 0 ? line.removeFromLeft(width) : line);
    (void)index;
  };
  enabled_.setBounds(form.removeFromTop(26).removeFromLeft(200));
  row(0, action_, 0, 200);
  row(1, role_, 1, 200);
  row(2, chop_, 2, 200);
  row(3, position_, 3, 200);
  {
    auto line = form.removeFromTop(26);
    captions_[4]->setBounds(line.removeFromLeft(80));
    barFrom_.setBounds(line.removeFromLeft(50).reduced(2));
    barTo_.setBounds(line.removeFromLeft(50).reduced(2));
  }
  {
    auto line = form.removeFromTop(26);
    captions_[5]->setBounds(line.removeFromLeft(80));
    beatFrom_.setBounds(line.removeFromLeft(50).reduced(2));
    beatTo_.setBounds(line.removeFromLeft(50).reduced(2));
  }
  {
    auto line = form.removeFromTop(26);  // Count / Repeat by / Weight share one line; only the matching ones are visible
    countCaption_->setBounds(line.removeFromLeft(80));
    count_.setBounds(line.removeFromLeft(60).reduced(2));
    repeatCaption_->setBounds(line.removeFromLeft(80));
    repeatBy_.setBounds(line.removeFromLeft(120).reduced(2));
  }
  {
    auto line = form.removeFromTop(26);
    weightCaption_->setBounds(line.removeFromLeft(80));
    weight_.setBounds(line.removeFromLeft(260));
  }
  row(0, priority_, 9, 340);
  row(0, label_, 10, 340);

  if (tab_ == Tab::Roles) {
    auto area = getLocalBounds().reduced(8);
    area.removeFromTop(26);
    area.removeFromBottom(30 + 112 + 4);
    chopList_.setBounds(area.removeFromLeft(560));
    area.removeFromLeft(10);
    chopRole_.setBounds(area.removeFromTop(26).removeFromLeft(220));
    audition_.setBounds(area.removeFromTop(30).removeFromLeft(140).reduced(2));
    emptyRoles_.setBounds(getLocalBounds().reduced(8).withTrimmedTop(40).removeFromTop(26));
  }
}
