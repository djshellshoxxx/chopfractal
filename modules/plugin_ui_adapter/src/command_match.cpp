#include "chopfractal/plugin_ui_adapter/command_match.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace chopfractal::ui {

namespace {
char fold(char c) { return static_cast<unsigned char>(c) < 128 ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : c; }
bool wordStart(std::string_view s, std::size_t i) { return i == 0 || s[i - 1] == ' ' || s[i - 1] == '_' || s[i - 1] == '-' || s[i - 1] == '/'; }

FuzzyMatch matchToken(std::string_view q, std::string_view c) {
  FuzzyMatch m;
  if (q.empty()) {
    m.matched = true;
    return m;
  }
  std::size_t qi = 0;
  int score = 0, skipped = 0;
  bool consecutive = false;
  bool started = false;
  for (std::size_t ci = 0; ci < c.size() && qi < q.size(); ++ci) {
    if (fold(c[ci]) == fold(q[qi])) {
      score += 16;
      if (wordStart(c, ci)) score += 8;
      if (consecutive) score += 6;
      if (c[ci] == q[qi]) score += 4;
      m.positions.push_back(static_cast<std::uint8_t>(std::min<std::size_t>(ci, 255)));
      consecutive = true;
      started = true;
      ++qi;
    } else {
      consecutive = false;
      if (started) ++skipped;
    }
  }
  if (qi < q.size()) return m;
  score -= std::min(skipped, 12);
  if (q.size() <= c.size()) {
    bool prefix = true;
    for (std::size_t i = 0; i < q.size(); ++i)
      if (fold(q[i]) != fold(c[i])) prefix = false;
    if (prefix) score += (q.size() == c.size()) ? 40 : 24;
  }
  m.score = score;
  m.matched = true;
  return m;
}

std::vector<std::string_view> tokens(std::string_view q) {
  std::vector<std::string_view> t;
  std::size_t i = 0;
  while (i < q.size()) {
    while (i < q.size() && std::isspace(static_cast<unsigned char>(q[i]))) ++i;
    const std::size_t s = i;
    while (i < q.size() && !std::isspace(static_cast<unsigned char>(q[i]))) ++i;
    if (i > s) t.push_back(q.substr(s, i - s));
  }
  return t;
}
}  // namespace

FuzzyMatch fuzzyScore(std::string_view query, std::string_view candidate) {
  const auto toks = tokens(query);
  FuzzyMatch total;
  total.matched = true;
  for (const auto t : toks) {
    FuzzyMatch m = matchToken(t, candidate);
    if (!m.matched) return FuzzyMatch{};
    total.score += m.score;
    total.positions.insert(total.positions.end(), m.positions.begin(), m.positions.end());
  }
  std::sort(total.positions.begin(), total.positions.end());
  total.positions.erase(std::unique(total.positions.begin(), total.positions.end()), total.positions.end());
  return total;
}

std::vector<std::pair<int, int>> fuzzyRank(std::string_view query, const std::vector<std::string_view>& candidates, int limit) {
  struct Hit {
    int score, length, index;
  };
  std::vector<Hit> hits;
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const FuzzyMatch m = fuzzyScore(query, candidates[i]);
    if (m.matched) hits.push_back({m.score, static_cast<int>(candidates[i].size()), static_cast<int>(i)});
  }
  std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
    if (a.score != b.score) return a.score > b.score;
    if (a.length != b.length) return a.length < b.length;
    return a.index < b.index;
  });
  std::vector<std::pair<int, int>> out;
  for (const auto& h : hits) {
    if (static_cast<int>(out.size()) >= limit) break;
    out.emplace_back(h.index, h.score);
  }
  return out;
}

std::string rangeErrorText(double min, double max) {
  auto num = [](double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%g", v);
    return std::string(b);
  };
  return num(min) + " to " + num(max) + " expected.";
}

bool parseNumberArg(std::string_view text, double min, double max, double& out, std::string& error, bool clampInstead) {
  std::string s(text);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  std::size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  s = s.substr(b);
  if (s.empty()) {
    error = rangeErrorText(min, max);
    return false;
  }
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0' || !std::isfinite(v)) {
    error = rangeErrorText(min, max);
    return false;
  }
  if (v < min || v > max) {
    if (!clampInstead) {
      error = rangeErrorText(min, max);
      return false;
    }
    out = std::min(max, std::max(min, v));
    return true;
  }
  out = v;
  return true;
}

EventId neighborHit(const PatternView& view, EventId current, HitDirection dir) {
  std::vector<const EventRect*> hits;
  for (const auto& r : view.rects)
    if (r.depth == 0) hits.push_back(&r);
  if (hits.empty()) return current;
  std::sort(hits.begin(), hits.end(), [](const EventRect* a, const EventRect* b) {
    if (a->x0 != b->x0) return a->x0 < b->x0;
    if (a->chopIndex != b->chopIndex) return a->chopIndex < b->chopIndex;
    return a->id.value < b->id.value;
  });
  int cur = -1;
  for (std::size_t i = 0; i < hits.size(); ++i)
    if (hits[i]->id == current) cur = static_cast<int>(i);
  if (dir == HitDirection::Next) return cur < 0 ? hits.front()->id : hits[static_cast<std::size_t>(std::min<int>(cur + 1, static_cast<int>(hits.size()) - 1))]->id;
  if (dir == HitDirection::Previous) return cur < 0 ? hits.back()->id : hits[static_cast<std::size_t>(std::max(cur - 1, 0))]->id;
  if (cur < 0) return hits.front()->id;
  const int lane = hits[static_cast<std::size_t>(cur)]->chopIndex;
  const double x = hits[static_cast<std::size_t>(cur)]->x0;
  const int step = dir == HitDirection::LaneUp ? -1 : 1;
  int bestLane = -1;
  for (const auto* h : hits) {
    const int d = (h->chopIndex - lane) * step;
    if (d <= 0) continue;
    if (bestLane < 0 || d < (bestLane - lane) * step) bestLane = h->chopIndex;
  }
  if (bestLane < 0) return current;
  const EventRect* best = nullptr;
  for (const auto* h : hits)
    if (h->chopIndex == bestLane && (!best || std::fabs(h->x0 - x) < std::fabs(best->x0 - x))) best = h;
  return best ? best->id : current;
}

}  // namespace chopfractal::ui
