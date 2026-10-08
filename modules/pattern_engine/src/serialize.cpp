#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/pattern_engine/pattern.hpp>
#include <chopfractal/pattern_engine/session.hpp>

namespace chopfractal::pattern {
namespace {
constexpr std::size_t kMinBeatBytes = 8 + 2 + 4;
constexpr std::size_t kMinBarBytes = 8 + 2 + 4 + 4;
constexpr std::size_t kMinEventBytes = 68;
}  // namespace

std::vector<std::uint8_t> serialize(const Pattern& p) {
  std::vector<std::uint8_t> out;
  bytes::Writer w(out);
  const Settings& s = p.settings;
  w.u32(p.engineVersion);
  w.i32(s.bars);
  w.i32(s.timeSignature.numerator);
  w.i32(s.timeSignature.denominator);
  w.i32(s.grid.division);
  w.boolean(s.grid.triplet);
  w.f64(s.density);
  w.f64(s.variation.phrase);
  w.f64(s.variation.bar);
  w.f64(s.variation.beat);
  w.f64(s.variation.event);
  w.f64(s.swing);
  w.u64(s.seed);
  w.i32(s.maxEvents);
  w.boolean(s.allowReverse);
  w.boolean(s.allowPitch);
  w.boolean(s.allowRetrigger);
  w.i32(s.pitchRange);
  w.i32(s.maxRetrigger);
  w.i64(s.maxShiftTicks);
  w.u64(p.phraseId.value);
  w.boolean(p.phraseLocked);
  w.u64(p.nextId);
  w.u32(static_cast<std::uint32_t>(p.bars.size()));
  for (const Bar& b : p.bars) {
    w.u64(b.id.value);
    w.boolean(b.locked);
    w.boolean(b.userOwned);
    w.i32(b.copyOf);
    w.u32(static_cast<std::uint32_t>(b.beats.size()));
    for (const Beat& bt : b.beats) {
      w.u64(bt.id.value);
      w.boolean(bt.locked);
      w.boolean(bt.userOwned);
      w.u32(static_cast<std::uint32_t>(bt.events.size()));
      for (const Event& e : bt.events) writeEvent(w, e);
    }
  }
  return out;
}

Result<Pattern> deserialize(const std::uint8_t* data, std::size_t size) {
  bytes::Reader r(data, size);
  Pattern p;
  Settings& s = p.settings;
  p.engineVersion = r.u32();
  s.bars = r.i32();
  s.timeSignature.numerator = r.i32();
  s.timeSignature.denominator = r.i32();
  s.grid.division = r.i32();
  s.grid.triplet = r.boolean();
  s.density = r.f64();
  s.variation.phrase = r.f64();
  s.variation.bar = r.f64();
  s.variation.beat = r.f64();
  s.variation.event = r.f64();
  s.swing = r.f64();
  s.seed = r.u64();
  s.maxEvents = r.i32();
  s.allowReverse = r.boolean();
  s.allowPitch = r.boolean();
  s.allowRetrigger = r.boolean();
  s.pitchRange = r.i32();
  s.maxRetrigger = r.i32();
  s.maxShiftTicks = r.i64();
  p.phraseId = ScopeId{r.u64()};
  p.phraseLocked = r.boolean();
  p.nextId = r.u64();
  if (!r.ok()) return makeError(ErrorCode::Corrupt, "pattern state is truncated");
  if (p.engineVersion == 0 || p.engineVersion > kEngineVersion)
    return makeError(ErrorCode::UnsupportedVersion, "pattern was written by a newer engine", p.engineVersion);
  Status sv = validateSettings(s);
  if (!sv.ok()) return makeError(ErrorCode::Corrupt, "pattern settings are invalid: " + sv.error().message);
  const std::uint32_t barCount = r.count(static_cast<std::uint32_t>(limits::kMaxBars), kMinBarBytes);
  if (!r.ok() || static_cast<int>(barCount) != s.bars) return makeError(ErrorCode::Corrupt, "bar count is invalid");
  for (std::uint32_t bi = 0; bi < barCount; ++bi) {
    Bar b;
    b.id = ScopeId{r.u64()};
    b.locked = r.boolean();
    b.userOwned = r.boolean();
    b.copyOf = r.i32();
    const std::uint32_t beatCount = r.count(32, kMinBeatBytes);
    if (!r.ok()) return makeError(ErrorCode::Corrupt, "bar data is truncated");
    for (std::uint32_t ti = 0; ti < beatCount; ++ti) {
      Beat bt;
      bt.id = ScopeId{r.u64()};
      bt.locked = r.boolean();
      bt.userOwned = r.boolean();
      const std::uint32_t n = r.count(static_cast<std::uint32_t>(limits::kMaxEventsPerPattern), kMinEventBytes);
      if (!r.ok()) return makeError(ErrorCode::Corrupt, "beat data is truncated");
      bt.events.resize(n);
      for (std::uint32_t i = 0; i < n; ++i)
        if (!readEvent(r, bt.events[i])) return makeError(ErrorCode::Corrupt, "event data is malformed");
      b.beats.push_back(std::move(bt));
    }
    p.bars.push_back(std::move(b));
  }
  if (!r.ok() || r.remaining() != 0) return makeError(ErrorCode::Corrupt, "pattern state has trailing or missing bytes");
  Status v = validate(p);
  if (!v.ok()) return makeError(ErrorCode::Corrupt, "pattern state failed validation: " + v.error().message);
  return p;
}

// ---- PatternSession ----
Status PatternSession::commit(Result<Pattern> next) {
  if (!next.ok()) return next.error();
  history_.commit(std::make_shared<const Pattern>(std::move(next.value())));
  return {};
}

void PatternSession::adopt(Pattern p) { history_.commit(std::make_shared<const Pattern>(std::move(p))); }

void PatternSession::reset() {
  history_.reset(nullptr);
  slots_ = {};
}

void PatternSession::restore(Pattern p) {
  history_.reset(std::make_shared<const Pattern>(std::move(p)));
  slots_ = {};
}

Status PatternSession::storeSnapshot(std::size_t slot) {
  if (slot >= 2) return makeError(ErrorCode::OutOfRange, "snapshot slot must be 0 (A) or 1 (B)");
  if (!hasPattern()) return makeError(ErrorCode::InvalidArgument, "there is no pattern to store");
  slots_[slot] = history_.current();
  return {};
}

Status PatternSession::recallSnapshot(std::size_t slot) {
  if (slot >= 2) return makeError(ErrorCode::OutOfRange, "snapshot slot must be 0 (A) or 1 (B)");
  if (!slots_[slot]) return makeError(ErrorCode::NotFound, "that snapshot slot is empty");
  history_.commit(slots_[slot]);
  return {};
}

}  // namespace chopfractal::pattern
