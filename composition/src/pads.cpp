// Performance pads (F07): the note-to-chop bank, the project-level pad settings (state section tag 10) and the merge of
// a recorded take into the pattern. The voices themselves live in the renderer.
#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace chopfractal::composition {

namespace {
constexpr std::uint16_t kPadsSchema = 1;
constexpr std::uint32_t kMaxChokeOverrides = 256;

bool validDivision(int d) { return d == 0 || d == 4 || d == 8 || d == 16 || d == 32; }

Status validate(const PadSettings& s) {
  if (s.baseNote > 111) return makeError(ErrorCode::OutOfRange, "the base note must be 0 to 111");
  if (s.midiChannel > 16) return makeError(ErrorCode::OutOfRange, "the MIDI channel must be 0 (omni) or 1 to 16");
  if (!validDivision(s.quantizeDivision)) return makeError(ErrorCode::OutOfRange, "the pad quantize division must be off, 4, 8, 16 or 32");
  if (s.merge > 1) return makeError(ErrorCode::OutOfRange, "the pad merge mode must be add or replace");
  if (s.chokeOverride.size() > kMaxChokeOverrides) return makeError(ErrorCode::LimitExceeded, "too many choke group overrides", kMaxChokeOverrides);
  for (const auto& kv : s.chokeOverride)
    if (kv.second > 4) return makeError(ErrorCode::OutOfRange, "a choke group is 0 (off) or 1 to 4");
  return {};
}

std::vector<std::uint8_t> encode(const PadSettings& s) {
  std::vector<std::uint8_t> out;
  bytes::Writer w(out);
  w.u8(s.baseNote);
  w.u8(s.midiChannel);
  w.u8(s.quantizeDivision);
  w.boolean(s.quantizeTriplet);
  w.u8(s.merge);
  w.u32(static_cast<std::uint32_t>(s.chokeOverride.size()));
  for (const auto& kv : s.chokeOverride) {
    w.u64(kv.first);
    w.u8(kv.second);
  }
  return out;
}

Result<PadSettings> decode(const std::vector<std::uint8_t>& in) {
  bytes::Reader r(in.data(), in.size());
  PadSettings s;
  s.baseNote = r.u8();
  s.midiChannel = r.u8();
  s.quantizeDivision = r.u8();
  s.quantizeTriplet = r.boolean();
  s.merge = r.u8();
  const std::uint32_t n = r.u32();
  if (!r.ok() || n > kMaxChokeOverrides || r.remaining() != static_cast<std::size_t>(n) * 9)
    return makeError(ErrorCode::Corrupt, "the pads section is not valid");
  for (std::uint32_t i = 0; i < n; ++i) {
    const std::uint64_t chop = r.u64();
    const std::uint8_t group = r.u8();
    s.chokeOverride[chop] = group;
  }
  if (!r.ok() || s.chokeOverride.size() != n) return makeError(ErrorCode::Corrupt, "the pads section is not valid");
  Status st = validate(s);
  if (!st.ok()) return makeError(ErrorCode::Corrupt, "the pads section is not valid");
  return s;
}

Status storeSection(ProjectSession& session, const PadSettings& s) {
  StateSection sec;
  sec.tag = kTagPads;
  sec.schema = kPadsSchema;
  if (!(s == PadSettings{})) sec.bytes = encode(s);
  return session.setSection(std::move(sec));
}

// Every top-level event of a bar, in any beat.
template <class Fn>
void forBarEvents(const pattern::Pattern& p, int bar, Fn fn) {
  for (const pattern::Beat& beat : p.bars[static_cast<std::size_t>(bar)].beats)
    for (const Event& e : beat.events) fn(e);
}
}  // namespace

void ProjectSession::attachPadMailbox(render::Mailbox<render::PadBank>* mailbox) {
  padMailbox_ = mailbox;
  if (padMailbox_ && padBank_) padMailbox_->publish(padBank_);
}

