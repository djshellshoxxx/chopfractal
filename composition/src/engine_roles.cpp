// Role annotation for engine v2 and the session's cached annotated snapshot.
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/chop_insight/insight.hpp>
#include <chopfractal/composition/engine_roles.hpp>
#include <chopfractal/recursive_hit_zoom/zoom.hpp>
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_set>

namespace chopfractal::composition {

DrumRole drumRoleFromRoleId(const std::string& id) {
  if (id == "kick") return DrumRole::Kick;
  if (id == "snare") return DrumRole::Snare;
  if (id == "hat") return DrumRole::Hat;
  if (id == "cymbal") return DrumRole::Cymbal;
  if (id == "percussion" || id == "accent") return DrumRole::Perc;
  return DrumRole::Other;
}

namespace {
double median(std::vector<double> v) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

struct Entry {
  DrumRole role = DrumRole::Unknown;
  bool explicitRole = false;
  bool hasFeatures = false;
  insight::ChopFeatures f;
};

DrumRole fromInsight(insight::Role r) {
  switch (r) {
    case insight::Role::Kick: return DrumRole::Kick;
    case insight::Role::Snare: return DrumRole::Snare;
    case insight::Role::Hat: return DrumRole::Hat;
    case insight::Role::Cymbal: return DrumRole::Cymbal;
    default: return DrumRole::Unknown;
  }
}
}  // namespace

ChopSnapshot annotateChops(const ChopSnapshot& raw, const render::SourceData* source, const roles::RoleMap& roles) {
  ChopSnapshot out = raw;
  std::vector<Entry> e(raw.chops.size());

  insight::SourceView view;
  const float* planes[2] = {nullptr, nullptr};
  if (source && source->frames > 0 && source->channels >= 1 && source->channels <= 2) {
    for (int c = 0; c < source->channels; ++c) planes[c] = source->channel(c);
    view = insight::SourceView{planes, source->channels, source->frames, source->sampleRate};
  }

  for (std::size_t i = 0; i < raw.chops.size(); ++i) {
    const ChopInfo& c = raw.chops[i];
    const std::string id = roles.roleOf(c.id);
    if (!id.empty()) {
      e[i].role = drumRoleFromRoleId(id);
      e[i].explicitRole = true;
    }
    if (view.channels) {
      auto f = insight::analyzeChop(view, c.range);
      if (f.ok()) {
        e[i].f = f.value();
        e[i].hasFeatures = true;
        if (!e[i].explicitRole) {
          double conf = 0.0;
          const DrumRole r = fromInsight(insight::classify(e[i].f, &conf, nullptr));
          if (conf >= 0.3) e[i].role = r;
        }
      }
    }
  }

  // Ghost notes: an automatically detected snare clearly quieter than the snares' median.
  std::vector<double> snareRms;
  for (const Entry& x : e)
    if (x.role == DrumRole::Snare && x.hasFeatures) snareRms.push_back(x.f.rms);
  if (snareRms.size() >= 2) {
    const double m = median(snareRms);
    for (Entry& x : e)
      if (x.role == DrumRole::Snare && !x.explicitRole && x.hasFeatures && x.f.rms < 0.55 * m) x.role = DrumRole::Ghost;
  }
  // Open hats: an automatically detected hat that rings at least twice as long as the others (and over 150 ms).
  std::vector<double> hatDecay;
  for (const Entry& x : e)
    if (x.role == DrumRole::Hat && x.hasFeatures) hatDecay.push_back(x.f.decayMs);
  if (hatDecay.size() >= 2) {
    const double m = median(hatDecay);
    std::size_t opened = 0, hats = 0;
    for (Entry& x : e)
      if (x.role == DrumRole::Hat && x.hasFeatures) {
        ++hats;
        if (!x.explicitRole && x.f.decayMs >= 150.0 && x.f.decayMs >= 2.0 * m) {
          x.role = DrumRole::OpenHat;
          ++opened;
        }
      }
    if (opened == hats) {  // never leave the source without a closed hat: the shortest one stays closed
      std::size_t best = e.size();
      for (std::size_t i = 0; i < e.size(); ++i)
        if (e[i].role == DrumRole::OpenHat && (best == e.size() || e[i].f.decayMs < e[best].f.decayMs)) best = i;
      if (best != e.size()) e[best].role = DrumRole::Hat;
    }
  }

  // Open hat versus crash: both are bright with a long decay. With several such chops the shorter ones are open hats;
  // a lone one is an open hat when it rings under half a second.
  std::vector<double> ringDecay;
  for (const Entry& x : e)
    if (x.role == DrumRole::Cymbal && x.hasFeatures && !x.explicitRole) ringDecay.push_back(x.f.decayMs);
  if (!ringDecay.empty()) {
    const double longest = *std::max_element(ringDecay.begin(), ringDecay.end());
    for (Entry& x : e)
      if (x.role == DrumRole::Cymbal && x.hasFeatures && !x.explicitRole) {
        const bool shorter = ringDecay.size() >= 2 ? x.f.decayMs < 0.6 * longest : x.f.decayMs < 500.0;
        if (shorter && x.f.decayMs < 700.0) x.role = DrumRole::OpenHat;
      }
  }

  // Level match inside each role group.
  std::map<int, std::vector<double>> groupRms;
  auto groupOf = [](DrumRole r) { return static_cast<int>(r); };  // open hats ring longer, so they match only among themselves
  for (const Entry& x : e)
    if (x.hasFeatures && x.f.rms > 1e-6 && x.role != DrumRole::Unknown && x.role != DrumRole::Other) groupRms[groupOf(x.role)].push_back(x.f.rms);

  for (std::size_t i = 0; i < raw.chops.size(); ++i) {
    ChopInfo& c = out.chops[i];
    c.role = e[i].role;
    c.gainTrim = 1.f;
    if (!e[i].hasFeatures || e[i].f.rms <= 1e-6) continue;
    auto it = groupRms.find(groupOf(e[i].role));
    if (it == groupRms.end() || it->second.size() < 2) continue;
    double trim = median(it->second) / e[i].f.rms;
    trim = std::min(1.4, std::max(0.7, trim));
    if (e[i].f.peak > 1e-6) trim = std::min(trim, 1.0 / e[i].f.peak);  // the trimmed chop never clips
    c.gainTrim = static_cast<float>(trim);
  }
  return out;
}

std::vector<Event> overlayAnchors(std::vector<Event> events, const ChopSnapshot& annotated, Ticks barTicks, std::uint64_t seed, int bar) {
  if (barTicks != 3840) return events;
  auto firstOf = [&](DrumRole r) -> const ChopInfo* {
    for (const ChopInfo& c : annotated.chops)
      if (c.role == r) return &c;
    return nullptr;
  };
  const ChopInfo* kick = firstOf(DrumRole::Kick);
  const ChopInfo* snare = firstOf(DrumRole::Snare);
  struct Target { Ticks at; const ChopInfo* chop; int velocity; };
  const Target targets[3] = {{0, kick, 118}, {960, snare, 110}, {2880, snare, 112}};
  std::unordered_set<std::uint64_t> used;
  for (const Event& e : events) used.insert(e.id.value);
  for (int k = 0; k < 3; ++k) {
    const Target& t = targets[k];
    if (!t.chop) continue;
    std::size_t hit = events.size();
    for (std::size_t i = 0; i < events.size(); ++i)
      if (events[i].start >= t.at - 120 && events[i].start < t.at + 120 && (hit == events.size() || std::llabs(events[i].start - t.at) < std::llabs(events[hit].start - t.at))) hit = i;
    Event a;
    if (hit != events.size()) a = events[hit];
    else {
      std::uint64_t v = kDerivedIdBit | (deriveKey(seed, 0xA2C4, static_cast<std::uint64_t>(bar), static_cast<std::uint64_t>(k)) & ~kDerivedIdBit);
      while (!used.insert(v).second) v = kDerivedIdBit | ((v + 1) & ~kDerivedIdBit);
      a.id = EventId{v};
      a.duration = 480;
    }
    a.chop = t.chop->id;
    a.region = SampleRange{};
    a.start = t.at;
    a.child.reset();
    a.childActive = true;
    a.tx = EventTransform{};
    a.tx.level = std::min(limits::kMaxLevel, static_cast<float>(t.velocity) / 127.f * t.chop->gainTrim);
    a.fx = EventFx{};
    if (hit != events.size()) events[hit] = a;
    else events.push_back(a);
  }
  std::stable_sort(events.begin(), events.end(), [](const Event& x, const Event& y) { return x.start < y.start; });
  return events;
}

std::shared_ptr<const NestedPattern> keepTransientFirst(const std::shared_ptr<const NestedPattern>& child, const Event& parent, std::uint64_t seed, std::size_t budget) {
  if (!child) return child;
  auto out = std::make_shared<NestedPattern>(*child);
  std::size_t first = out->events.size();
  for (std::size_t i = 0; i < out->events.size(); ++i)
    if (out->events[i].start == 0 && !out->events[i].locked) first = i;
  Event e;
  if (first != out->events.size()) e = out->events[first];
  else {
    if (out->events.size() + 1 > budget) return child;
    std::unordered_set<std::uint64_t> used;
    for (const Event& x : out->events) used.insert(x.id.value);
    std::uint64_t v = kDerivedIdBit | (deriveKey(seed, 0xA2C5, parent.id.value, 0) & ~kDerivedIdBit);
    while (!used.insert(v).second) v = kDerivedIdBit | ((v + 1) & ~kDerivedIdBit);
    e.id = EventId{v};
    e.duration = std::max<Ticks>(zoom::kMinChildTicks, out->windowDuration / 4);
    e.start = 0;
  }
  e.chop = parent.chop;
  e.region = parent.region;
  e.enabled = true;
  e.probability = 1.f;
  e.tx = parent.tx;
  e.tx.retrigger = 1;
  e.tx.reverse = false;
  e.fx = EventFx{};
  if (first != out->events.size()) out->events[first] = e;
  else out->events.insert(out->events.begin(), e);
  return out;
}

// Cache key: chop layout, explicit roles and the audio identity. Cheap enough to recompute on every call.
ChopSnapshotPtr ProjectSession::annotatedChops() const {
  if (!chops_) return nullptr;
  std::uint64_t key = mix64(static_cast<std::uint64_t>(chops_->chops.size()) + 0x51ED);
  for (const ChopInfo& c : chops_->chops) {
    key = hashCombine(key, c.id.value);
    key = hashCombine(key, static_cast<std::uint64_t>(c.range.start));
    key = hashCombine(key, static_cast<std::uint64_t>(c.range.end));
  }
  for (const auto& kv : roles_.all()) {
    key = hashCombine(key, kv.first);
    for (char ch : kv.second) key = hashCombine(key, static_cast<std::uint64_t>(static_cast<unsigned char>(ch)));
  }
  key = hashCombine(key, reinterpret_cast<std::uintptr_t>(sourceData_.get()));
  if (!annotated_ || annotatedKey_ != key) {
    annotated_ = std::make_shared<const ChopSnapshot>(annotateChops(*chops_, sourceData_.get(), roles_));
    annotatedKey_ = key;
  }
  return annotated_;
}

}  // namespace chopfractal::composition
