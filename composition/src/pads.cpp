// Live pads: note -> chop map, pad settings, and merging a recorded take into the pattern
// (docs/specs/roadmap/F07-performance-pads.md). Message thread only.
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cmath>
#include <set>

namespace chopfractal::composition {

void ProjectSession::publishPadBank() {
  if (!padMailbox_) return;
  auto bank = std::make_shared<render::PadBank>();
  if (chops_) {
    for (std::size_t i = 0; i < chops_->chops.size(); ++i) {
      const int note = static_cast<int>(padSettings_.baseNote) + static_cast<int>(i);
      if (note > 127) break;
      const ChopInfo& c = chops_->chops[i];
      render::PadSlot& slot = bank->slots[static_cast<std::size_t>(note)];
      slot.region = c.range;
      slot.fadeInFrames = c.fadeInFrames;
      slot.fadeOutFrames = c.fadeOutFrames;
      const auto it = padSettings_.chokeOverride.find(c.id.value);
      slot.chokeGroup = it != padSettings_.chokeOverride.end() ? it->second : (roles_.roleOf(c.id) == "hat" ? 1 : 0);
    }
  }
  padMailbox_->publish(std::move(bank));
}

Status ProjectSession::setPadSettings(const PadSettings& s) {
  if (s.baseNote > 111) return makeError(ErrorCode::OutOfRange, "the base note must be 0 to 111", 111);
  if (s.midiChannel > 16) return makeError(ErrorCode::OutOfRange, "the MIDI channel must be 0 (omni) to 16", 16);
  if (!(s.quantizeDivision == 0 || s.quantizeDivision == 4 || s.quantizeDivision == 8 || s.quantizeDivision == 16 || s.quantizeDivision == 32))
    return makeError(ErrorCode::OutOfRange, "the quantize division must be off, 4, 8, 16 or 32");
  if (s.merge > 1) return makeError(ErrorCode::OutOfRange, "the merge mode must be add or replace");
  for (const auto& kv : s.chokeOverride)
    if (kv.second > 4) return makeError(ErrorCode::OutOfRange, "choke groups are 0 (off) to 4", 4);
  padSettings_ = s;
  publishPadBank();
  return {};
}

Status ProjectSession::setChokeGroup(ChopId chop, std::uint8_t group) {
  if (group > 4) return makeError(ErrorCode::OutOfRange, "choke groups are 0 (off) to 4", 4);
  if (!chopMap_ || !chopMap_->find(chop)) return makeError(ErrorCode::NotFound, "unknown chop");
  padSettings_.chokeOverride[chop.value] = group;
  publishPadBank();
  return {};
}

Result<TakeReport> ProjectSession::mergeTake(const std::vector<TakeHit>& hitsIn, const TakeOptions& o) {
  if (!patterns_.hasPattern() || !chops_) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  if (morphActive_ || sequencer_.running() || startPending_ != 0) return makeError(ErrorCode::Conflict, "Stop scenes or cancel the morph before recording.");
  TakeReport report;
  if (hitsIn.empty()) return report;
  std::vector<TakeHit> hits = hitsIn;
  std::stable_sort(hits.begin(), hits.end(), [](const TakeHit& a, const TakeHit& b) { return a.ppqOn != b.ppqOn ? a.ppqOn < b.ppqOn : a.note < b.note; });

  pattern::Pattern p = patterns_.current();
  const Ticks L = pattern::lengthTicks(p);
  const Ticks barT = pattern::barTicks(p.settings);
  const bool quantize = o.quantizeDivision > 0;
  const Ticks g = quantize ? gridTicks(Grid{o.quantizeDivision, o.triplet}) : 0;
  const Ticks patternGrid = gridTicks(p.settings.grid);
  std::set<std::pair<int, Ticks>> cleared;
  bool capped = false;
  for (const TakeHit& h : hits) {
    const int idx = static_cast<int>(h.note) - o.baseNote;
    if (idx < 0 || idx >= static_cast<int>(chops_->chops.size())) {
      ++report.skippedUnmapped;
      continue;
    }
    if (capped) {
      ++report.skippedCap;
      continue;
    }
    const ChopId chop = chops_->chops[static_cast<std::size_t>(idx)].id;
    double t = std::fmod(h.ppqOn * static_cast<double>(kTicksPerQuarter), static_cast<double>(L));
    if (t < 0.0) t += static_cast<double>(L);
    Ticks q = quantize ? static_cast<Ticks>(std::floor(t / static_cast<double>(g) + 0.5)) * g : static_cast<Ticks>(std::llround(t));
    if (q >= L) q -= L;
    const int bar = static_cast<int>(q / barT);
    const Ticks start = q % barT;
    Ticks duration;
    if (o.mode == render::PadMode::Gate && h.ppqOff >= 0.0) {
      duration = static_cast<Ticks>(std::llround((h.ppqOff - h.ppqOn) * static_cast<double>(kTicksPerQuarter)));
      if (quantize) duration = std::max<Ticks>(g, ((duration + g - 1) / g) * g);
      else duration = std::max<Ticks>(30, duration);
    } else {
      duration = quantize ? g : patternGrid;
    }
    duration = std::min(duration, barT - start);
    if (duration <= 0) {
      ++report.skippedUnmapped;
      continue;
    }
    if (o.merge == 0) {  // add: skip a duplicate of the same chop at the same position
      bool dup = false;
      for (const pattern::Beat& bt : p.bars[static_cast<std::size_t>(bar)].beats)
        for (const Event& e : bt.events) dup = dup || (e.chop == chop && e.start == start);
      if (dup) {
        ++report.skippedDuplicate;
        continue;
      }
    } else if (cleared.insert({bar, start}).second) {  // replace: clear the unlocked events of this slot once
      const Ticks width = quantize ? g : 1;
      std::vector<EventId> doomed;
      for (const pattern::Beat& bt : p.bars[static_cast<std::size_t>(bar)].beats)
        for (const Event& e : bt.events)
          if (e.start >= start && e.start < start + width && !pattern::isLocked(p, e.id)) doomed.push_back(e.id);
      for (EventId id : doomed) {
        auto r = pattern::deleteEvent(p, id);
        if (r.ok()) {
          p = std::move(r.value());
          ++report.replaced;
        }
      }
    }
    auto added = pattern::addEvent(p, bar, chop, start, duration, *chops_);
    if (!added.ok()) {
      if (added.error().code == ErrorCode::Blocked) ++report.skippedLocked;
      else if (added.error().code == ErrorCode::LimitExceeded) {
        ++report.skippedCap;
        capped = true;
      } else {
        ++report.skippedUnmapped;
      }
      continue;
    }
    const EventId newId{added.value().nextId - 1};
    EventTransform tx;
    tx.level = render::velocityGain(o.curve, h.velocity);
    auto withTx = pattern::setEventTransform(added.value(), newId, tx);
    p = withTx.ok() ? std::move(withTx.value()) : std::move(added.value());
    ++report.added;
  }
  if (report.added == 0 && report.replaced == 0) return report;
  Status st = edit([&](const pattern::Pattern&, const ChopSnapshot&) { return Result<pattern::Pattern>(p); });
  if (!st.ok()) return st.error();
  return report;
}

}  // namespace chopfractal::composition
