// F18 arrangement model: totals, validation, tag 16 encoding and the fill helpers (see arrangement.hpp).
#include <chopfractal/composition/arrangement.hpp>

#include <chopfractal/chop_contracts/bytes.hpp>

#include <algorithm>
#include <iterator>
#include <set>

namespace chopfractal::composition {

namespace {
constexpr int kBarChoices[] = {1, 2, 4, 8};

bool isBarChoice(int b) { return b == 1 || b == 2 || b == 4 || b == 8; }
}  // namespace

int Arrangement::totalBars() const {
  int total = 0;
  for (const ArrangementSection& s : sections) total += static_cast<int>(s.bars) * static_cast<int>(s.repeats);
  return total;
}

bool Arrangement::valid() const {
  if (sections.empty() || sections.size() > kMaxArrangementSections) return false;
  const int t = totalBars();
  return t >= kMinArrangementBars && t <= kMaxArrangementBars;
}

bool Arrangement::isDefault() const { return *this == Arrangement{}; }

Status validateStored(const Arrangement& a) {
  if (a.sections.size() > kMaxArrangementSections) return makeError(ErrorCode::LimitExceeded, "An arrangement has at most 32 sections.", static_cast<std::int64_t>(kMaxArrangementSections));
  for (const ArrangementSection& s : a.sections) {
    if (!isBarChoice(s.bars)) return makeError(ErrorCode::OutOfRange, "A section is 1, 2, 4 or 8 bars long.");
    if (s.repeats < 1 || s.repeats > kMaxSectionRepeats) return makeError(ErrorCode::OutOfRange, "A section repeats 1 to 8 times.", kMaxSectionRepeats);
  }
  if (static_cast<std::uint8_t>(a.transition) > static_cast<std::uint8_t>(Transition::Crossfade50)) return makeError(ErrorCode::OutOfRange, "Unknown section transition.");
  return {};
}

std::vector<std::uint8_t> serialize(const Arrangement& a) {
  std::vector<std::uint8_t> out;
  if (a.isDefault()) return out;
  bytes::Writer w(out);
  w.u8(1);
  w.u8(static_cast<std::uint8_t>((a.fadeOut ? 1 : 0) | (a.tail ? 2 : 0) | (a.writeMidi ? 4 : 0) | (a.writeMarkers ? 8 : 0)));
  w.u8(static_cast<std::uint8_t>(a.transition));
  w.u8(static_cast<std::uint8_t>(a.sections.size()));
  for (const ArrangementSection& s : a.sections) {
    w.u32(s.sceneId);
    w.u8(s.bars);
    w.u8(s.repeats);
  }
  return out;
}

Result<Arrangement> deserialize(const std::uint8_t* data, std::size_t size) {
  bytes::Reader r(data, size);
  if (r.u8() != 1) return makeError(ErrorCode::Corrupt, "unknown arrangement version");
  const std::uint8_t flags = r.u8();
  const std::uint8_t transition = r.u8();
  const std::uint8_t count = r.u8();
  if (!r.ok() || (flags & ~0x0Fu) != 0 || transition > static_cast<std::uint8_t>(Transition::Crossfade50) || count > kMaxArrangementSections)
    return makeError(ErrorCode::Corrupt, "invalid arrangement header");
  Arrangement a;
  a.fadeOut = (flags & 1) != 0;
  a.tail = (flags & 2) != 0;
  a.writeMidi = (flags & 4) != 0;
  a.writeMarkers = (flags & 8) != 0;
  a.transition = static_cast<Transition>(transition);
  if (static_cast<std::size_t>(count) * 6 != r.remaining()) return makeError(ErrorCode::Corrupt, "arrangement length does not match its count");
  for (std::uint8_t i = 0; i < count; ++i) {
    ArrangementSection s;
    s.sceneId = r.u32();
    s.bars = r.u8();
    s.repeats = r.u8();
    a.sections.push_back(s);
  }
  if (!r.ok()) return makeError(ErrorCode::Corrupt, "arrangement is truncated");
  Status v = validateStored(a);
  if (!v.ok()) return v.error();
  return a;
}

std::vector<int> allowedBars(int patternBars) {
  std::vector<int> out;
  const int p = std::max(1, patternBars);
  for (int b : kBarChoices)
    if (b % p == 0) out.push_back(b);
  if (out.empty()) out.assign(std::begin(kBarChoices), std::end(kBarChoices));
  return out;
}

int defaultBars(int patternBars) {
  const std::vector<int> ok = allowedBars(patternBars);
  for (int b : ok)
    if (b >= 4) return b;
  return ok.back();
}

std::string truncationNote(int sceneNumber, int patternBars, int sectionBars) {
  if (patternBars <= sectionBars) return {};
  return "Scene " + std::to_string(sceneNumber) + " has " + std::to_string(patternBars) + " bars; using the first " + std::to_string(sectionBars);
}

Arrangement autoFill16(const scenes::SceneList& list, const Arrangement& keep) {
  Arrangement a = keep;
  a.sections.clear();
  if (list.scenes.empty()) return a;
  int total = 0;
  for (std::size_t i = 0; total < 16 && a.sections.size() < kMaxArrangementSections; ++i) {
    const scenes::Scene& sc = list.scenes[i % list.scenes.size()];
    ArrangementSection s;
    s.sceneId = sc.id;
    s.bars = static_cast<std::uint8_t>(defaultBars(sc.bars));
    s.repeats = 1;
    a.sections.push_back(s);
    total += s.bars;
  }
  return a;
}

Arrangement fromSceneChain(const scenes::SceneList& list, const Arrangement& keep) {
  Arrangement a = keep;
  a.sections.clear();
  if (list.scenes.empty()) return a;
  std::set<scenes::SceneId> seen;
  int total = 0;
  std::size_t i = 0;
  while (i < list.scenes.size() && a.sections.size() < kMaxArrangementSections) {
    const scenes::Scene& sc = list.scenes[i];
    if (!seen.insert(sc.id).second) break;
    ArrangementSection s;
    s.sceneId = sc.id;
    s.bars = static_cast<std::uint8_t>(defaultBars(sc.bars));
    s.repeats = static_cast<std::uint8_t>(std::min(kMaxSectionRepeats, std::max(1, sc.loops)));
    // Trim: fewer repeats first, then stop when even one repeat does not fit.
    while (s.repeats > 1 && total + s.bars * s.repeats > kMaxArrangementBars) --s.repeats;
    if (total + s.bars * s.repeats > kMaxArrangementBars) break;
    a.sections.push_back(s);
    total += s.bars * s.repeats;
    if (total >= kMaxArrangementBars) break;
    switch (sc.next) {
      case scenes::Next::Advance:
        ++i;
        break;
      case scenes::Next::Jump: {
        const int j = scenes::indexOf(list, sc.jumpTo);
        if (j < 0) return a;
        i = static_cast<std::size_t>(j);
        break;
      }
      default:
        return a;
    }
  }
  return a;
}

}  // namespace chopfractal::composition
