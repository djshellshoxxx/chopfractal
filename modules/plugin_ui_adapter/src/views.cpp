#include <chopfractal/plugin_ui_adapter/views.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace chopfractal::ui {

PeakBins computePeaks(const float* const* channels, int numChannels, std::int64_t frames, SampleRange range, int bins) {
  PeakBins out;
  if (!channels || numChannels < 1 || frames <= 0 || bins < 1) return out;
  range.start = std::max<std::int64_t>(0, range.start);
  range.end = std::min(frames, range.end);
  if (range.empty()) return out;
  out.min.assign(static_cast<std::size_t>(bins), 0.f);
  out.max.assign(static_cast<std::size_t>(bins), 0.f);
  const std::int64_t len = range.length();
  for (int b = 0; b < bins; ++b) {
    std::int64_t s = range.start + len * b / bins;
    std::int64_t e = range.start + len * (b + 1) / bins;
    if (e <= s) e = std::min(range.end, s + 1);  // more bins than frames: every bin still reads a sample
    float lo = 1e30f, hi = -1e30f;
    for (std::int64_t i = s; i < e && i < frames; ++i) {
      float v = 0.f;
      for (int c = 0; c < numChannels; ++c) v += channels[c][i];
      v /= static_cast<float>(numChannels);
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
    if (lo > hi) lo = hi = 0.f;
    out.min[static_cast<std::size_t>(b)] = lo;
    out.max[static_cast<std::size_t>(b)] = hi;
  }
  return out;
}

double frameToX(const ViewWindow& w, std::int64_t frame) {
  const double span = static_cast<double>(std::max<std::int64_t>(1, w.endFrame - w.startFrame));
  return static_cast<double>(frame - w.startFrame) * w.widthPx / span;
}

std::int64_t xToFrame(const ViewWindow& w, double x) {
  const double span = static_cast<double>(std::max<std::int64_t>(1, w.endFrame - w.startFrame));
  const double width = w.widthPx > 0.0 ? w.widthPx : 1.0;
  return w.startFrame + static_cast<std::int64_t>(std::llround(x * span / width));
}

namespace {
ViewWindow clampWindow(ViewWindow w, std::int64_t total) {
  const std::int64_t minSpan = std::min<std::int64_t>(total, 16);
  std::int64_t span = std::max(minSpan, w.endFrame - w.startFrame);
  span = std::min(span, total);
  w.startFrame = std::max<std::int64_t>(0, std::min(w.startFrame, total - span));
  w.endFrame = w.startFrame + span;
  return w;
}
}  // namespace

ViewWindow zoomAround(const ViewWindow& w, std::int64_t anchorFrame, double factor, std::int64_t totalFrames) {
  if (totalFrames <= 0 || !(factor > 0.0) || !std::isfinite(factor)) return w;
  const double span = static_cast<double>(w.endFrame - w.startFrame);
  const double newSpan = std::max(1.0, span / factor);
  const double t = span > 0.0 ? static_cast<double>(anchorFrame - w.startFrame) / span : 0.5;  // anchor keeps its screen position
  ViewWindow out = w;
  out.startFrame = anchorFrame - static_cast<std::int64_t>(std::llround(t * newSpan));
  out.endFrame = out.startFrame + static_cast<std::int64_t>(std::llround(newSpan));
  return clampWindow(out, totalFrames);
}

ViewWindow scrolledBy(const ViewWindow& w, double deltaPx, std::int64_t totalFrames) {
  if (totalFrames <= 0 || !std::isfinite(deltaPx)) return w;
  const double span = static_cast<double>(w.endFrame - w.startFrame);
  const std::int64_t delta = static_cast<std::int64_t>(std::llround(deltaPx * span / (w.widthPx > 0.0 ? w.widthPx : 1.0)));
  ViewWindow out = w;
  out.startFrame += delta;
  out.endFrame += delta;
  return clampWindow(out, totalFrames);
}

std::vector<MarkerView> buildMarkerViews(const source::ChopMap& map, const ViewWindow& window) {
  std::vector<MarkerView> out;
  for (const source::Marker& m : map.markers()) {
    MarkerView v;
    v.id = m.id;
    v.x = frameToX(window, m.position);
    v.visible = m.position >= window.startFrame && m.position < window.endFrame;
    v.enabled = m.enabled;
    v.manual = m.manual;
    v.label = m.label;
    v.color = m.color;
    out.push_back(std::move(v));
  }
  return out;
}

namespace {

std::string describe(const Event& e, int bar, int beat, int indexInBeat, int depth, int chopIndex, const std::string& role) {
  std::string s = depth == 0 ? "Bar " + std::to_string(bar + 1) + ", beat " + std::to_string(beat + 1) + ", hit " + std::to_string(indexInBeat + 1)
                             : "Zoomed hit " + std::to_string(indexInBeat + 1) + " (level " + std::to_string(depth) + ")";
  s += ", chop " + (chopIndex >= 0 ? std::to_string(chopIndex + 1) : std::string("?"));
  if (!role.empty()) s += " (" + role + ")";
  s += ", level " + std::to_string(static_cast<int>(std::lround(e.tx.level * 100.f))) + "%";
  if (e.locked) s += ", locked";
  if (e.userOwned) s += ", edited";
  if (e.child) s += e.childActive ? ", zoomed in" : ", zoom hidden";
  return s;
}

void addRects(PatternView& view, const std::vector<Event>& events, const ChopSnapshot& chops, const RoleLookup& roleOf, std::int64_t windowStart,
              int bar, int beat, int depth, EventId parent) {
  const double total = static_cast<double>(view.lengthTicks);
  int index = 0;
  for (const Event& e : events) {
    EventRect r;
    r.id = e.id;
    r.parent = parent;
    r.bar = bar;
    r.beat = beat;
    r.depth = depth;
    const std::int64_t abs = windowStart + e.start;
    r.x0 = static_cast<double>(abs) / total;
    r.x1 = static_cast<double>(abs + e.duration) / total;
    for (std::size_t i = 0; i < chops.chops.size(); ++i)
      if (chops.chops[i].id == e.chop) r.chopIndex = static_cast<int>(i);
    r.level = e.tx.level;
    r.locked = e.locked;
    r.userOwned = e.userOwned;
    r.hasChild = e.child != nullptr;
    r.childActive = e.child != nullptr && e.childActive;
    r.description = describe(e, bar, beat, index, depth, r.chopIndex, roleOf ? roleOf(e.chop) : std::string());
    view.rects.push_back(std::move(r));
    if (e.child) addRects(view, e.child->events, chops, roleOf, abs, bar, beat, depth + 1, e.id);
    ++index;
  }
}

bool findPath(const std::vector<Event>& events, EventId id, std::vector<std::string>& path, const std::string& noun) {
  int i = 0;
  for (const Event& e : events) {
    ++i;
    if (e.id == id) {
      path.push_back(noun + " " + std::to_string(i));
      return true;
    }
    if (e.child) {
      path.push_back(noun + " " + std::to_string(i));
      if (findPath(e.child->events, id, path, "Child")) return true;
      path.pop_back();
    }
  }
  return false;
}

}  // namespace

PatternView buildPatternView(const pattern::Pattern& p, const ChopSnapshot& chops, const RoleLookup& roleOf) {
  PatternView v;
  v.lengthTicks = pattern::lengthTicks(p);
  if (v.lengthTicks <= 0) return v;
  const std::int64_t barT = pattern::barTicks(p.settings);
  const std::int64_t beatT = ticksPerBeat(p.settings.timeSignature);
  for (std::size_t b = 0; b < p.bars.size(); ++b) {
    v.barLines.push_back(static_cast<double>(static_cast<std::int64_t>(b) * barT) / static_cast<double>(v.lengthTicks));
    v.barLocked.push_back(p.phraseLocked || p.bars[b].locked);
    for (std::size_t t = 0; t < p.bars[b].beats.size(); ++t) {
      v.beatLines.push_back(static_cast<double>(static_cast<std::int64_t>(b) * barT + static_cast<std::int64_t>(t) * beatT) / static_cast<double>(v.lengthTicks));
      addRects(v, p.bars[b].beats[t].events, chops, roleOf, static_cast<std::int64_t>(b) * barT, static_cast<int>(b), static_cast<int>(t), 0, EventId{});
    }
  }
  return v;
}

std::vector<std::string> breadcrumbFor(const pattern::Pattern& p, EventId id) {
  std::vector<std::string> path;
  for (std::size_t b = 0; b < p.bars.size(); ++b)
    for (std::size_t t = 0; t < p.bars[b].beats.size(); ++t) {
      path = {"Phrase", "Bar " + std::to_string(b + 1), "Beat " + std::to_string(t + 1)};
      if (findPath(p.bars[b].beats[t].events, id, path, "Hit")) return path;
    }
  return {};
}

std::vector<TreeNodeView> layoutHistoryTree(const std::vector<history::NodeSummary>& nodes, history::NodeId active) {
  std::map<history::NodeId, std::vector<const history::NodeSummary*>> children;
  std::map<history::NodeId, const history::NodeSummary*> byId;
  for (const auto& n : nodes) byId[n.id] = &n;
  for (const auto& n : nodes) children[byId.count(n.parent) ? n.parent : history::kNoNode].push_back(&n);
  for (auto& kv : children)
    std::sort(kv.second.begin(), kv.second.end(), [](const history::NodeSummary* a, const history::NodeSummary* b) { return a->order < b->order; });

  std::vector<TreeNodeView> out;
  int nextColumn = 0;
  std::function<int(const history::NodeSummary&, int)> visit = [&](const history::NodeSummary& n, int depth) -> int {
    const std::size_t slot = out.size();
    TreeNodeView v;
    v.id = n.id;
    v.parent = n.parent;
    v.depth = depth;
    v.active = n.id == active;
    v.favorite = n.favorite;
    v.label = n.label;
    out.push_back(v);
    int column;
    auto it = children.find(n.id);
    if (it == children.end() || it->second.empty()) {
      column = nextColumn++;
    } else {
      column = visit(*it->second.front(), depth + 1);  // a parent sits above its first child
      for (std::size_t i = 1; i < it->second.size(); ++i) visit(*it->second[i], depth + 1);
    }
    out[slot].column = column;
    return column;
  };
  for (const history::NodeSummary* root : children[history::kNoNode]) visit(*root, 0);
  return out;
}

}  // namespace chopfractal::ui
