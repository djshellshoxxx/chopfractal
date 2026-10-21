#pragma once
// Slice analysis used by the renderer's quality stage (docs/research/BREAK_ENGINE_RESEARCH.md sections 7.1 to 7.3):
// a transient-aware start so attacks are not cut, a coarse acoustic class per slice (kick, snare, hat, cymbal,
// other) that drives fades and voice choke, and the loudness of the first 50 ms after the onset for per-slice
// level matching. Pure functions over immutable source data; message thread only (they allocate nothing but are
// not meant for the audio thread).
#include <chopfractal/audio_renderer/renderer.hpp>

#include <cstdint>
#include <vector>

namespace chopfractal::render {

struct SliceAnalysis {
  SliceClass cls = SliceClass::Other;
  std::int64_t start = 0;      // transient-aware start, never after the requested start, at most 2 ms before it
  bool moved = false;          // start < requested start
  double rms50 = 0.0;          // linear RMS (mono mix) over the first 50 ms after `start`
  double peak10 = 0.0;         // peak over the first 10 ms
};

// `region` is in source frames inside `src`. Invalid or silent regions return the defaults with start = region.start.
SliceAnalysis analyzeSlice(const SourceData& src, SampleRange region);

// Per-class level target: the median rms50 of the class. trims[i] is the linear gain that pulls slice i inside the
// class tolerance (kick +-2 dB, snare +-1.5 dB, hat/cymbal +-2 dB, other +-3 dB, clamped to +-9 dB). A class with fewer
// than two slices, or a silent slice, gets 1.0.
std::vector<float> matchTrims(const std::vector<SliceAnalysis>& slices);

const char* sliceClassName(SliceClass c);

}  // namespace chopfractal::render
