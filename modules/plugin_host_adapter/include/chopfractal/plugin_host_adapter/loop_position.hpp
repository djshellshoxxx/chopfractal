#pragma once
// Where the host playhead sits inside the generated pattern's loop, and whether the block that is about to
// play crosses the loop boundary or the loop midpoint.
//
// Extracted unchanged from ChopFractalProcessor::processBlock (plugin/src/PluginProcessor.cpp) so the
// arithmetic can be tested without JUCE. The processor uses the three results as follows:
//   boundary  -> tells the message thread to apply a quantized variation switch (ProjectSession::onLoopBoundary)
//   midpoint  -> tells the message thread to take one Evolve step mid-loop (ProjectSession::evolveStep)
//   playheadQuarters -> drawn by the Orbit View playhead
//
// Contract: pure function, no allocation, no locks, no I/O, so it is safe to call on the audio thread.
// Units: positions in quarter notes (ppq), tempo in beats per minute, rate in Hz.
#include <chopfractal/audio_renderer/renderer.hpp>

namespace chopfractal::host {

struct LoopPosition {
  bool boundary = false;         // the block spans a multiple of the loop length
  bool midpoint = false;         // the block spans the middle of a loop (loop length / 2 plus a multiple)
  double playheadQuarters = -1;  // position within the loop in [0, patternQuarters); -1 when not playing/valid
};

// `patternQuarters` is the loop length in quarter notes; values <= 0 mean "no pattern" (nothing is reported).
// A block is reported only when the host is playing and supplied a valid position.
LoopPosition computeLoopPosition(const render::TransportBlock& block, int frames, double sampleRate, double patternQuarters);

}  // namespace chopfractal::host
