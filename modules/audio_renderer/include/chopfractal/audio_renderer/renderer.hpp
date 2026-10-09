#pragma once
// Voice playback and event transforms. Consumes immutable prepared data (SourceData + Playback) and
// writes into caller-provided buffers. No JUCE/VST types cross this API.
//
// Fixed transform order (changing it needs listening tests and a state-version bump):
//   source region -> reverse/read direction -> playback rate (resample + pitch) -> gate/fades
//   -> level/pan -> voice sum -> dry mix + output gain.
// Pan is a balance law (center = unity on both sides, hard pan silences the opposite side); mono
// output is (L + R) / 2. Interpolation is linear. Pitch changes duration (resampling, not time-stretch).
//
// Threading: prepare()/makePlayback()/mailbox().publish() are non-real-time. process() and reset() are
// real-time safe: no allocation, locks, I/O, or logging. requestPreview() may be called from one
// non-RT producer thread.
#include <chopfractal/audio_renderer/mailbox.hpp>
#include <chopfractal/chop_contracts/event.hpp>
#include <chopfractal/chop_contracts/result.hpp>
#include <chopfractal/chop_contracts/time.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace chopfractal::render {

constexpr std::size_t kMaxPreparedEvents = 4096;  // after retrigger expansion

struct SourceData {
  int channels = 1;       // 1 or 2
  int sampleRate = 48000;
  std::int64_t frames = 0;
  std::vector<float> samples;  // planar: all of channel 0, then all of channel 1
  const float* channel(int c) const { return samples.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(frames); }
};
using SourcePtr = std::shared_ptr<const SourceData>;

// Prepared, validated, immutable playback description (retriggers already expanded into sub-events).
struct Playback {
  SourcePtr source;
  FlatEventList events;  // sorted by start
  Ticks lengthTicks = 0; // loop length of the pattern
  // A source is loaded (so chops can be auditioned) but no pattern exists yet: the input passes through
  // unchanged and previews are mixed on top.
  bool passThrough = false;
};
Result<std::shared_ptr<const Playback>> makePlayback(SourcePtr source, const FlatEventList& events, Ticks lengthTicks);
Result<std::shared_ptr<const Playback>> makePassThroughPlayback(SourcePtr source);

struct Config {
  double sampleRate = 48000.0;
  int maxBlock = 2048;
  int maxVoices = 8;
};

struct TransportBlock {
  bool playing = false;
  bool positionValid = false;  // false: free-run from the last position at `bpm`
  double ppq = 0.0;            // quarter notes at the first frame of the block
  double bpm = 120.0;          // sanitized internally; the manual-BPM fallback when the host has none
  bool scheduleEvents = true;  // false: let sounding voices ring out but start nothing new (offline tails)
};

struct RenderParams {
  bool enabled = true;     // false = plain pass-through (host-style bypass handled by the adapter)
  float dryMix = 0.f;      // 0..1 of the current input mixed in alongside the generated playback
  float outputGainDb = 0.f;
};

struct PreviewRequest {
  SampleRange region;
  EventTransform tx;
};

class Renderer {
 public:
  Renderer();
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  void prepare(const Config& config);  // non-RT; allocates all voice and scratch memory
  Mailbox<Playback>& mailbox();

  // `in` may alias `out`. numChannels is 1 or 2. frames may be 0.
  void process(const TransportBlock& transport, const RenderParams& params, const float* const* in, float* const* out,
              int numChannels, int frames);

  bool requestPreview(const PreviewRequest& request);  // false when the queue is full
  void reset();                                         // RT-safe: silences everything immediately
  int activeVoices() const;                             // sounding + fading voices (preview excluded)
  std::uint64_t droppedTriggers() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

struct OfflineSettings {
  double sampleRate = 48000.0;
  double bpm = 120.0;
  int blockSize = 512;
  int cycles = 1;           // how many times the pattern loops
  double tailSeconds = 0.5; // ring-out after the last cycle, without starting new events
  RenderParams params;
};
// Renders through the same Renderer::process path as live playback. Output is planar stereo.
Result<std::vector<std::vector<float>>> renderOffline(std::shared_ptr<const Playback> playback, const OfflineSettings& settings);

}  // namespace chopfractal::render
