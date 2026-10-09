#include <chopfractal/source_chop/chop_map.hpp>

#include <algorithm>
#include <cstdlib>

#include <chopfractal/chop_contracts/bytes.hpp>

namespace chopfractal::source {
namespace {
constexpr std::size_t kMaxLabel = 64;
constexpr std::size_t kMinMarkerBytes = 46;
}  // namespace

Result<ChopMap> ChopMap::create(SourceInfo info, std::int64_t minRegionFrames) {
  if (info.frames <= 0 || info.frames > kMaxSourceFrames) return makeError(ErrorCode::InvalidArgument, "source length is zero or exceeds the limit");
  if (info.channels < 1 || info.channels > 2) return makeError(ErrorCode::InvalidArgument, "only mono and stereo sources are supported");
  if (info.sampleRate < 8000 || info.sampleRate > 384000) return makeError(ErrorCode::InvalidArgument, "unsupported sample rate");
  if (minRegionFrames < 1 || minRegionFrames * 2 > info.frames) return makeError(ErrorCode::InvalidArgument, "invalid minimum region length");
  if (info.name.size() > 256 || info.path.size() > 1024) return makeError(ErrorCode::InvalidArgument, "name or path too long");
  return ChopMap(std::move(info), minRegionFrames);
}

const Marker* ChopMap::find(ChopId id) const {
  for (const Marker& m : markers())
    if (m.id == id) return &m;
  return nullptr;
}

Marker* ChopMap::findIn(State& s, ChopId id) const {
  for (Marker& m : s.markers)
    if (m.id == id) return &m;
  return nullptr;
}

std::vector<Region> ChopMap::regions() const {
  std::vector<const Marker*> on;
  for (const Marker& m : markers())
    if (m.enabled) on.push_back(&m);
  std::vector<Region> out;
  for (std::size_t i = 0; i < on.size(); ++i) {
    const std::int64_t end = (i + 1 < on.size()) ? on[i + 1]->position : info_.frames;
    out.push_back({on[i]->id, {on[i]->position, end}});
  }
  return out;
}

ChopSnapshotPtr ChopMap::snapshot() const {
  auto snap = std::make_shared<ChopSnapshot>();
  snap->source = info_.id;
  snap->sourceFrames = info_.frames;
  snap->sampleRate = info_.sampleRate;
  for (const Region& r : regions()) {
    const Marker* m = find(r.id);
    SampleRange range = r.range;
    const SampleRange trimmed{range.start + m->trimStart, range.end - static_cast<std::int64_t>(m->trimEnd)};
    if (!trimmed.empty()) range = trimmed;
    snap->chops.push_back({r.id, range, m->group, m->fadeIn, m->fadeOut});
  }
  return snap;
}

Status ChopMap::validate(const State& s) const {
  if (s.markers.size() > limits::kMaxChops) return makeError(ErrorCode::LimitExceeded, "too many markers", static_cast<std::int64_t>(limits::kMaxChops));
  for (std::size_t i = 0; i < s.markers.size(); ++i) {
    const Marker& m = s.markers[i];
    if (!m.id.valid()) return makeError(ErrorCode::InvalidArgument, "marker has an invalid id");
    if (m.label.size() > kMaxLabel) return makeError(ErrorCode::InvalidArgument, "marker label too long");
    if (m.position < 0 || m.position > info_.frames - minRegion_) return makeError(ErrorCode::OutOfRange, "marker lies outside the source");
    if (i > 0) {
      const Marker& p = s.markers[i - 1];
      if (m.position - p.position < minRegion_) return makeError(ErrorCode::Conflict, "markers are closer than the minimum region length");
      if (m.id == p.id) return makeError(ErrorCode::Conflict, "duplicate marker id");
    }
  }
  return {};
}

Status ChopMap::commit(State next) {
  std::sort(next.markers.begin(), next.markers.end(), [](const Marker& a, const Marker& b) { return a.position < b.position; });
  Status v = validate(next);
  if (!v.ok()) return v;
  history_.commit(std::move(next));
  return {};
}

Result<ChopId> ChopMap::addMarker(std::int64_t position, bool manual) {
  State s = history_.current();
  Marker m;
  m.id = ChopId{s.nextId++};
  m.position = position;
  m.manual = manual;
  const ChopId id = m.id;
  s.markers.push_back(std::move(m));
  Status st = commit(std::move(s));
  if (!st.ok()) return st.error();
  return id;
}

Status ChopMap::moveMarker(ChopId id, std::int64_t position) {
  State s = history_.current();
  Marker* m = findIn(s, id);
  if (!m) return makeError(ErrorCode::NotFound, "marker not found");
  m->position = position;
  // Moving may not cross a neighbour: the marker order must stay identical.
  std::vector<ChopId> before;
  for (const Marker& x : history_.current().markers) before.push_back(x.id);
  std::vector<Marker> sorted = s.markers;
  std::sort(sorted.begin(), sorted.end(), [](const Marker& a, const Marker& b) { return a.position < b.position; });
  for (std::size_t i = 0; i < sorted.size(); ++i)
    if (sorted[i].id != before[i]) return makeError(ErrorCode::Conflict, "a marker cannot be moved past its neighbour");
  return commit(std::move(s));
}

Status ChopMap::deleteMarker(ChopId id) {
  State s = history_.current();
  auto it = std::find_if(s.markers.begin(), s.markers.end(), [&](const Marker& m) { return m.id == id; });
  if (it == s.markers.end()) return makeError(ErrorCode::NotFound, "marker not found");
  s.markers.erase(it);
  return commit(std::move(s));
}

#define CHOP_EDIT_MARKER(expr)                                                  \
  State s = history_.current();                                                 \
  Marker* m = findIn(s, id);                                                    \
  if (!m) return makeError(ErrorCode::NotFound, "marker not found");            \
  expr;                                                                         \
  return commit(std::move(s))

Status ChopMap::setEnabled(ChopId id, bool enabled) { CHOP_EDIT_MARKER(m->enabled = enabled); }
Status ChopMap::setLabel(ChopId id, std::string label) { CHOP_EDIT_MARKER(m->label = std::move(label)); }
Status ChopMap::setColor(ChopId id, std::uint32_t color) { CHOP_EDIT_MARKER(m->color = color); }
Status ChopMap::setGroup(ChopId id, std::uint32_t group) { CHOP_EDIT_MARKER(m->group = group); }
Status ChopMap::setFades(ChopId id, std::uint32_t fadeIn, std::uint32_t fadeOut) {
  CHOP_EDIT_MARKER((m->fadeIn = fadeIn, m->fadeOut = fadeOut));
}

Status ChopMap::setTrim(ChopId id, std::uint32_t trimStart, std::uint32_t trimEnd) {
  const std::vector<Region> regs = regions();
  const Marker* cur = find(id);
  if (!cur) return makeError(ErrorCode::NotFound, "marker not found");
  for (const Region& r : regs) {
    if (r.id != id) continue;
    if (static_cast<std::int64_t>(trimStart) + trimEnd > r.range.length() - minRegion_)
      return makeError(ErrorCode::OutOfRange, "trim leaves less than the minimum region length");
  }
  CHOP_EDIT_MARKER((m->trimStart = trimStart, m->trimEnd = trimEnd));
}
#undef CHOP_EDIT_MARKER

Status ChopMap::clearMarkers() {
  State s = history_.current();
  s.markers.clear();
  return commit(std::move(s));
}

Result<ChopMap::State> ChopMap::computeProposal(const Proposal& proposal, MergeMode mode) const {
  State s = history_.current();
  if (mode == MergeMode::Replace) s.markers.clear();
  std::vector<ProposedMarker> sorted = proposal.markers;
  std::sort(sorted.begin(), sorted.end(), [](const ProposedMarker& a, const ProposedMarker& b) { return a.position < b.position; });
  for (const ProposedMarker& p : sorted) {
    if (s.markers.size() >= limits::kMaxChops) break;
    if (p.position < 0 || p.position > info_.frames - minRegion_) continue;
    bool clear = true;
    for (const Marker& m : s.markers)
      if (std::llabs(m.position - p.position) < minRegion_) {
        clear = false;
        break;
      }
    if (!clear) continue;
    Marker m;
    m.id = ChopId{s.nextId++};
    m.position = p.position;
    m.manual = false;
    s.markers.push_back(std::move(m));
  }
  std::sort(s.markers.begin(), s.markers.end(), [](const Marker& a, const Marker& b) { return a.position < b.position; });
  Status v = validate(s);
  if (!v.ok()) return v.error();
  return s;
}

Result<std::vector<Marker>> ChopMap::preview(const Proposal& proposal, MergeMode mode) const {
  auto s = computeProposal(proposal, mode);
  if (!s.ok()) return s.error();
  return s.value().markers;
}

Status ChopMap::applyProposal(const Proposal& proposal, MergeMode mode) {
  auto s = computeProposal(proposal, mode);
  if (!s.ok()) return s.error();
  history_.commit(std::move(s.value()));
  return {};
}

std::vector<std::uint8_t> ChopMap::serialize() const {
  std::vector<std::uint8_t> out;
  bytes::Writer w(out);
  w.u64(info_.id.value);
  w.str(info_.name);
  w.i32(info_.channels);
  w.i32(info_.sampleRate);
  w.i64(info_.frames);
  w.str(info_.path);
  w.i64(minRegion_);
  const State& s = history_.current();
  w.u64(s.nextId);
  w.u32(static_cast<std::uint32_t>(s.markers.size()));
  for (const Marker& m : s.markers) {
    w.u64(m.id.value);
    w.i64(m.position);
    w.boolean(m.enabled);
    w.boolean(m.manual);
    w.str(m.label);
    w.u32(m.color);
    w.u32(m.group);
    w.u32(m.trimStart);
    w.u32(m.trimEnd);
    w.u32(m.fadeIn);
    w.u32(m.fadeOut);
  }
  return out;
}

Result<ChopMap> ChopMap::deserialize(const std::uint8_t* data, std::size_t size) {
  bytes::Reader r(data, size);
  SourceInfo info;
  info.id = SourceId{r.u64()};
  info.name = r.str(256);
  info.channels = r.i32();
  info.sampleRate = r.i32();
  info.frames = r.i64();
  info.path = r.str(1024);
  const std::int64_t minRegion = r.i64();
  if (!r.ok()) return makeError(ErrorCode::Corrupt, "source state is malformed");
  auto map = create(std::move(info), minRegion);
  if (!map.ok()) return makeError(ErrorCode::Corrupt, "source state is invalid: " + map.error().message);
  State s;
  s.nextId = r.u64();
  if (s.nextId == 0) return makeError(ErrorCode::Corrupt, "the id counter is invalid");
  const std::uint32_t n = r.count(static_cast<std::uint32_t>(limits::kMaxChops), kMinMarkerBytes);
  if (!r.ok()) return makeError(ErrorCode::Corrupt, "marker count is invalid");
  for (std::uint32_t i = 0; i < n; ++i) {
    Marker m;
    m.id = ChopId{r.u64()};
    m.position = r.i64();
    m.enabled = r.boolean();
    m.manual = r.boolean();
    m.label = r.str(kMaxLabel);
    m.color = r.u32();
    m.group = r.u32();
    m.trimStart = r.u32();
    m.trimEnd = r.u32();
    m.fadeIn = r.u32();
    m.fadeOut = r.u32();
    if (!r.ok()) return makeError(ErrorCode::Corrupt, "marker data is truncated");
    if (m.id.value >= s.nextId) return makeError(ErrorCode::Corrupt, "marker id exceeds the id counter");
    for (const Marker& prev : s.markers)
      if (prev.id == m.id) return makeError(ErrorCode::Corrupt, "duplicate marker id");
    s.markers.push_back(std::move(m));
  }
  if (r.remaining() != 0) return makeError(ErrorCode::Corrupt, "trailing bytes in source state");
  // Positions must already be sorted and valid; a state that needs repair is rejected, not "fixed".
  Status v = map.value().validate(s);
  if (!v.ok()) return makeError(ErrorCode::Corrupt, "marker state is invalid: " + v.error().message);
  map.value().history_.reset(std::move(s));
  return map;
}

}  // namespace chopfractal::source
