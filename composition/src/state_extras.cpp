#include "state_wire.hpp"

#include <chopfractal/chop_contracts/bytes.hpp>

#include <cmath>

namespace chopfractal::composition {

namespace {

bool validDivision(int d) { return d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32; }
bool validMotif(const std::string& m) {
  if (m.empty() || m.size() > 8) return false;
  for (char c : m)
    if (c != 'x' && c != 'a' && c != '.') return false;
  return true;
}
int divisionCode(int d) {  // 1->0 ... 32->5
  int c = 0;
  while ((1 << c) < d) ++c;
  return c;
}

Status bad(ErrorCode code, const char* field) { return makeError(code, std::string("project extras: ") + field + " is out of range"); }

// First failing field, or ok.
Status check(const ProjectExtras& e) {
  if (e.seed > 999999999999ull) return bad(ErrorCode::OutOfRange, "seed");
  if (!validMotif(e.motif)) return bad(ErrorCode::InvalidArgument, "motif");
  if (e.fractalDepth < 1 || e.fractalDepth > 3) return bad(ErrorCode::OutOfRange, "fractalDepth");
  if (e.roleIndex < 0 || static_cast<std::size_t>(e.roleIndex) >= roles::builtinRoles().size()) return bad(ErrorCode::OutOfRange, "roleIndex");
  if (e.detectMode < 0 || e.detectMode > 3) return bad(ErrorCode::OutOfRange, "detectMode");
  if (!validDivision(e.gridDivision)) return bad(ErrorCode::OutOfRange, "gridDivision");
  if (!(e.fractalMutation == -1.0 || (e.fractalMutation >= 0.0 && e.fractalMutation <= 1.0))) return bad(ErrorCode::OutOfRange, "fractalMutation");
  if (!(e.fractalAccentDecay > 0.0 && e.fractalAccentDecay <= 1.0)) return bad(ErrorCode::OutOfRange, "fractalAccentDecay");
  if (e.exportUi.formatIndex < 0 || e.exportUi.formatIndex > 2) return bad(ErrorCode::OutOfRange, "exportUi.formatIndex");
  if (e.exportUi.rateIndex < 0 || e.exportUi.rateIndex > 3) return bad(ErrorCode::OutOfRange, "exportUi.rateIndex");
  if (e.exportUi.loopsIndex < 0 || e.exportUi.loopsIndex > 4) return bad(ErrorCode::OutOfRange, "exportUi.loopsIndex");
  {
    const MidiExportSettings& m = e.exportUi.midi;
    if (m.baseNote < 0 || m.baseNote > 127) return bad(ErrorCode::OutOfRange, "exportUi.midi.baseNote");
    if (static_cast<std::uint8_t>(m.curve) > 3) return bad(ErrorCode::OutOfRange, "exportUi.midi.curve");
    if (m.fixedVelocity < 1 || m.fixedVelocity > 127) return bad(ErrorCode::OutOfRange, "exportUi.midi.fixedVelocity");
    if (m.loops < 1 || m.loops > 16) return bad(ErrorCode::OutOfRange, "exportUi.midi.loops");
    if (m.channel < 0 || m.channel > 15) return bad(ErrorCode::OutOfRange, "exportUi.midi.channel");
  }
  const UiPrefs& u = e.ui;
  if (u.viewMode < 0 || u.viewMode > 1) return bad(ErrorCode::OutOfRange, "ui.viewMode");
  if (u.scalePercent < 50 || u.scalePercent > 200) return bad(ErrorCode::OutOfRange, "ui.scalePercent");
  if (u.width < 640 || u.width > 4096) return bad(ErrorCode::OutOfRange, "ui.width");
  if (u.height < 480 || u.height > 4096) return bad(ErrorCode::OutOfRange, "ui.height");
  if (u.markerSnap < 0 || u.markerSnap > 2) return bad(ErrorCode::OutOfRange, "ui.markerSnap");
  if (u.snapDivision != 0 && !validDivision(u.snapDivision)) return bad(ErrorCode::OutOfRange, "ui.snapDivision");
  if (u.search.size() > 32) return bad(ErrorCode::OutOfRange, "ui.search");
  return {};
}

// Resets each invalid field to its default on its own, so one bad value never costs the others.
void sanitize(ProjectExtras& e) {
  const ProjectExtras d;
  if (e.seed > 999999999999ull) e.seed = d.seed;
  if (!validMotif(e.motif)) e.motif = d.motif;
  if (e.fractalDepth < 1 || e.fractalDepth > 3) e.fractalDepth = d.fractalDepth;
  if (e.roleIndex < 0 || static_cast<std::size_t>(e.roleIndex) >= roles::builtinRoles().size()) e.roleIndex = d.roleIndex;
  if (e.detectMode < 0 || e.detectMode > 3) e.detectMode = d.detectMode;
  if (!validDivision(e.gridDivision)) e.gridDivision = d.gridDivision;
  if (!(e.fractalMutation == -1.0 || (e.fractalMutation >= 0.0 && e.fractalMutation <= 1.0))) e.fractalMutation = d.fractalMutation;
  if (!(e.fractalAccentDecay > 0.0 && e.fractalAccentDecay <= 1.0)) e.fractalAccentDecay = d.fractalAccentDecay;
  if (e.exportUi.formatIndex < 0 || e.exportUi.formatIndex > 2) e.exportUi.formatIndex = d.exportUi.formatIndex;
  if (e.exportUi.rateIndex < 0 || e.exportUi.rateIndex > 3) e.exportUi.rateIndex = d.exportUi.rateIndex;
  if (e.exportUi.loopsIndex < 0 || e.exportUi.loopsIndex > 4) e.exportUi.loopsIndex = d.exportUi.loopsIndex;
  {
    MidiExportSettings& m = e.exportUi.midi;
    const MidiExportSettings& dm = d.exportUi.midi;
    if (m.baseNote < 0 || m.baseNote > 127) m.baseNote = dm.baseNote;
    if (static_cast<std::uint8_t>(m.curve) > 3) m.curve = dm.curve;
    if (m.fixedVelocity < 1 || m.fixedVelocity > 127) m.fixedVelocity = dm.fixedVelocity;
    if (m.loops < 1 || m.loops > 16) m.loops = dm.loops;
    if (m.channel < 0 || m.channel > 15) m.channel = dm.channel;
  }
  UiPrefs& u = e.ui;
  if (u.viewMode < 0 || u.viewMode > 1) u.viewMode = d.ui.viewMode;
  if (u.scalePercent < 50 || u.scalePercent > 200) u.scalePercent = d.ui.scalePercent;
  if (u.width < 640 || u.width > 4096) u.width = d.ui.width;
  if (u.height < 480 || u.height > 4096) u.height = d.ui.height;
  if (u.markerSnap < 0 || u.markerSnap > 2) u.markerSnap = d.ui.markerSnap;
  if (u.snapDivision != 0 && !validDivision(u.snapDivision)) u.snapDivision = d.ui.snapDivision;
  if (u.search.size() > 32) u.search = d.ui.search;
}

}  // namespace

Status validate(const ProjectExtras& e) { return check(e); }

std::size_t sectionByteCap(std::uint8_t tag) {
  constexpr std::size_t kKiB = 1024, kMiB = 1024 * 1024;
  if (tag >= statewire::kTagSeed && tag <= 7) return 4 * kKiB;
  if (tag == statewire::kTagAB) return 2 * kMiB;
  if (tag == kTagFractalTune) return 64;
  if (tag == 11) return 32 * kMiB;  // F09 scenes
  if (tag == 15) return 64;         // F16 level match
  if (tag == 16) return 1 * kMiB;   // F18 arrangement
  if (tag == 0) return 4 * kKiB;
  return 64 * kKiB;                 // pads, stems, capture, groove, fx, free and reserved tags
}

namespace statewire {

std::size_t blockCap(std::uint8_t tag) { return sectionByteCap(tag) + (isFeatureTag(tag) ? 2 : 0); }

Result<Blocks> parse(const std::uint8_t* data, std::size_t size) {
  Blocks out;
  if (size == 0) return out;
  if (size > kMaxExtrasBytes) return makeError(ErrorCode::LimitExceeded, "the extras section is larger than 40 MiB");
  bytes::Reader r(data, size);
  if (r.u8() != kMarker) return makeError(ErrorCode::Corrupt, "unknown trailing data in the session payload");
  const std::uint16_t count = r.u16();
  if (!r.ok() || count > kMaxExtrasBlocks) return makeError(ErrorCode::Corrupt, "invalid extras block count");
  for (std::uint16_t i = 0; i < count; ++i) {
    const std::uint8_t tag = r.u8();
    const std::uint32_t len = r.u32();
    if (!r.ok() || len > r.remaining()) return makeError(ErrorCode::Corrupt, "an extras block runs past the end");
    if (len > blockCap(tag)) return makeError(ErrorCode::LimitExceeded, "an extras block is over its size cap");
    const std::uint8_t* p = r.take(len);
    if (len != 0 && !p) return makeError(ErrorCode::Corrupt, "malformed extras block");
    if (!out.emplace(tag, std::vector<std::uint8_t>(p, p + len)).second) return makeError(ErrorCode::Corrupt, "duplicate extras block");
  }
  if (r.remaining() != 0) return makeError(ErrorCode::Corrupt, "trailing bytes after the extras blocks");
  return out;
}

void write(const Blocks& blocks, std::vector<std::uint8_t>& out) {
  bytes::Writer w(out);
  w.u8(kMarker);
  w.u16(static_cast<std::uint16_t>(blocks.size()));
  for (const auto& [tag, payload] : blocks) {
    w.u8(tag);
    w.u32(static_cast<std::uint32_t>(payload.size()));
    w.raw(payload.data(), payload.size());
  }
}

void encodeExtras(const ProjectExtras& e, Blocks& out) {
  const ProjectExtras d;
  auto put = [&](std::uint8_t tag, const std::vector<std::uint8_t>& payload) { out[tag] = payload; };
  std::vector<std::uint8_t> b;
  if (e.seed != d.seed) {
    b.clear();
    bytes::Writer(b).u64(e.seed);
    put(kTagSeed, b);
  }
  if (e.motif != d.motif || e.fractalDepth != d.fractalDepth) {
    b.clear();
    bytes::Writer w(b);
    w.u8(static_cast<std::uint8_t>(e.fractalDepth));
    w.u8(static_cast<std::uint8_t>(e.motif.size()));
    w.raw(e.motif.data(), e.motif.size());
    put(kTagFractal, b);
  }
  if (e.roleIndex != d.roleIndex) put(kTagRole, {static_cast<std::uint8_t>(e.roleIndex)});
  if (!(e.exportUi == d.exportUi)) {
    b = {static_cast<std::uint8_t>(e.exportUi.formatIndex), static_cast<std::uint8_t>(e.exportUi.rateIndex), static_cast<std::uint8_t>(e.exportUi.loopsIndex)};
    if (!(e.exportUi.midi == d.exportUi.midi)) {  // F13: five more bytes behind the block's length, only when they are not the defaults
      const MidiExportSettings& m = e.exportUi.midi;
      for (int v : {m.baseNote, static_cast<int>(m.curve), m.fixedVelocity, m.loops, m.channel}) b.push_back(static_cast<std::uint8_t>(v));
    }
    put(kTagExport, b);
  }
  if (e.detectMode != d.detectMode) put(kTagDetect, {static_cast<std::uint8_t>(e.detectMode)});
  if (e.gridDivision != d.gridDivision || e.gridTriplet != d.gridTriplet)
    put(kTagGrid, {static_cast<std::uint8_t>(divisionCode(e.gridDivision)), static_cast<std::uint8_t>(e.gridTriplet ? 1 : 0)});
  {
    UiPrefs a = e.ui, c = d.ui;
    a.editHits = c.editHits = false;  // travels in the FractalTune block
    if (!(a == c)) {
      b.clear();
      bytes::Writer w(b);
      w.u8(static_cast<std::uint8_t>(e.ui.viewMode));
      w.u16(static_cast<std::uint16_t>(e.ui.scalePercent));
      w.u16(static_cast<std::uint16_t>(e.ui.width));
      w.u16(static_cast<std::uint16_t>(e.ui.height));
      w.u8(static_cast<std::uint8_t>(e.ui.markerSnap));
      w.u8(e.ui.gridSnap ? 1 : 0);
      w.u8(static_cast<std::uint8_t>(e.ui.snapDivision == 0 ? 0 : divisionCode(e.ui.snapDivision) + 1));
      w.u8(e.ui.favoritesOnly ? 1 : 0);
      w.u8(static_cast<std::uint8_t>(e.ui.search.size()));
      w.raw(e.ui.search.data(), e.ui.search.size());
      put(kTagUi, b);
    }
  }
  if (e.fractalMutation != d.fractalMutation || e.fractalAccentDecay != d.fractalAccentDecay || e.ui.editHits != d.ui.editHits) {
    b.clear();
    bytes::Writer w(b);
    w.f64(e.fractalMutation);
    w.f64(e.fractalAccentDecay);
    w.boolean(e.ui.editHits);
    put(kTagFractalTune, b);
  }
}

Result<ProjectExtras> decodeExtras(const Blocks& blocks) {
  ProjectExtras e;
  auto malformed = [](const char* what) { return makeError(ErrorCode::Corrupt, std::string("malformed extras block: ") + what); };
  for (const auto& [tag, payload] : blocks) {
    bytes::Reader r(payload.data(), payload.size());
    switch (tag) {
      case kTagSeed:
        e.seed = r.u64();
        if (!r.ok() || r.remaining() != 0) return malformed("seed");
        break;
      case kTagFractal: {
        e.fractalDepth = r.u8();
        const std::uint8_t n = r.u8();
        const std::uint8_t* p = r.take(n);
        if (!r.ok() || r.remaining() != 0) return malformed("fractal");
        e.motif.assign(reinterpret_cast<const char*>(p), n);
        break;
      }
      case kTagRole:
        e.roleIndex = r.u8();
        if (!r.ok() || r.remaining() != 0) return malformed("role");
        break;
      case kTagExport:
        e.exportUi.formatIndex = r.u8();
        e.exportUi.rateIndex = r.u8();
        e.exportUi.loopsIndex = r.u8();
        if (r.ok() && r.remaining() == 5) {
          MidiExportSettings& m = e.exportUi.midi;
          m.baseNote = r.u8();
          m.curve = static_cast<VelocityCurve>(r.u8());
          m.fixedVelocity = r.u8();
          m.loops = r.u8();
          m.channel = r.u8();
        }
        if (!r.ok() || r.remaining() != 0) return malformed("export");
        break;
      case kTagDetect:
        e.detectMode = r.u8();
        if (!r.ok() || r.remaining() != 0) return malformed("detect");
        break;
      case kTagGrid: {
        const int code = r.u8();
        e.gridTriplet = r.u8() != 0;
        if (!r.ok() || r.remaining() != 0) return malformed("grid");
        e.gridDivision = code <= 5 ? (1 << code) : 0;  // 0 is invalid and falls back to the default
        break;
      }
      case kTagUi: {
        UiPrefs& u = e.ui;
        u.viewMode = r.u8();
        u.scalePercent = r.u16();
        u.width = r.u16();
        u.height = r.u16();
        u.markerSnap = r.u8();
        u.gridSnap = r.u8() != 0;
        const int snap = r.u8();
        u.favoritesOnly = r.u8() != 0;
        const std::uint8_t n = r.u8();
        const std::uint8_t* p = r.take(n);
        if (!r.ok() || r.remaining() != 0) return malformed("ui");
        u.search.assign(reinterpret_cast<const char*>(p), n);
        u.snapDivision = snap == 0 ? 0 : (snap <= 6 ? (1 << (snap - 1)) : -1);
        break;
      }
      case kTagFractalTune:
        e.fractalMutation = r.f64();
        e.fractalAccentDecay = r.f64();
        e.ui.editHits = r.u8() != 0;
        if (!r.ok() || r.remaining() != 0) return malformed("fractal tuning");
        break;
      default:
        break;  // tag 8 is decoded by decodeAB, feature tags by their decoders, unknown tags below 9 are skipped
    }
  }
  sanitize(e);
  return e;
}

void encodeAB(const std::array<std::vector<std::uint8_t>, 2>& slots, Blocks& out) {
  std::vector<std::uint8_t> b;
  bytes::Writer w(b);
  std::uint8_t mask = 0;
  for (std::size_t i = 0; i < 2; ++i)
    if (!slots[i].empty()) mask |= static_cast<std::uint8_t>(1u << i);
  if (mask == 0) return;
  w.u8(mask);
  for (std::size_t i = 0; i < 2; ++i)
    if (mask & (1u << i)) {
      w.u32(static_cast<std::uint32_t>(slots[i].size()));
      w.raw(slots[i].data(), slots[i].size());
    }
  out[kTagAB] = std::move(b);
}

Result<std::array<std::vector<std::uint8_t>, 2>> decodeAB(const std::vector<std::uint8_t>& block) {
  std::array<std::vector<std::uint8_t>, 2> slots;
  bytes::Reader r(block.data(), block.size());
  const std::uint8_t mask = r.u8();
  if (!r.ok() || (mask & ~3u) != 0) return makeError(ErrorCode::Corrupt, "malformed A/B block");
  for (std::size_t i = 0; i < 2; ++i) {
    if (!(mask & (1u << i))) continue;
    const std::uint32_t len = r.u32();
    if (!r.ok() || len > r.remaining() || len > kMaxSlotBytes) return makeError(ErrorCode::Corrupt, "malformed A/B snapshot");
    const std::uint8_t* p = r.take(len);
    if (len != 0 && !p) return makeError(ErrorCode::Corrupt, "malformed A/B snapshot");
    slots[i].assign(p, p + len);
  }
  if (r.remaining() != 0) return makeError(ErrorCode::Corrupt, "trailing bytes in the A/B block");
  return slots;
}

}  // namespace statewire
}  // namespace chopfractal::composition
