#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/preset_store/presets.hpp>

#include <cmath>

namespace chopfractal::presets {
namespace {

bool in01(double v) { return std::isfinite(v) && v >= 0.0 && v <= 1.0; }

std::uint32_t fnv1a(const std::uint8_t* d, std::size_t n) {
  std::uint32_t h = 2166136261u;
  for (std::size_t i = 0; i < n; ++i) h = (h ^ d[i]) * 16777619u;
  return h;
}

bool printable(const std::string& s) {
  for (unsigned char c : s)
    if (c < 0x20 || c == 0x7F) return false;
  return true;
}

}  // namespace

Status validate(const Preset& p) {
  if (p.name.empty() || p.name.size() > kMaxName || !printable(p.name)) return makeError(ErrorCode::InvalidArgument, "preset name must be 1 to 48 printable characters");
  if (!(p.bars == 1 || p.bars == 2 || p.bars == 4 || p.bars == 8)) return makeError(ErrorCode::OutOfRange, "bars must be 1, 2, 4 or 8");
  if (!in01(p.density) || !in01(p.swing) || !in01(p.variation) || !in01(p.fxIntensity) || !in01(p.chaos) || !in01(p.mutation) || !in01(p.evolveAmount))
    return makeError(ErrorCode::OutOfRange, "a preset value lies outside 0 to 1");
  if (p.motif.size() < 2 || p.motif.size() > 8) return makeError(ErrorCode::OutOfRange, "motif must have 2 to 8 cells");
  bool hit = false;
  for (char c : p.motif) {
    if (c != 'x' && c != 'a' && c != '.') return makeError(ErrorCode::InvalidArgument, "motif may only contain x, a and .");
    hit = hit || c != '.';
  }
  if (!hit) return makeError(ErrorCode::InvalidArgument, "motif needs at least one hit");
  if (p.depth < 1 || p.depth > 3) return makeError(ErrorCode::OutOfRange, "depth must be 1 to 3");
  if (!(std::isfinite(p.accentDecay) && p.accentDecay > 0.0 && p.accentDecay <= 1.0)) return makeError(ErrorCode::OutOfRange, "accent decay must lie in (0, 1]");
  const int e = p.evolveEvery;
  if (!(e == 1 || e == 2 || e == 4 || e == 8 || e == 16)) return makeError(ErrorCode::OutOfRange, "evolve cadence must be 1, 2, 4, 8 or 16 loops");
  return {};
}

std::vector<std::uint8_t> serialize(const Preset& p) {
  std::vector<std::uint8_t> out;
  bytes::Writer w(out);
  for (char c : {'C', 'F', 'P', 'R'}) w.u8(static_cast<std::uint8_t>(c));
  w.u32(kVersion);
  w.str(p.name);
  w.u8(static_cast<std::uint8_t>((p.allowReverse ? 1 : 0) | (p.allowPitch ? 2 : 0) | (p.allowRetrigger ? 4 : 0) | (p.allowFilter ? 8 : 0) |
                                  (p.allowGlide ? 16 : 0) | (p.allowCrunch ? 32 : 0)));
  w.i32(p.bars);
  w.f64(p.density);
  w.f64(p.swing);
  w.f64(p.variation);
  w.f64(p.fxIntensity);
  w.f64(p.chaos);
  w.str(p.motif);
  w.i32(p.depth);
  w.f64(p.mutation);
  w.f64(p.accentDecay);
  w.i32(p.evolveEvery);
  w.f64(p.evolveAmount);
  w.u32(fnv1a(out.data(), out.size()));
  return out;
}

Result<Preset> deserialize(const std::uint8_t* d, std::size_t n) {
  if (!d || n < 12) return makeError(ErrorCode::Corrupt, "not a preset file");
  if (n > 4096) return makeError(ErrorCode::LimitExceeded, "preset file is too large");
  bytes::Reader r(d, n);
  const char magic[4] = {static_cast<char>(r.u8()), static_cast<char>(r.u8()), static_cast<char>(r.u8()), static_cast<char>(r.u8())};
  if (!r.ok() || magic[0] != 'C' || magic[1] != 'F' || magic[2] != 'P' || magic[3] != 'R') return makeError(ErrorCode::Corrupt, "not a preset file");
  const std::uint32_t version = r.u32();
  if (version == 0 || version > kVersion) return makeError(ErrorCode::UnsupportedVersion, "preset was saved by a newer version", version);
  Preset p;
  p.name = r.str(kMaxName);
  const std::uint8_t flags = r.u8();
  p.allowReverse = flags & 1;
  p.allowPitch = flags & 2;
  p.allowRetrigger = flags & 4;
  p.allowFilter = flags & 8;
  p.allowGlide = flags & 16;
  p.allowCrunch = flags & 32;
  p.bars = r.i32();
  p.density = r.f64();
  p.swing = r.f64();
  p.variation = r.f64();
  p.fxIntensity = r.f64();
  p.chaos = r.f64();
  p.motif = r.str(8);
  p.depth = r.i32();
  p.mutation = r.f64();
  p.accentDecay = r.f64();
  p.evolveEvery = r.i32();
  p.evolveAmount = r.f64();
  const std::size_t bodyEnd = n - r.remaining();
  const std::uint32_t sum = r.u32();
  if (!r.ok()) return makeError(ErrorCode::Corrupt, "preset file is truncated");
  if (r.remaining() != 0) return makeError(ErrorCode::Corrupt, "preset file has trailing bytes");
  if (sum != fnv1a(d, bodyEnd)) return makeError(ErrorCode::Corrupt, "preset checksum does not match");
  if (flags > 63) return makeError(ErrorCode::Corrupt, "unknown preset flags");
  Status v = validate(p);
  if (!v.ok()) return makeError(ErrorCode::Corrupt, "preset values are invalid: " + v.error().message);
  return p;
}

const std::vector<Preset>& factoryPresets() {
  static const std::vector<Preset> list = [] {
    std::vector<Preset> v;
    auto add = [&](const char* name, int bars, double dens, double swing, double var, double fxi, double chaos, unsigned flags, const char* motif, int depth,
                   double mut, int every, double amount) {
      Preset p;
      p.name = name;
      p.bars = bars;
      p.density = dens;
      p.swing = swing;
      p.variation = var;
      p.fxIntensity = fxi;
      p.chaos = chaos;
      p.allowReverse = flags & 1;
      p.allowPitch = flags & 2;
      p.allowRetrigger = flags & 4;
      p.allowFilter = flags & 8;
      p.allowGlide = flags & 16;
      p.allowCrunch = flags & 32;
      p.motif = motif;
      p.depth = depth;
      p.mutation = mut;
      p.evolveEvery = every;
      p.evolveAmount = amount;
      v.push_back(p);
    };
    add("Tight Groove", 4, 0.55, 0.00, 0.25, 0.30, 0.00, 0, "x.xx", 1, 0.0, 8, 0.15);
    add("Lazy Swing", 4, 0.50, 0.55, 0.35, 0.30, 0.00, 8, "x..x.x..", 1, 0.0, 8, 0.20);
    add("Stutter Storm", 2, 0.70, 0.00, 0.60, 0.50, 0.30, 4 | 1, "xxax", 2, 0.30, 4, 0.35);
    add("Tape Wobble", 4, 0.45, 0.20, 0.50, 0.70, 0.20, 16 | 8, "a.x.xx..", 1, 0.0, 4, 0.30);
    add("Glitch Garden", 4, 0.65, 0.00, 0.75, 0.80, 0.60, 1 | 2 | 4 | 8 | 16 | 32, "x.xx", 3, 0.50, 2, 0.50);
    add("Fractal Break", 8, 0.60, 0.10, 0.45, 0.40, 0.10, 1 | 4, "a.xx.x..", 2, 0.15, 4, 0.25);
    return v;
  }();
  return list;
}

std::string fileNameFor(const std::string& name) {
  std::string s;
  bool lastUnderscore = true;
  for (char ch : name) {
    const unsigned char c = static_cast<unsigned char>(ch);
    char o = '_';
    if (c >= 'a' && c <= 'z') o = static_cast<char>(c);
    else if (c >= 'A' && c <= 'Z') o = static_cast<char>(c - 'A' + 'a');
    else if ((c >= '0' && c <= '9') || c == '-') o = static_cast<char>(c);
    if (o == '_') {
      if (lastUnderscore) continue;
      lastUnderscore = true;
    } else {
      lastUnderscore = false;
    }
    s.push_back(o);
    if (s.size() >= 48) break;
  }
  while (!s.empty() && s.back() == '_') s.pop_back();
  return (s.empty() ? std::string("untitled") : s) + ".cfpreset";
}

}  // namespace chopfractal::presets
