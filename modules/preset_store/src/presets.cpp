#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/preset_store/presets.hpp>

#include <cmath>
#include <cstdint>

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
  if (p.category.empty() || p.category.size() > kMaxCategory || !printable(p.category)) return makeError(ErrorCode::InvalidArgument, "category must be 1 to 24 printable characters");
  if (p.description.size() > kMaxDescription || !printable(p.description)) return makeError(ErrorCode::InvalidArgument, "description must be at most 120 printable characters");
  if (p.params.size() > kMaxParams) return makeError(ErrorCode::LimitExceeded, "a preset holds at most 32 parameters");
  for (std::size_t i = 0; i < p.params.size(); ++i) {
    const ParamValue& v = p.params[i];
    if (v.id.empty() || v.id.size() > kMaxParamId || !printable(v.id)) return makeError(ErrorCode::InvalidArgument, "parameter id is empty, too long or not printable");
    if (!std::isfinite(v.value) || std::fabs(v.value) > 1.0e6) return makeError(ErrorCode::OutOfRange, "parameter value is not a sensible number");
    for (std::size_t j = 0; j < i; ++j)
      if (p.params[j].id == v.id) return makeError(ErrorCode::Conflict, "duplicate parameter id " + v.id);
  }
  if (p.seed > 999999999999ull) return makeError(ErrorCode::OutOfRange, "seed must be 0 to 999999999999");
  const int g = p.gridDivision;
  if (!(g == 1 || g == 2 || g == 4 || g == 8 || g == 16 || g == 32)) return makeError(ErrorCode::OutOfRange, "grid must be 1, 2, 4, 8, 16 or 32");
  if (p.rules.rules.size() > kMaxRules) return makeError(ErrorCode::LimitExceeded, "a preset holds at most 64 rules");
  if (!p.rules.rules.empty() && !roles::validateRules(p.rules).ok()) return makeError(ErrorCode::InvalidArgument, "the preset rules are not valid");
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
  w.str(p.category);
  w.str(p.description);
  w.u32(static_cast<std::uint32_t>(p.params.size()));
  for (const ParamValue& v : p.params) {
    w.str(v.id);
    w.f64(v.value);
  }
  w.u8(p.useSeed ? 1 : 0);
  w.u64(p.seed);
  w.i32(p.gridDivision);
  w.u8(p.gridTriplet ? 1 : 0);
  const std::vector<std::uint8_t> ruleBytes = p.rules.rules.empty() ? std::vector<std::uint8_t>() : roles::serialize(roles::RoleMap{}, p.rules);
  w.u32(static_cast<std::uint32_t>(ruleBytes.size()));
  w.raw(ruleBytes.data(), ruleBytes.size());
  w.u32(fnv1a(out.data(), out.size()));
  return out;
}

