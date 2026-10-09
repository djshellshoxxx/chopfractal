#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/fractal_rhythm/fractal.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace chopfractal::fractal {
namespace {

bool validCell(char c) { return c == 'x' || c == 'a' || c == '.'; }

struct Builder {
  const Context& ctx;
  const Settings& s;
  std::string motif;
  std::unordered_set<std::uint64_t> used;
  std::size_t leafCount = 0;
  std::size_t recent = 0;  // chops placed so far this bar (nearest last)
  std::vector<ChopId> placed;
  Status error;

  std::uint64_t newId(std::uint64_t path) {
    std::uint64_t v = kDerivedIdBit | (deriveKey(s.seed, 0xF2AC, static_cast<std::uint64_t>(ctx.bar), path) & ~kDerivedIdBit);
    if (v == kDerivedIdBit) v |= 1;
    while (!used.insert(v).second) v = kDerivedIdBit | ((v + 1) & ~kDerivedIdBit);
    return v;
  }

  std::string motifAt(int level) const {
    std::string m = motif;
    if (s.mirror && (level % 2 == 1)) std::reverse(m.begin(), m.end());
    return m;
  }

  // Picks the chop for the next leaf: source order along the rhythm, with a seeded chance of an
  // alternate, then the first candidate the policy accepts. Invalid id = the cell becomes a rest.
  ChopId pickChop(std::uint64_t path, Ticks startInBar) {
    const auto& chops = ctx.chops->chops;
    const std::size_t n = chops.size();
    Rng r(deriveKey(s.seed, 0xF2C0, static_cast<std::uint64_t>(ctx.bar), path));
    const double dAlt = r.uniform01();
    const std::uint64_t pick = r.uniform(n);
    std::size_t idx = (leafCount + static_cast<std::size_t>(ctx.bar) * 3) % n;
    if (s.mutation > 0.0 && dAlt < s.mutation * 0.5) idx = static_cast<std::size_t>(pick);
    if (!ctx.policy) return chops[idx].id;
    for (std::size_t k = 0; k < n; ++k) {
      const ChopInfo& c = chops[(idx + k) % n];
      CandidateQuery q;
      q.chop = c.id;
      q.startInBar = startInBar;
      q.scope.level = ScopeLevel::Event;
      q.scope.bar = ctx.bar;
      q.scope.barCount = ctx.barCount;
      q.scope.beatsPerBar = ctx.beatsPerBar;
      q.scope.beat = static_cast<int>(std::min<Ticks>(ctx.beatsPerBar - 1, startInBar / (ctx.barTicks / ctx.beatsPerBar)));
      q.firstInBar = placed.empty();
      q.barSoFar = placed;
      if (ctx.policy->evaluate(q).allowed) return c.id;
    }
    return ChopId{};
  }

  // Builds the events of one window. `base` is the window start relative to the bar.
  std::vector<Event> build(Ticks base, Ticks window, int level, std::uint64_t path, float levelSoFar) {
    std::vector<Event> out;
    const std::string m = motifAt(level);
    const int n = static_cast<int>(m.size());
    std::vector<char> cells(m.begin(), m.end());
    if (level > 0 && s.mutation > 0.0) {
      Rng r(deriveKey(s.seed, 0xF2F1, static_cast<std::uint64_t>(ctx.bar), path));
      const std::vector<char> original = cells;
      for (char& c : cells)
        if (r.uniform01() < s.mutation) c = (c == '.') ? 'x' : '.';
      if (std::none_of(cells.begin(), cells.end(), [](char c) { return c != '.'; })) cells = original;  // never silence a whole cell
    }
    for (int i = 0; i < n; ++i) {
      if (cells[static_cast<std::size_t>(i)] == '.') continue;
      const Ticks a = window * i / n, b = window * (i + 1) / n;
      if (b - a < kMinCellTicks) {
        error = makeError(ErrorCode::LimitExceeded, "the smallest cell would be shorter than 15 ticks; use a smaller depth", level);
        return {};
      }
      const std::uint64_t cp = path * 16 + static_cast<std::uint64_t>(i) + 1;
      const float w = cells[static_cast<std::size_t>(i)] == 'a' ? 1.f : 0.8f;
      const float lvl = levelSoFar * w;
      Event e;
      e.id = EventId{newId(cp)};
      e.start = a;
      e.duration = b - a;
      if (level == s.depth - 1) {
        e.chop = pickChop(cp, base + a);
        if (!e.chop.valid()) continue;  // policy rejected every chop: this cell rests
        e.tx.level = static_cast<float>(std::min(1.0, std::max(0.05, static_cast<double>(lvl) * std::pow(s.accentDecay, s.depth - 1))));
        placed.push_back(e.chop);
        ++leafCount;
      } else {
        auto kids = build(base + a, b - a, level + 1, cp, lvl);
        if (!error.ok()) return {};
        if (kids.empty()) continue;
        e.chop = kids.front().chop;
        for (Event& k : kids)
          if (k.chop != e.chop) k.sourceOverride = true;
        auto child = std::make_shared<NestedPattern>();
        child->seed = deriveKey(s.seed, 0xF2A1, static_cast<std::uint64_t>(ctx.bar), cp);
        child->windowDuration = e.duration;
        child->depth = static_cast<std::uint8_t>(level + 1);
        child->events = std::move(kids);
        e.child = std::move(child);
      }
      out.push_back(std::move(e));
    }
    return out;
  }
};

}  // namespace