// Chop i (snapshot order) plays on note baseNote + i. Rebuilt from the playback publish, so a marker edit, a role change
// or a settings change always reaches the audio thread.
void ProjectSession::publishPadBank() {
  auto bank = std::make_shared<render::PadBank>();
  if (chops_) {
    const ChopSnapshotPtr view = annotatedChops();
    const ChopSnapshot& snap = view ? *view : *chops_;
    for (std::size_t i = 0; i < snap.chops.size(); ++i) {
      const int note = static_cast<int>(padSettings_.baseNote) + static_cast<int>(i);
      if (note >= render::kPadNotes) break;
      const ChopInfo& c = snap.chops[i];
      render::PadSlot& slot = bank->slots[static_cast<std::size_t>(note)];
      slot.region = c.range;
      slot.fadeInFrames = c.fadeInFrames;
      slot.fadeOutFrames = c.fadeOutFrames;
      auto it = padSettings_.chokeOverride.find(c.id.value);
      slot.chokeGroup = it != padSettings_.chokeOverride.end() ? it->second : (c.role == DrumRole::Hat ? 1 : 0);
    }
  }
  padBank_ = std::move(bank);
  if (padMailbox_) padMailbox_->publish(padBank_);
}

Status ProjectSession::decodePads(const StateSection& s) {
  if (s.schema != kPadsSchema) return makeError(ErrorCode::Corrupt, "unknown pads section schema");
  auto r = decode(s.bytes);
  if (!r.ok()) return r.error();
  padSettings_ = r.value();
  publishPadBank();
  return {};
}

Status ProjectSession::setPadSettings(const PadSettings& settings) {
  Status st = validate(settings);
  if (!st.ok()) return st;
  if (settings == padSettings_) return {};
  const PadSettings previous = padSettings_;
  padSettings_ = settings;
  st = storeSection(*this, padSettings_);
  if (!st.ok()) {
    padSettings_ = previous;
    return st;
  }
  publishPadBank();
  return {};
}

Status ProjectSession::setChokeGroup(ChopId chop, std::uint8_t group) {
  if (group > 4) return makeError(ErrorCode::OutOfRange, "a choke group is 0 (off) or 1 to 4");
  if (!chops_ || !chops_->find(chop)) return makeError(ErrorCode::NotFound, "unknown chop");
  PadSettings next = padSettings_;
  const ChopSnapshotPtr view = annotatedChops();
  const ChopInfo* info = view ? view->find(chop) : nullptr;
  const std::uint8_t dflt = info && info->role == DrumRole::Hat ? 1 : 0;
  if (group == dflt) next.chokeOverride.erase(chop.value);
  else next.chokeOverride[chop.value] = group;
  return setPadSettings(next);
}

