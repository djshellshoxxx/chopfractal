#pragma once
// Project-wide hard limits shared by all modules (spec: pattern-generation-engine, recursive-hit-zoom).
#include <cstddef>
#include <cstdint>

namespace chopfractal::limits {

constexpr std::size_t kMaxEventsPerPattern = 512;  // flattened events across the whole pattern
constexpr int kMaxVoices = 8;
constexpr int kMaxBars = 8;
constexpr std::size_t kMaxChops = 256;  // spec requires >= 128 editable chops
constexpr int kDefaultNestedDepth = 2;  // default recursion maximum for Recursive Hit Zoom
constexpr int kMaxNestedDepth = 3;      // configurable hard maximum
constexpr int kMaxRetrigger = 8;
constexpr float kMaxPitchSemitones = 24.f;
constexpr float kMaxLevel = 4.f;
constexpr float kMaxGlideSemitones = 24.f;

}  // namespace chopfractal::limits