Result<Preset> deserialize(const std::uint8_t* d, std::size_t n) {
  if (!d || n < 12) return makeError(ErrorCode::Corrupt, "not a preset file");
  if (n > kMaxFileBytes) return makeError(ErrorCode::LimitExceeded, "preset file is too large");
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
  if (version >= 2) {
    p.category = r.str(kMaxCategory);
    p.description = r.str(kMaxDescription);
    const std::uint32_t count = r.u32();
    if (!r.ok() || count > kMaxParams) return makeError(ErrorCode::Corrupt, "preset parameter list is invalid");
    for (std::uint32_t i = 0; i < count && r.ok(); ++i) {
      ParamValue v;
      v.id = r.str(kMaxParamId);
      v.value = r.f64();
      p.params.push_back(std::move(v));
    }
    p.useSeed = r.u8() != 0;
    p.seed = r.u64();
    p.gridDivision = r.i32();
    p.gridTriplet = r.u8() != 0;
    const std::uint32_t ruleLen = r.u32();
    if (!r.ok() || ruleLen > r.remaining()) return makeError(ErrorCode::Corrupt, "preset rules are truncated");
    if (ruleLen > 0) {
      const std::uint8_t* rb = r.take(ruleLen);
      auto state = roles::deserialize(rb, ruleLen);
      if (!state.ok()) return makeError(ErrorCode::Corrupt, "preset rules are corrupt");
      p.rules = state.value().rules;
    }
  }
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


namespace {
struct Spec {
  const char* name;
  const char* category;
  const char* description;
  int bars;
  double density, swing, variation, fx, chaos;
  unsigned flags;  // 1 reverse, 2 pitch, 4 retrigger, 8 filter, 16 glide, 32 crunch
  const char* motif;
  int depth;
  double mutation;
  int every;
  double amount;
  int grid;
  bool triplet;
  std::uint64_t seed;
  int rules;  // 0 none, 1 backbeat, 2 no repeats, 3 backbeat + no repeats, 4 build
  std::vector<ParamValue> params;
};

roles::RuleSet rulesFor(int kind) {
  roles::RuleSet rs;
  if (kind == 1 || kind == 3) {
    rs.rules.push_back(roles::templates::preserveFirst(1, "kick"));
    rs.rules.push_back(roles::templates::requireInFinalBeat(2, "snare"));
  }
  if (kind == 2) rs.rules.push_back(roles::templates::avoidAdjacentRepeats(1, roles::RepeatBy::SameChop));
  if (kind == 3) rs.rules.push_back(roles::templates::avoidAdjacentRepeats(3, roles::RepeatBy::SameChop));
  if (kind == 4) {
    rs.rules.push_back(roles::templates::preferAtPhraseBoundary(1, "cymbal", 3.0));
    rs.rules.push_back(roles::templates::avoidAdjacentRepeats(2, roles::RepeatBy::SameRole));
  }
  return rs;
}

// The engine v2 break parameters of a style: profile index, backbeat (0 standard, 1 rolling), anchors (0 profile, 1 strict, 2 free).
std::vector<ParamValue> brk(int profile, int backbeat, int anchors, double groove, double humanize, double fill, double glitch) {
  return {{"engine_version", 1}, {"break_profile", static_cast<double>(profile)}, {"break_backbeat", static_cast<double>(backbeat)}, {"break_anchors", static_cast<double>(anchors)},
          {"break_groove", groove}, {"break_humanize", humanize}, {"break_fill", fill}, {"break_glitch", glitch}};
}
std::vector<ParamValue> join(std::vector<ParamValue> a, const std::vector<ParamValue>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}
}  // namespace

const std::vector<Preset>& factoryPresets() {
  static const std::vector<Preset> list = [] {
    // Break profile indices (break_profile): 0 Amen / Funk, 1 Jungle, 2 DnB Rolling, 3 Breakcore, 4 IDM Glitch, 5 Hip-hop.
    const std::vector<Spec> specs = {
        {"Amen Classic", "Amen / Funk", "Tight two-bar funk break with the kick and the snare on 2 and 4 held in place", 2, 0.60, 0.10, 0.30, 0.30, 0.00, 0, "x.xx", 2, 0.10, 8, 0.15, 16, false, 1101, 1, brk(0, 0, 1, 0.60, 0.50, 0.40, 0.10)},
        {"Funky Drummer", "Amen / Funk", "Loose four-bar groove with ghost notes and a late snare fill", 4, 0.50, 0.30, 0.25, 0.30, 0.00, 0, "x..xx.x.", 1, 0.05, 8, 0.15, 16, false, 1102, 1, brk(0, 0, 0, 0.80, 0.70, 0.50, 0.00)},
        {"Jungle Roll", "Jungle", "Fast rolling chops with retriggers and a busy snare", 2, 0.80, 0.10, 0.55, 0.60, 0.00, 4, "xxax", 3, 0.30, 4, 0.30, 16, false, 1103, 2, brk(1, 0, 0, 0.50, 0.50, 0.60, 0.25)},
        {"Ragga Chop", "Jungle", "Pitched and filtered stabs over a strict jungle backbone", 4, 0.70, 0.20, 0.50, 0.55, 0.05, 2 | 8, "x.xx", 2, 0.25, 4, 0.25, 16, false, 1104, 3, brk(1, 0, 1, 0.55, 0.45, 0.50, 0.30)},
        {"Rolling Two-Step", "DnB Rolling", "Ghost notes right after the backbeat and sixteenth hats, the rolling drum and bass feel", 2, 0.65, 0.05, 0.35, 0.40, 0.00, 0, "x.x.", 2, 0.15, 4, 0.20, 16, false, 1105, 1, brk(2, 1, 1, 0.40, 0.40, 0.50, 0.10)},
        {"Liquid Roller", "DnB Rolling", "Smooth four-bar roller with filter movement and a soft echo", 4, 0.50, 0.10, 0.25, 0.50, 0.00, 8, "x..x.x..", 2, 0.10, 8, 0.15, 16, false, 1106, 4,
         join(brk(2, 1, 0, 0.45, 0.50, 0.35, 0.00), {{"allow_echo", 1}, {"echo_amount", 30}, {"echo_rate", 1}})},
        {"Breakcore Storm", "Breakcore", "One bar of dense reversed and crushed hits, nothing is protected", 1, 0.90, 0.00, 0.80, 0.80, 0.50, 1 | 4 | 32, "xxxx", 3, 0.60, 2, 0.50, 32, false, 1107, 0, brk(3, 0, 0, 0.30, 0.20, 0.80, 0.80)},
        {"Mashed Amen", "Breakcore", "Two bars of pitched, retriggered chops with snare rolls", 2, 0.85, 0.00, 0.70, 0.70, 0.35, 2 | 4 | 32, "xxax", 3, 0.50, 2, 0.45, 32, false, 1108, 0,
         join(brk(3, 0, 0, 0.35, 0.25, 0.70, 0.60), {{"allow_roll", 1}, {"roll_amount", 40}, {"roll_rate", 4}, {"roll_rate_end", 6}})},
        {"IDM Grid Fracture", "IDM Glitch", "Triplet sixteenth grid that keeps breaking its own pattern", 4, 0.55, 0.00, 0.65, 0.70, 0.40, 1 | 8 | 16, "x.xx", 3, 0.45, 2, 0.40, 16, true, 1109, 0, brk(4, 0, 2, 0.40, 0.60, 0.50, 0.70)},
        {"Clicks and Cuts", "IDM Glitch", "Sparse, tiny edits and crushed clicks on a thirty-second grid", 2, 0.40, 0.00, 0.60, 0.75, 0.50, 2 | 8 | 32, "x.x", 2, 0.40, 2, 0.35, 32, false, 1110, 0, brk(4, 0, 2, 0.30, 0.30, 0.30, 0.90)},
        {"Boom Bap Basics", "Boom-bap", "Two-bar hip-hop drums with a lazy groove and a firm backbeat", 2, 0.45, 0.30, 0.25, 0.50, 0.00, 0, "x.xx", 2, 0.10, 8, 0.15, 16, false, 1111, 1, brk(5, 0, 1, 0.70, 0.60, 0.20, 0.00)},
        {"Dusty Swing", "Boom-bap", "Swung eighth notes, pitched chops and tape glide", 4, 0.38, 0.55, 0.30, 0.40, 0.00, 2 | 16, "x.x.", 2, 0.15, 8, 0.20, 8, false, 1112, 1, brk(5, 0, 0, 0.80, 0.80, 0.15, 0.00)},
        {"Chopped Soul", "Boom-bap", "Four bars of sample chops with a filtered answer in the last beat", 4, 0.50, 0.20, 0.35, 0.50, 0.00, 2 | 8, "xaxx", 2, 0.20, 4, 0.20, 16, false, 1113, 3, brk(5, 0, 0, 0.65, 0.55, 0.30, 0.05)},
        {"Stutter Edit Gate", "Stutter Edit", "Gated sixteenth stutters on an Amen backbone", 2, 0.60, 0.00, 0.50, 0.80, 0.15, 4, "xxax", 2, 0.30, 4, 0.30, 16, false, 1114, 1,
         join(brk(0, 0, 1, 0.50, 0.40, 0.40, 0.40), {{"allow_stutter", 1}, {"stutter_amount", 70}, {"stutter_rate", 4}, {"stutter_gate", 45}, {"stutter_decay_db", -3}, {"stutter_pitch", 0}})},
        {"Stutter Edit Tape", "Stutter Edit", "Slowing eighth stutters with tape glide and an echo tail", 2, 0.50, 0.15, 0.45, 0.85, 0.10, 16, "x.x.", 2, 0.25, 4, 0.25, 16, false, 1115, 1,
         join(brk(5, 0, 0, 0.60, 0.50, 0.25, 0.15),
              {{"allow_stutter", 1}, {"stutter_amount", 55}, {"stutter_rate", 2}, {"stutter_gate", 70}, {"stutter_decay_db", -1.5}, {"stutter_pitch", -5}, {"allow_echo", 1}, {"echo_amount", 45}, {"echo_rate", 2}})},
        {"Stutter Edit Roll", "Stutter Edit", "Accelerating rolls that freeze on the last hit of the bar", 2, 0.65, 0.00, 0.50, 0.80, 0.20, 4, "xxax", 3, 0.30, 2, 0.30, 16, false, 1116, 1,
         join(brk(2, 1, 1, 0.40, 0.40, 0.60, 0.30),
              {{"allow_roll", 1}, {"roll_amount", 65}, {"roll_rate", 4}, {"roll_rate_end", 8}, {"roll_freeze", 30}, {"allow_stutter", 1}, {"stutter_amount", 40}, {"stutter_rate", 6}})},
        {"Stutter Edit Glitch", "Stutter Edit", "Hard pitched thirty-second stutters and rolls with crush", 1, 0.80, 0.00, 0.75, 0.95, 0.50, 4 | 32, "xxxx", 3, 0.50, 2, 0.50, 32, false, 1117, 0,
         join(brk(3, 0, 0, 0.30, 0.20, 0.80, 0.85),
              {{"allow_stutter", 1}, {"stutter_amount", 90}, {"stutter_rate", 6}, {"stutter_gate", 30}, {"stutter_decay_db", -6}, {"stutter_pitch", 7}, {"allow_roll", 1}, {"roll_amount", 50}, {"roll_rate", 6}, {"roll_rate_end", 9}})},
    };
    std::vector<Preset> v;
    for (const Spec& s : specs) {
      Preset p;
      p.name = s.name;
      p.category = s.category;
      p.description = s.description;
      p.bars = s.bars;
      p.density = s.density;
      p.swing = s.swing;
      p.variation = s.variation;
      p.fxIntensity = s.fx;
      p.chaos = s.chaos;
      p.allowReverse = s.flags & 1;
      p.allowPitch = s.flags & 2;
      p.allowRetrigger = s.flags & 4;
      p.allowFilter = s.flags & 8;
      p.allowGlide = s.flags & 16;
      p.allowCrunch = s.flags & 32;
      p.motif = s.motif;
      p.depth = s.depth;
      p.mutation = s.mutation;
      p.evolveEvery = s.every;
      p.evolveAmount = s.amount;
      p.params = s.params;
      p.useSeed = true;
      p.seed = s.seed;
      p.gridDivision = s.grid;
      p.gridTriplet = s.triplet;
      p.rules = rulesFor(s.rules);
      v.push_back(std::move(p));
    }
    return v;
  }();
  return list;
}

const std::vector<std::string>& factoryCategories() {
  static const std::vector<std::string> cats = {"Amen / Funk", "Jungle", "DnB Rolling", "Breakcore", "IDM Glitch", "Boom-bap", "Stutter Edit"};
  return cats;
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
  if (s.empty()) {
    s = "untitled";
    // Names with no ASCII letters or digits (for example non-Latin scripts) would all collide on "untitled".
    if (!name.empty()) {
      std::uint32_t h = 2166136261u;
      for (char ch : name) h = (h ^ static_cast<unsigned char>(ch)) * 16777619u;
      static const char* const hex = "0123456789abcdef";
      s += '-';
      for (int shift = 20; shift >= 0; shift -= 4) s += hex[(h >> shift) & 0xFu];
    }
  }
  // Windows reserves these device names whatever the extension.
  static const char* const kReserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
                                          "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
  for (const char* r : kReserved)
    if (s == r) {
      s = "preset-" + s;
      break;
    }
  return s + ".cfpreset";
}

}  // namespace chopfractal::presets
