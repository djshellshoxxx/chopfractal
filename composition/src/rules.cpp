// Rule editor support: role clearing, the last blocked changes, "why is this hit here" and a side-effect-free rule test
// (docs/specs/roadmap/F06-rule-editor.md). Message thread only.
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

namespace chopfractal::composition {
namespace {

std::string weightText(double w) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.2g", w);
  return buf;
}

const roles::Rule* ruleById(const roles::RuleSet& set, std::uint32_t id) {
  for (const roles::Rule& r : set.rules)
    if (r.id == id) return &r;
  return nullptr;
}

}  // namespace

Status ProjectSession::clearRole(ChopId chop) {
  if (!chopMap_ || !chopMap_->find(chop)) return makeError(ErrorCode::NotFound, "unknown chop");
  roles_.clearRole(chop);
  publishPadBank();
  return {};
}

void ProjectSession::recordRuleNotes(const pattern::GenerationReport& report) {
  ruleNotes_.clear();
  for (const pattern::Note& n : report.notes) {
    if (n.kind != pattern::Note::Kind::BlockedByRule) continue;
    RuleNote note;
    note.bar = n.bar;
    note.beat = n.beat;
    note.rule = n.rule;
    const roles::Rule* r = ruleById(rules_, n.rule);
    const std::string who = r ? "rule " + std::to_string(r->id) + (r->label.empty() ? std::string() : " (\"" + r->label + "\")") : std::string("a rule");
    note.text = "Bar " + std::to_string(n.bar + 1) + ", beat " + std::to_string(n.beat + 1) + ": " + who + " blocked a change; the previous content was kept.";
    ruleNotes_.push_back(std::move(note));
  }
  if (ruleNotes_.size() > 64) ruleNotes_.erase(ruleNotes_.begin(), ruleNotes_.end() - 64);  // newest kept
}

Result<EventExplanation> ProjectSession::explainEvent(EventId id) const {
  if (!patterns_.hasPattern() || !chops_) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  const pattern::Pattern& p = patterns_.current();
  const Event* e = pattern::findEvent(p, id);
  if (!e || !pattern::locate(p, id)) return makeError(ErrorCode::NotFound, "that hit does not exist");
  EventExplanation out;
  out.handPlaced = e->userOwned;
  const auto policy = makePolicy();
  if (!policy) {
    out.text = "No rules or roles are set, so nothing constrains this hit.";
    return out;
  }
  const CandidateQuery q = pattern::candidateQueryFor(p, id);
  const Decision d = policy->evaluate(q);
  const std::uint32_t protector = policy->preservingRule(q);
  out.allowed = d.allowed;
  out.weight = d.weight;
  out.preserved = protector != 0;
  std::string text = policy->explain(d);
  if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
  text += ".";
  std::vector<std::uint32_t> used;
  for (const TraceEntry& t : d.trace) {
    used.push_back(t.rule);
    if (t.effect == TraceEntry::Effect::Weighted)
      if (const roles::Rule* r = ruleById(rules_, t.rule)) text += " Rule " + std::to_string(t.rule) + " multiplies its weight by x" + weightText(r->weight) + ".";
  }
  if (protector != 0) {
    used.push_back(protector);
    const roles::Rule* r = ruleById(rules_, protector);
    text += " Mutate keeps it because rule " + std::to_string(protector) + (r && !r->label.empty() ? " (\"" + r->label + "\")" : std::string()) + " protects it.";
  }
  if (out.handPlaced && !d.allowed) text += " You placed this hit by hand, so Mutate keeps it even though the rules reject it.";
  if (d.trace.empty() && protector == 0) text += " No rule applies to this hit.";
  std::sort(used.begin(), used.end());
  used.erase(std::unique(used.begin(), used.end()), used.end());
  out.rules = std::move(used);
  out.text = std::move(text);
  return out;
}

Result<RulePreview> ProjectSession::previewRules(const roles::RuleSet& draft, const pattern::Settings& settings) const {
  if (!chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  const roles::ValidationReport rep = roles::validateRules(draft, &roles_);
  if (!rep.ok())
    for (const roles::Issue& i : rep.issues)
      if (i.severity == roles::Issue::Severity::Error) return makeError(ErrorCode::InvalidArgument, "rule " + std::to_string(i.rule) + ": " + i.message);
  std::vector<ChopId> ids;
  for (const ChopInfo& c : chops_->chops) ids.push_back(c.id);
  const roles::GrammarPolicy policy(roles_, draft, ids);
  pattern::GenerationReport report;
  auto scratch = pattern::generate(*chops_, settings, &policy, &report);
  if (!scratch.ok()) return scratch.error();
  RulePreview out;
  out.events = static_cast<int>(pattern::eventCount(scratch.value()));
  std::map<std::uint32_t, int> by;
  for (const pattern::Note& n : report.notes)
    if (n.kind == pattern::Note::Kind::BlockedByRule) {
      ++out.blocked;
      ++by[n.rule];
    }
  out.blockedByRule.assign(by.begin(), by.end());
  return out;
}

}  // namespace chopfractal::composition
