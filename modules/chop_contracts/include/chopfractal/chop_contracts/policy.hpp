#pragma once
// Narrow interface through which an optional rule provider (e.g. chop_roles_grammar) guides
// generation without pattern_engine depending on it. The composition root injects the policy.
#include <chopfractal/chop_contracts/ids.hpp>
#include <chopfractal/chop_contracts/time.hpp>
#include <cstdint>
#include <vector>

namespace chopfractal {

enum class ScopeLevel : std::uint8_t { Phrase, Bar, Beat, Event };

struct ScopeContext {
  ScopeLevel level = ScopeLevel::Event;
  int bar = 0;
  int barCount = 1;
  int beat = 0;
  int beatsPerBar = 4;
  bool finalBeat() const { return beat == beatsPerBar - 1; }
  bool finalBar() const { return bar == barCount - 1; }
};

// Immutable summary of a candidate (or existing) event handed to the policy.
struct CandidateQuery {
  ChopId chop;
  Ticks startInBar = 0;
  ScopeContext scope;
  bool firstInBar = false;                // no earlier event exists in this bar
  std::vector<ChopId> recent;             // preceding chops in time order across the phrase (nearest last)
  std::vector<ChopId> barSoFar;           // chops already placed earlier in this bar
};

struct TraceEntry {
  enum class Effect : std::uint8_t { Rejected, Weighted, Preserved, Required, Conflict };
  std::uint32_t rule = 0;
  Effect effect = Effect::Weighted;
};

struct Decision {
  bool allowed = true;
  double weight = 1.0;
  std::uint32_t blockedBy = 0;  // rule id of the deciding hard constraint when !allowed
  std::vector<TraceEntry> trace;
};

struct Requirement {
  ChopId chop;
  std::uint32_t rule = 0;
};

class ICandidatePolicy {
 public:
  virtual ~ICandidatePolicy() = default;
  // Pure: identical input gives identical output.
  virtual Decision evaluate(const CandidateQuery& q) const = 0;
  // True when mutation must keep this existing event (e.g. "preserve the first kick in each bar").
  virtual bool isPreserved(const CandidateQuery& q) const = 0;
  // Chops that must appear at least once in the given scope (beat), given what is already placed.
  virtual std::vector<Requirement> required(const ScopeContext& scope, const std::vector<ChopId>& placedInScope) const = 0;
};

}  // namespace chopfractal
