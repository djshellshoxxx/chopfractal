#pragma once
// Command palette logic (F19) and keyboard hit navigation (F04) without any UI toolkit: the fuzzy matcher, the number
// argument parser and the neighbour-hit walk. Pure and deterministic; unit tested.
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <chopfractal/plugin_ui_adapter/views.hpp>

namespace chopfractal::ui {

struct FuzzyMatch {
  int score = 0;
  std::vector<std::uint8_t> positions;  // indexes of the matched candidate characters, ascending
  bool matched = false;
};
// Case-insensitive ASCII subsequence match. Whitespace in the query splits tokens that must each match (any order).
// Score: +16 per matched character, +8 at a word start, +6 consecutive, +4 exact case, -1 per skipped character after the
// first match (capped at -12), +24 when the query is a prefix of the candidate, +40 when it equals it. An empty query matches
// everything with score 0.
FuzzyMatch fuzzyScore(std::string_view query, std::string_view candidate);
// Indexes of the candidates that match, best first (score descending, then shorter candidate, then lower index), at most `limit`.
std::vector<std::pair<int, int>> fuzzyRank(std::string_view query, const std::vector<std::string_view>& candidates, int limit);

// Parses a typed number for a parameter command. Accepts "0.6", ".5", "-3", "35%" (percent of the range when the range is 0 to 1
// is NOT applied: the number is taken as typed). Returns false and fills `error` ("0 to 1 expected.") when it does not parse or
// lies outside [min, max]; `clampInstead` clamps instead of failing for numbers that parse.
bool parseNumberArg(std::string_view text, double min, double max, double& out, std::string& error, bool clampInstead = false);
std::string rangeErrorText(double min, double max);  // "0 to 1 expected."

enum class HitDirection { Previous, Next, LaneUp, LaneDown };
// The hit to select after an arrow key. Top-level hits only, ordered by (x0, lane, id). Previous / Next never wrap (the
// current hit is returned at the ends; with no selection Next gives the first and Previous the last). LaneUp / LaneDown go to
// the nearest lane that has hits, choosing the hit closest in time (ties to the earlier). An unknown `current` counts as none.
EventId neighborHit(const PatternView&, EventId current, HitDirection);

}  // namespace chopfractal::ui
