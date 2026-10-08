#pragma once
// Translates host playhead data (which may be missing, partial, or invalid) into the renderer's
// transport model, reporting which fallbacks were used so the UI can show them.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/chop_contracts/time.hpp>

#include <cstdint>
#include <optional>

namespace chopfractal::host {

// Every field may be absent; hosts differ. JUCE's AudioPlayHead::PositionInfo maps onto this directly.
struct HostTimeInfo {
  std::optional<double> bpm;
  std::optional<double> ppqPosition;  // quarter notes at the first sample of the block
  std::optional<TimeSignature> timeSignature;
  bool isPlaying = false;
  bool isLooping = false;
  std::optional<double> loopStartPpq;
  std::optional<double> loopEndPpq;
};

struct TimeTranslation {
  render::TransportBlock block;
  TimeSignature timeSignature;  // effective meter
  bool usedFallbackTempo = false;  // manual BPM in use (host tempo absent or invalid)
  bool usedFallbackMeter = false;  // 4/4 assumed
  bool freeRunning = false;        // no usable host position: the renderer advances its own clock
};

// `manualBpm` is the user's fallback tempo. Tempo is never inferred from the imported audio.
TimeTranslation translateHostTime(const HostTimeInfo& host, double manualBpm, TimeSignature fallbackMeter = {});

// Plugin bus layouts the shell accepts: mono or stereo, with matching input and output.
bool isSupportedBusLayout(int inputChannels, int outputChannels);

}  // namespace chopfractal::host