std::size_t soundingHits(const Settings& s) {
  std::size_t active = 0;
  for (char c : s.motif)
    if (c != '.') ++active;
  std::size_t total = 1;
  for (int d = 0; d < s.depth; ++d) total *= active;
  return total;
}

Status validate(const Context& ctx, const Settings& s) {
  if (!ctx.chops || ctx.chops->chops.empty()) return makeError(ErrorCode::InvalidArgument, "there are no chops to play");
  if (ctx.barTicks <= 0 || ctx.beatsPerBar < 1 || ctx.beatsPerBar > ctx.barTicks || ctx.bar < 0 || ctx.barCount < 1 || ctx.bar >= ctx.barCount)
    return makeError(ErrorCode::InvalidArgument, "invalid bar context");
  const int len = static_cast<int>(s.motif.size());
  if (len < kMinMotif || len > kMaxMotif) return makeError(ErrorCode::OutOfRange, "the motif must have 2 to 8 cells", len);
  bool hit = false;
  for (char c : s.motif) {
    if (!validCell(c)) return makeError(ErrorCode::InvalidArgument, "the motif may only contain x, a and .");
    hit = hit || c != '.';
  }
  if (!hit) return makeError(ErrorCode::InvalidArgument, "the motif needs at least one hit");
  if (s.depth < 1 || s.depth > kMaxDepth) return makeError(ErrorCode::OutOfRange, "depth must be 1 to 3", kMaxDepth);
  if (!(s.mutation >= 0.0 && s.mutation <= 1.0)) return makeError(ErrorCode::OutOfRange, "mutation must lie in [0, 1]");
  if (!(s.accentDecay > 0.0 && s.accentDecay <= 1.0)) return makeError(ErrorCode::OutOfRange, "accent decay must lie in (0, 1]");
  return {};
}

Result<std::vector<Event>> generateBar(const Context& ctx, const Settings& s) {
  Status v = validate(ctx, s);
  if (!v.ok()) return v.error();
  // Budget: each scale multiplies the hit count by the number of active cells. Mutation can add cells, so
  // the bound uses the larger of the motif's active cells and the full cell count where flips can occur.
  if (soundingHits(s) > ctx.eventBudget) {
    std::size_t active = 0;
    for (char c : s.motif)
      if (c != '.') ++active;
    int permitted = 0;
    std::size_t total = active;
    while (permitted < s.depth && total <= ctx.eventBudget) {
      ++permitted;
      total *= active;
    }
    return makeError(ErrorCode::LimitExceeded, "too many hits for the event budget; the deepest permitted depth is " + std::to_string(permitted), permitted);
  }
  Builder b{ctx, s, s.motif, {}, 0, 0, {}, {}};
  auto events = b.build(0, ctx.barTicks, 0, 0, 1.f);
  if (!b.error.ok()) return b.error.error();
  if (b.leafCount > ctx.eventBudget)
    return makeError(ErrorCode::LimitExceeded, "mutation produced too many hits for the event budget", static_cast<std::int64_t>(b.leafCount));
  return events;
}

}  // namespace chopfractal::fractal
