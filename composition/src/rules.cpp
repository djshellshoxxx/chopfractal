// Rule editor support on ProjectSession (G2 / F06): clearing a role, remembering what the rules blocked, explaining one hit and
// testing a draft rule set without touching the pattern. Message thread only, like the rest of the session.
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace chopfractal::composition {
namespace {
constexpr std::size_t kMaxRuleNotes = 64;

std::string weightText(double w) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.2g", w);
  return buf;
}
std::string capitalize(std::string s) {
  if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
  return s;
}
const roles::Rule* ruleById(const roles::RuleSet& set, std::uint32_t id) {
  for (const roles::Rule& r : set.rules)
    if (r.id == id) return &r;
  return nullptr;
}
}  // namespace

Status ProjectSession::clearRole(ChopId chop) {
  if (!chopMap_ || !chopMap_->find(chop)) return makeError(ErrorCode::NotFound, "unknown chop");
  if (roles_.roleOf(chop).empty()) return {};
  roles_.clearRole(chop);
  publishPadBank();
  if (!routing_.roleBus.empty()) return republish();
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
    std::string who = "a rule";
    if (const roles::Rule* r = ruleById(rules_, n.rule)) who = "rule " + std::to_string(r->id) + (r->label.empty() ? std::string() : " (\"" + r->label + "\")");
    else if (n.rule) who = "rule " + std::to_string(n.rule);
    note.text = "Bar " + std::to_string(n.bar + 1) + ", beat " + std::to_string(n.beat + 1) + ": " + who + " blocked a change; the previous content was kept.";
    ruleNotes_.push_back(std::move(note));
  }
  if (ruleNotes_.size() > kMaxRuleNotes) ruleNotes_.erase(ruleNotes_.begin(), ruleNotes_.begin() + static_cast<std::ptrdiff_t>(ruleNotes_.size() - kMaxRuleNotes));
}

Result<EventExplanation> ProjectSession::explainEvent(EventId id) const {
  const pattern::Pattern* p = pattern();
  if (!p || !chops_) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  const Event* e = pattern::findEvent(*p, id);
  if (!e) return makeError(ErrorCode::NotFound, "that hit no longer exists");
  EventExplanation out;
  out.handPlaced = e->userOwned;
  auto policy = makePolicy();
  if (!policy) {
    out.text = "No rules or roles are set, so nothing constrains this hit.";
    return out;
  }
  const CandidateQuery q = pattern::candidateQueryFor(*p, id);
  const Decision d = policy->evaluate(q);
  const std::uint32_t protector = policy->preservingRule(q);
  out.allowed = d.allowed;
  out.preserved = protector != 0;
  out.weight = d.weight;
  std::set<std::uint32_t> involved;
  for (const TraceEntry& t : d.trace) involved.insert(t.rule);
  if (protector) involved.insert(protector);
  out.rules.assign(involved.begin(), involved.end());
  std::string text = capitalize(policy->explain(d)) + ".";
  for (const TraceEntry& t : d.trace)
    if (t.effect == TraceEntry::Effect::Weighted)
      if (const roles::Rule* r = ruleById(policy->rules(), t.rule)) text += " Rule " + std::to_string(r->id) + " multiplies its weight by x" + weightText(r->weight) + ".";
  if (protector) {
    const roles::Rule* r = ruleById(policy->rules(), protector);
    text += " Mutate keeps it because rule " + std::to_string(protector) + (r && !r->label.empty() ? " (\"" + r->label + "\")" : std::string()) + " protects it.";
  }
  if (out.handPlaced && !d.allowed) text += " You placed this hit by hand, so Mutate keeps it even though the rules reject it.";
  if (d.trace.empty() && !protector) text += " No rule applies to this hit.";
  out.text = std::move(text);
  return out;
}

Result<RulePreview> ProjectSession::previewRules(const roles::RuleSet& draft, const pattern::Settings& settings) const {
  if (!chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  const roles::ValidationReport rep = roles::validateRules(draft, &roles_);
  for (const roles::Issue& i : rep.issues)
    if (i.severity == roles::Issue::Severity::Error) return makeError(ErrorCode::InvalidArgument, i.message);
  std::vector<ChopId> ids;
  for (const ChopInfo& c : chops_->chops) ids.push_back(c.id);
  const roles::GrammarPolicy policy(roles_, draft, ids);
  pattern::GenerationReport report;
  auto generated = pattern::generate(*chops_, settings, &policy, &report);
  if (!generated.ok()) return generated.error();
  RulePreview out;
  for (const pattern::Bar& bar : generated.value().bars)
    for (const pattern::Beat& beat : bar.beats) out.events += static_cast<int>(beat.events.size());
  std::map<std::uint32_t, int> byRule;
  for (const pattern::Note& n : report.notes)
    if (n.kind == pattern::Note::Kind::BlockedByRule) {
      ++out.blocked;
      ++byRule[n.rule];
    }
  for (const auto& kv : byRule) out.blockedByRule.emplace_back(kv.first, kv.second);
  return out;
}

}  // namespace chopfractal::composition