Result<TakeReport> ProjectSession::mergeTake(const std::vector<TakeHit>& hitsIn, const TakeOptions& opt) {
  if (!patterns_.hasPattern() || !chops_) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  if (!validDivision(opt.quantizeDivision)) return makeError(ErrorCode::OutOfRange, "the quantize division must be off, 4, 8, 16 or 32");

  std::vector<TakeHit> hits = hitsIn;
  std::stable_sort(hits.begin(), hits.end(), [](const TakeHit& a, const TakeHit& b) {
    return a.ppqOn != b.ppqOn ? a.ppqOn < b.ppqOn : a.note < b.note;
  });

  const ChopSnapshot& chops = *chops_;
  pattern::Pattern work = patterns_.current();
  const Ticks length = pattern::lengthTicks(work);
  const Ticks barT = pattern::barTicks(work.settings);
  if (length <= 0 || barT <= 0 || work.bars.empty()) return makeError(ErrorCode::InvalidArgument, "the pattern has no length");
  const bool quantize = opt.quantizeDivision != 0;
  const Ticks g = quantize ? gridTicks(Grid{opt.quantizeDivision, opt.triplet}) : 0;
  const Ticks slotLen = quantize ? g : gridTicks(work.settings.grid);

  TakeReport report;
  bool capHit = false;
  bool changed = false;
  std::set<std::pair<int, Ticks>> clearedSlots;

  for (const TakeHit& hit : hits) {
    const long long idx = static_cast<long long>(hit.note) - opt.baseNote;
    if (idx < 0 || idx >= static_cast<long long>(chops.chops.size()) || !std::isfinite(hit.ppqOn)) {
      ++report.skippedUnmapped;
      continue;
    }
    const ChopId chop = chops.chops[static_cast<std::size_t>(idx)].id;

    double t = std::fmod(hit.ppqOn * static_cast<double>(kTicksPerQuarter), static_cast<double>(length));
    if (t < 0) t += static_cast<double>(length);
    Ticks q;
    if (quantize) {
      q = static_cast<Ticks>(std::floor(t / static_cast<double>(g) + 0.5)) * g;
      if (q >= length) q -= length;
    } else {
      q = static_cast<Ticks>(std::llround(t));
      if (q >= length) q -= length;
    }
    const int bar = static_cast<int>(q / barT);
    const Ticks start = q % barT;

    Ticks dur;
    if (opt.mode == render::PadMode::Gate && hit.ppqOff >= 0.0 && hit.ppqOff >= hit.ppqOn) {
      dur = static_cast<Ticks>(std::llround((hit.ppqOff - hit.ppqOn) * static_cast<double>(kTicksPerQuarter)));
      if (quantize) {
        dur = std::max<Ticks>(g, ((dur + g - 1) / g) * g);
      } else {
        dur = std::max<Ticks>(30, dur);
      }
    } else {
      dur = quantize ? g : gridTicks(work.settings.grid);
    }
    dur = std::min(dur, barT - start);
    if (dur <= 0) {
      ++report.skippedUnmapped;
      continue;
    }

    if (capHit) {
      ++report.skippedCap;
      continue;
    }

    if (opt.merge == 0) {
      bool duplicate = false;
      forBarEvents(work, bar, [&](const Event& e) { duplicate = duplicate || (e.chop == chop && e.start == start); });
      if (duplicate) {
        ++report.skippedDuplicate;
        continue;
      }
    } else if (clearedSlots.insert({bar, start}).second) {
      std::vector<EventId> doomed;
      forBarEvents(work, bar, [&](const Event& e) {
        if (e.start >= start && e.start < start + slotLen && !pattern::isLocked(work, e.id)) doomed.push_back(e.id);
      });
      for (EventId id : doomed) {
        auto r = pattern::deleteEvent(work, id);
        if (!r.ok()) continue;
        work = std::move(r.value());
        ++report.replaced;
        changed = true;
      }
    }

    auto added = pattern::addEvent(work, bar, chop, start, dur, chops);
    if (!added.ok()) {
      if (added.error().code == ErrorCode::Blocked) ++report.skippedLocked;
      else if (added.error().code == ErrorCode::LimitExceeded) {
        ++report.skippedCap;
        capHit = true;
      } else {
        return added.error();
      }
      continue;
    }
    pattern::Pattern next = std::move(added.value());
    const EventId id{next.nextId - 1};
    const Event* e = pattern::findEvent(next, id);
    EventTransform tx = e ? e->tx : EventTransform{};
    tx.level = render::velocityGain(opt.curve, hit.velocity);
    auto withTx = pattern::setEventTransform(next, id, tx);
    if (!withTx.ok()) return withTx.error();
    work = std::move(withTx.value());
    ++report.added;
    changed = true;
  }

  if (changed) {
    Status st = edit([&](const pattern::Pattern&, const ChopSnapshot&) -> Result<pattern::Pattern> { return work; });
    if (!st.ok()) return st.error();
  }
  return report;
}

}  // namespace chopfractal::composition
