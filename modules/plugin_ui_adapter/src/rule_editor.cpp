#include <chopfractal/plugin_ui_adapter/rule_editor.hpp>

#include <algorithm>

namespace chopfractal::ui {
namespace {
bool sameRule(const roles::Rule& a, const roles::Rule& b) {
  return a.id == b.id && a.enabled == b.enabled && a.scope == b.scope && a.when.role == b.when.role && a.when.chop == b.when.chop && a.when.position == b.when.position &&
         a.when.barMin == b.when.barMin && a.when.barMax == b.when.barMax && a.when.beatMin == b.when.beatMin && a.when.beatMax == b.when.beatMax && a.action.kind == b.action.kind &&
         a.action.count == b.action.count && a.action.repeatBy == b.action.repeatBy && a.priority == b.priority && a.weight == b.weight && a.label == b.label;
}
}  // namespace

roles::RuleScope RuleEditorModel::scopeFor(roles::ActionKind k) {
  using roles::ActionKind;
  using roles::RuleScope;
  switch (k) {
    case ActionKind::Preserve: return RuleScope::Bar;
    case ActionKind::Require: return RuleScope::Beat;
    case ActionKind::Forbid: return RuleScope::Region;
    case ActionKind::MaxConsecutiveRepeats: return RuleScope::Phrase;
    case ActionKind::MaxCount: return RuleScope::Bar;
    case ActionKind::Prefer: return RuleScope::Event;
  }
  return RuleScope::Event;
}

const char* RuleEditorModel::actionName(roles::ActionKind k) {
  using roles::ActionKind;
  switch (k) {
    case ActionKind::Preserve: return "Keep";
    case ActionKind::Require: return "Require";
    case ActionKind::Forbid: return "Forbid";
    case ActionKind::MaxConsecutiveRepeats: return "Limit repeats";
    case ActionKind::MaxCount: return "Limit count per bar";
    case ActionKind::Prefer: return "Prefer / avoid";
  }
  return "";
}

void RuleEditorModel::reset(const roles::RuleSet& applied, const roles::RoleMap& roles, int patternBars, const std::vector<ChopId>& chops) {
  applied_ = applied;
  draft_ = applied;
  roles_ = roles;
  patternBars_ = patternBars;
  chops_ = chops;
  for (const auto& r : draft_.rules) highWater_ = std::max(highWater_, r.id);
}

bool RuleEditorModel::dirty() const {
  if (draft_.rules.size() != applied_.rules.size()) return true;
  for (std::size_t i = 0; i < draft_.rules.size(); ++i)
    if (!sameRule(draft_.rules[i], applied_.rules[i])) return true;
  return false;
}

bool RuleEditorModel::canApply() const {
  if (!dirty()) return false;
  for (const auto& i : issues())
    if (i.error) return false;
  return true;
}

std::uint32_t RuleEditorModel::nextId() const {
  std::uint32_t m = highWater_;
  for (const auto& r : draft_.rules) m = std::max(m, r.id);
  return m + 1;
}

std::uint32_t RuleEditorModel::addRule() {
  if (!canAdd()) return 0;
  roles::Rule r;
  r.id = nextId();
  r.scope = scopeFor(roles::ActionKind::Prefer);
  r.action.kind = roles::ActionKind::Prefer;
  r.weight = 2.0;
  highWater_ = r.id;
  draft_.rules.push_back(r);
  return r.id;
}

std::uint32_t RuleEditorModel::addFromTemplate(TemplateKind kind, const roles::RoleId& role, int barFrom1, int barTo1) {
  if (!canAdd()) return 0;
  const std::uint32_t id = nextId();
  roles::Rule r;
  switch (kind) {
    case TemplateKind::KeepFirst: r = roles::templates::preserveFirst(id, role); break;
    case TemplateKind::NoRepeats: r = roles::templates::avoidAdjacentRepeats(id, roles::RepeatBy::SameChop); break;
    case TemplateKind::NoRoleRepeats: r = roles::templates::avoidAdjacentRepeats(id, roles::RepeatBy::SameRole); break;
    case TemplateKind::NeedInFinalBeat: r = roles::templates::requireInFinalBeat(id, role); break;
    case TemplateKind::PreferAtBoundaries: r = roles::templates::preferAtPhraseBoundary(id, role, 3.0); break;
    case TemplateKind::ExcludeFromBars:
      if (barFrom1 < 1 || barTo1 > 8 || barFrom1 > barTo1) return 0;
      r = roles::templates::excludeRole(id, role, barFrom1 - 1, barTo1 - 1);
      break;
  }
  highWater_ = id;
  draft_.rules.push_back(r);
  return id;
}

std::uint32_t RuleEditorModel::duplicate(std::uint32_t id) {
  if (!canAdd()) return 0;
  const roles::Rule* src = find(id);
  if (!src) return 0;
  roles::Rule copy = *src;
  copy.id = nextId();
  copy.label = (src->label + " (copy)").substr(0, 128);
  highWater_ = copy.id;
  draft_.rules.push_back(copy);
  return copy.id;
}

bool RuleEditorModel::remove(std::uint32_t id) {
  const auto it = std::find_if(draft_.rules.begin(), draft_.rules.end(), [&](const roles::Rule& r) { return r.id == id; });
  if (it == draft_.rules.end()) return false;
  draft_.rules.erase(it);
  return true;
}

bool RuleEditorModel::update(const roles::Rule& edited) {
  for (auto& r : draft_.rules)
    if (r.id == edited.id) {
      r = edited;
      r.scope = scopeFor(r.action.kind);
      return true;
    }
  return false;
}

const roles::Rule* RuleEditorModel::find(std::uint32_t id) const {
  for (const auto& r : draft_.rules)
    if (r.id == id) return &r;
  return nullptr;
}

std::vector<RuleIssueView> RuleEditorModel::issues() const {
  std::vector<RuleIssueView> out;
  for (const auto& i : roles::validateRules(draft_, &roles_).issues) {
    RuleIssueView v;
    v.rule = i.rule;
    v.error = i.severity == roles::Issue::Severity::Error;
    v.text = (i.rule ? "rule " + std::to_string(i.rule) + ": " : std::string()) + i.message;
    out.push_back(std::move(v));
  }
  for (const auto& r : draft_.rules) {
    if (patternBars_ > 0 && r.when.barMin >= patternBars_)
      out.push_back({r.id, false,
                     "Rule " + std::to_string(r.id) + ": bars " + std::to_string(r.when.barMin + 1) + "-" + std::to_string(r.when.barMax >= 0 ? r.when.barMax + 1 : r.when.barMin + 1) +
                         " are outside this " + std::to_string(patternBars_) + "-bar pattern."});
    if (r.when.chop.valid() && !chops_.empty() && std::find(chops_.begin(), chops_.end(), r.when.chop) == chops_.end())
      out.push_back({r.id, false, "Rule " + std::to_string(r.id) + ": chop #" + std::to_string(r.when.chop.value) + " no longer exists."});
  }
  return out;
}

std::string RuleEditorModel::issueSummary() const {
  const auto all = issues();
  if (all.empty()) return "No problems";
  int errors = 0;
  for (const auto& i : all) errors += i.error ? 1 : 0;
  std::string s = std::to_string(all.size()) + (all.size() == 1 ? " problem" : " problems");
  if (errors > 0) s += " (" + std::to_string(errors) + (errors == 1 ? " error)" : " errors)");
  return s;
}

std::vector<RuleRowView> RuleEditorModel::rows() const {
  const auto problems = issues();
  std::vector<const roles::Rule*> order;
  for (const auto& r : draft_.rules) order.push_back(&r);
  std::stable_sort(order.begin(), order.end(), [](const roles::Rule* a, const roles::Rule* b) { return a->priority != b->priority ? a->priority > b->priority : a->id < b->id; });
  std::vector<RuleRowView> out;
  for (const roles::Rule* r : order) {
    RuleRowView v;
    v.id = r->id;
    v.enabled = r->enabled;
    v.priority = r->priority;
    v.tooltip = roles::describe(*r);
    for (const auto& i : problems)
      if (i.rule == r->id) (i.error ? v.hasError : v.hasWarning) = true;
    v.text = std::string(r->enabled ? "[x] #" : "[ ] #") + std::to_string(r->id) + "  " + (r->label.empty() ? v.tooltip : r->label) + " P" + std::to_string(r->priority);
    if (v.hasError) v.text += "  Error";
    else if (v.hasWarning) v.text += "  Warning";
    out.push_back(std::move(v));
  }
  return out;
}

}  // namespace chopfractal::ui
