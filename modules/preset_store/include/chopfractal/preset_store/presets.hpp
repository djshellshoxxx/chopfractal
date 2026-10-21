#pragma once
// Preset records: generation settings, Fractal Rhythm motif and Evolve cadence. Never a seed or a pattern, so
// loading a preset changes controls only; the pattern changes when the user presses Generate.
//
// File layout (little endian): "CFPR" | u32 version | str name | u8 flags | i32 bars | f64 density | f64 swing |
// f64 variation | f64 fxIntensity | f64 chaos | str motif | i32 depth | f64 mutation | f64 accentDecay |
// i32 evolveEvery | f64 evolveAmount | [version 2 only: str category | str description | u32 n | n x (str id, f64 value) |
// u8 useSeed | u64 seed | i32 gridDivision | u8 gridTriplet | u32 ruleBytes | rule bytes (roles::serialize)] |
// u32 checksum (FNV-1a of every preceding byte). Nothing may follow. Version 1 files stay readable forever.
#include <chopfractal/chop_contracts/result.hpp>
#include <chopfractal/chop_roles_grammar/grammar.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::presets {

constexpr std::uint32_t kVersion = 2;
constexpr std::size_t kMaxName = 48, kMaxCategory = 24, kMaxDescription = 120, kMaxParams = 32, kMaxParamId = 40, kMaxRules = 64;
constexpr std::size_t kMaxFileBytes = 64 * 1024;

// A host parameter the preset sets, by manifest id, as a plain (not normalised) value; a Choice stores its index.
struct ParamValue {
  std::string id;
  double value = 0.0;
};

struct Preset {
  std::string name = "Untitled";
  int bars = 4;               // 1, 2, 4 or 8
  double density = 0.5;       // 0..1
  double swing = 0.0;         // 0..1
  double variation = 0.5;     // 0..1
  double fxIntensity = 0.5;   // 0..1
  double chaos = 0.0;         // 0..1
  bool allowReverse = false, allowPitch = false, allowRetrigger = false, allowFilter = false, allowGlide = false, allowCrunch = false;
  std::string motif = "x.xx";  // 2..8 cells of x, a, .  (at least one hit)
  int depth = 2;               // 1..3
  double mutation = 0.0;       // 0..1
  double accentDecay = 0.85;   // (0, 1]
  int evolveEvery = 4;         // 1, 2, 4, 8 or 16 loops
  double evolveAmount = 0.25;  // 0..1
  // Version 2: break style and the rest of the sound.
  std::string category = "User";   // the style shown in the browser: a factory style name or "User"
  std::string description;         // 0..120 bytes
  std::vector<ParamValue> params;  // <= 32 manifest parameters beyond the flat fields above (engine v2 profile, stutter, roll ...)
  bool useSeed = false;            // a preset only sets the seed when asked to
  std::uint64_t seed = 1;          // 0..999999999999
  int gridDivision = 16;           // generation grid {1, 2, 4, 8, 16, 32}
  bool gridTriplet = false;
  roles::RuleSet rules;            // <= 64 rule templates; applying a preset replaces the project's rules with these
};

Status validate(const Preset& p);
std::vector<std::uint8_t> serialize(const Preset& p);  // precondition: validate(p).ok()
Result<Preset> deserialize(const std::uint8_t* data, std::size_t size);

// The built-in break-style presets (engine v2 profiles), all valid; never written to disk by this module.
const std::vector<Preset>& factoryPresets();
// The factory break styles in browser order; user presets use "User".
const std::vector<std::string>& factoryCategories();

// Lower-case [a-z0-9_-] stem (at most 48 characters, "untitled" when nothing survives) plus ".cfpreset".
std::string fileNameFor(const std::string& presetName);

}  // namespace chopfractal::presets
