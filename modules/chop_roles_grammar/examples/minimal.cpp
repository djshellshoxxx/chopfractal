// Minimal consumer: assign roles, add two guided rules, and evaluate a candidate.
#include <chopfractal/chop_roles_grammar/grammar.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal;
  roles::RoleMap map;
  map.assignRole(ChopId{1}, "kick");
  map.assignRole(ChopId{2}, "hat");

  roles::RuleSet rules;
  rules.rules.push_back(roles::templates::preserveFirst(1, "kick"));
  rules.rules.push_back(roles::templates::avoidAdjacentRepeats(2));
  if (!roles::validateRules(rules).ok()) return 1;

  roles::GrammarPolicy policy(map, rules, {ChopId{1}, ChopId{2}});
  CandidateQuery q;
  q.chop = ChopId{2};
  q.recent = {ChopId{2}};  // the previous event already used this chop
  const Decision d = policy.evaluate(q);
  if (d.allowed) return 1;
  std::printf("%s\n", policy.explain(d).c_str());
  return 0;
}
