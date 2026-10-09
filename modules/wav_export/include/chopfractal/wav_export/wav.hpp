#pragma once
// Deterministic WAV encode/decode and safe file output. Pure byte transforms; no audio engine types.
#include <chopfractal/chop_contracts/result.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::wav {

enum class Format : std::uint8_t { Pcm16, Pcm24, Float32 };

struct Options {
  int sampleRate = 48000;
  Format format = Format::Pcm24;
  bool dither = true;             // TPDF, 16-bit only
  std::uint64_t ditherSeed = 1;   // same seed gives byte-identical output
  bool normalize = false;         // scale so the peak sits at ceilingDb
  float ceilingDb = -1.f;         // [-60, 0]
};

struct EncodeReport {
  float peak = 0.f;               // input peak before any normalization gain
  float gain = 1.f;               // gain applied by normalization
  std::uint64_t clipped = 0;      // samples clamped when converting to PCM
};

struct Decoded {
  int sampleRate = 0;
  Format format = Format::Pcm16;
  std::vector<std::vector<float>> planar;
};

constexpr std::uint64_t kMaxDataBytes = 0xFFFFFFFFull - 64;

Result<std::vector<std::uint8_t>> encode(const std::vector<std::vector<float>>& planar, const Options& options, EncodeReport* report = nullptr);
Result<Decoded> decode(const std::uint8_t* data, std::size_t size);

// Writes to a temporary sibling then renames. Fails with Conflict if `path` exists and !overwrite; never
// leaves the temporary file behind on failure and never touches an existing target on failure.
Status writeFileAtomic(const std::string& path, const std::vector<std::uint8_t>& bytes, bool overwrite);

// Lower-case [a-z0-9_-], at most 48 characters, never empty ("untitled").
std::string sanitizeFileName(const std::string& text);

}  // namespace chopfractal::wav
