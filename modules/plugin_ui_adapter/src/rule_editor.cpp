#include <chopfractal/plugin_ui_adapter/rule_editor.hpp>

#include <algorithm>

namespace chopfractal::ui {
namespace {

bool same(const roles::Rule& a, const roles::Rule& b) {
  return a.id == b.id && a.enabled == b.enabled && a.scope == b.scope && a.when.role == b.when.role && a.when.chop == b.when.chop &&
         a.when.position == b.when.position && a.when.barMin == b.when.barMin && a.when.barMax == b.when.barMax &&
         a.when.beatMin == b.when.beatMin && a.when.beatMax == b.when.beatMax && a.action.kind == b.action.kind &&
         a.action.count == b.action.count && a.action.repeatBy == b.action.repeatBy && a.priority == b.priority &&
         a.weight == b.weight && a.label == b.label;
}

bool engineBefore(const roles::Rule& a, const roles::Rule& b) {
  return a.priority != b.priority ? a.priority > b.priority : a.id < b.id;
}

}  // namespace

roles::RuleScope RuleEditorModel::scopeFor(roles::ActionKind kind) {
  using roles::ActionKind;
  using roles::RuleScope;
  switch (kind) {
    case ActionKind::Preserve: return RuleScope::Bar;
    case ActionKind::Require: return RuleScope::Beat;
    case ActionKind::Forbid: return RuleScope::Region;
    case ActionKind::MaxConsecutiveRepeats: return RuleScope::Phrase;
    case ActionKind::MaxCount: return RuleScope::Bar;
    case ActionKind::Prefer: return RuleScope::Event;
  }
  return RuleScope::Event;
}

void RuleEditorModel::reset(const roles::RuleSet& applied, const roles::RoleMap& roles, int patternBars, const std::vector<ChopId>& chops) {
  applied_ = draft_ = applied;
  roles_ = roles;
  patternBars_ = patternBars;
  chops_ = chops;
  counter_ = 1;
  for (const roles::Rule& r : draft_.rules) counter_ = std::max(counter_, r.id + 1);
}

const roles::Rule* RuleEditorModel::find(std::uint32_t id) const {
  for (const roles::Rule& r : draft_.rules)
    if (r.id == id) return &r;
  return nullptr;
}

bool RuleEditorModel::dirty() const {
  if (applied_.rules.size() != draft_.rules.size()) return true;
  for (std::size_t i = 0; i < draft_.rules.size(); ++i)
    if (!same(applied_.rules[i], draft_.rules[i])) return true;
  return false;
}

bool RuleEditorModel::canApply() const {
  if (!dirty()) return false;
  for (const RuleIssueView& i : issues())
    if (i.error) return false;
  return true;
}

std::uint32_t RuleEditorModel::nextId() const {
  std::uint32_t m = counter_;
  for (const roles::Rule& r : draft_.rules) m = std::max(m, r.id + 1);
  return m;
}

std::uint32_t RuleEditorModel::takeId() {
  const std::uint32_t id = nextId();
  counter_ = id + 1;  // never reused within the session, even after a delete
  return id;
}

std::uint32_t RuleEditorModel::addRule() {
  if (!canAdd()) return 0;
  roles::Rule r;
  r.id = takeId();
  r.action.kind = roles::ActionKind::Prefer;
  r.scope = scopeFor(r.action.kind);
  r.weight = 2.0;
  draft_.rules.push_back(r);
  return r.id;
}

std::uint32_t RuleEditorModel::addFromTemplate(TemplateKind kind, const roles::RoleId& role, int barFrom1, int barTo1) {
  if (!canAdd()) return 0;
  if (kind == TemplateKind::ExcludeFromBars && !(barFrom1 >= 1 && barTo1 <= 8 && barFrom1 <= barTo1)) return 0;
  const std::uint32_t id = takeId();
  roles::Rule r;
  switch (kind) {
    case TemplateKind::KeepFirst: r = roles::templates::preserveFirst(id, role); break;
    case TemplateKind::NoRepeats: r = roles::templates::avoidAdjacentRepeats(id, roles::RepeatBy::SameChop); break;
    case TemplateKind::NoRoleRepeats: r = roles::templates::avoidAdjacentRepeats(id, roles::RepeatBy::SameRole); break;
    case TemplateKind::NeedInFinalBeat: r = roles::templates::requireInFinalBeat(id, role); break;
    case TemplateKind::PreferAtBoundaries: r = roles::templates::preferAtPhraseBoundary(id, role, 3.0); break;
    case TemplateKind::ExcludeFromBars: r = roles::templates::excludeRole(id, role, barFrom1 - 1, barTo1 - 1); break;
  }
  draft_.rules.push_back(r);
  return id;
}

std::uint32_t RuleEditorModel::duplicate(std::uint32_t id) {
  const roles::Rule* src = find(id);
  if (!src || !canAdd()) return 0;
  roles::Rule copy = *src;
  copy.id = takeId();
  copy.label = (copy.label + " (copy)").substr(0, 128);
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
  for (roles::Rule& r : draft_.rules)
    if (r.id == edited.id) {
      r = edited;
      r.scope = scopeFor(r.action.kind);
      return true;
    }
  return false;
}

std::vector<RuleIssueView> RuleEditorModel::issues() const {
  std::vector<RuleIssueView> out;
  for (const roles::Issue& i : roles::validateRules(draft_, &roles_).issues)
    out.push_back({i.rule, i.severity == roles::Issue::Severity::Error, i.message});
  for (const roles::Rule& r : draft_.rules) {
    if (patternBars_ > 0 && r.when.barMin >= patternBars_)
      out.push_back({r.id, false, "Rule " + std::to_string(r.id) + ": bars " + std::to_string(r.when.barMin + 1) +
                                      (r.when.barMax >= 0 ? "-" + std::to_string(r.when.barMax + 1) : std::string()) + " are outside this " +
                                      std::to_string(patternBars_) + "-bar pattern."});
    if (r.when.chop.valid() && std::find(chops_.begin(), chops_.end(), r.when.chop) == chops_.end() && !chops_.empty())
      out.push_back({r.id, false, "Rule " + std::to_string(r.id) + ": chop #" + std::to_string(r.when.chop.value) + " no longer exists."});
  }
  return out;
}

std::vector<RuleRowView> RuleEditorModel::rows() const {
  std::vector<roles::Rule> sorted = draft_.rules;
  std::stable_sort(sorted.begin(), sorted.end(), engineBefore);
  const std::vector<RuleIssueView> all = issues();
  std::vector<RuleRowView> out;
  for (const roles::Rule& r : sorted) {
    RuleRowView v;
    v.id = r.id;
    v.enabled = r.enabled;
    v.priority = r.priority;
    v.tooltip = roles::describe(r);
    for (const RuleIssueView& i : all)
      if (i.rule == r.id) (i.error ? v.hasError : v.hasWarning) = true;
    v.text = std::string(r.enabled ? "[x]" : "[ ]") + " #" + std::to_string(r.id) + "  " + (r.label.empty() ? v.tooltip : r.label) + " P" +
             std::to_string(r.priority) + (v.hasError ? "  Error" : (v.hasWarning ? "  Warning" : ""));
    out.push_back(std::move(v));
  }
  return out;
}

}  // namespace chopfractal::ui
