#pragma once
// Preset records: generation settings, Fractal Rhythm motif and Evolve cadence. Never a seed or a pattern, so
// loading a preset changes controls only; the pattern changes when the user presses Generate.
//
// File layout (little endian): "CFPR" | u32 version | str name | u8 flags | i32 bars | f64 density | f64 swing |
// f64 variation | f64 fxIntensity | f64 chaos | str motif | i32 depth | f64 mutation | f64 accentDecay |
// i32 evolveEvery | f64 evolveAmount | u32 checksum (FNV-1a of every preceding byte). Nothing may follow.
#include <chopfractal/chop_contracts/result.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::presets {

constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kMaxName = 48;

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
};

Status validate(const Preset& p);
std::vector<std::uint8_t> serialize(const Preset& p);  // precondition: validate(p).ok()
Result<Preset> deserialize(const std::uint8_t* data, std::size_t size);

// Six built-in presets, all valid; never written to disk by this module.
const std::vector<Preset>& factoryPresets();

// Lower-case [a-z0-9_-] stem (at most 48 characters, "untitled" when nothing survives) plus ".cfpreset".
std::string fileNameFor(const std::string& presetName);

}  // namespace chopfractal::presets
