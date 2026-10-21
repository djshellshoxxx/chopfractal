#pragma once
// Voice playback and event transforms. Consumes immutable prepared data (SourceData + Playback) and
// writes into caller-provided buffers. No JUCE/VST types cross this API.
//
// Fixed transform order (changing it needs listening tests and a state-version bump):
//   source region -> reverse/read direction -> playback rate (resample + pitch, then glide) -> crunch -> echo tone
//   -> tremolo -> gate/fades -> filter -> level/pan -> voice sum -> match gain -> dry mix + output gain -> limiter.
// The quality stage (RenderParams::slicePolish), the gesture effects (Echo, Tremolo-Pan, Stutter, Roll) and the
// output stage (match gain, limiter) are all off or absent by default and then leave the v1 output bit-identical.
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
#include <functional>
#include <memory>
#include <vector>

namespace chopfractal::render {

constexpr std::size_t kMaxPreparedEvents = 4096;  // after retrigger expansion

// Coarse acoustic class of a slice (see slice_quality.hpp). Drives fade lengths and voice choke.
enum class SliceClass : std::uint8_t { Other = 0, Kick, Snare, Hat, Cymbal };

// Which gesture effect an extra prepared event belongs to, and the RenderParams::fxMask bits that switch them.
namespace fxbit {
constexpr std::uint8_t kEcho = 1, kTremPan = 2, kStutter = 4, kRoll = 8, kAll = 15;
}

struct SourceData {
  int channels = 1;       // 1 or 2
  int sampleRate = 48000;
  std::int64_t frames = 0;
  std::vector<float> samples;  // planar: all of channel 0, then all of channel 1
  const float* channel(int c) const { return samples.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(frames); }
};
using SourcePtr = std::shared_ptr<const SourceData>;

// Runtime data prepared next to every event of a Playback (same index). Everything here is derived by makePlayback
// from the source audio and the event's effect blocks; the audio thread only reads it.
struct PreparedSlice {
  std::int64_t regionStart = 0;       // transient-aware start (equals the event region start when nothing moved)
  float trim = 1.f;                   // invisible per-slice level match (research 7.3)
  SliceClass cls = SliceClass::Other;
  std::uint8_t fxBit = 0;             // nonzero: an extra event that only plays while that effect is enabled
  std::uint8_t replacedBy = 0;        // nonzero: a base event that stays silent while that effect is enabled
  bool repeat = false;                // a retrigger, stutter or roll repeat: overlaps by design, never choked as a new hit
  bool secondary = false;             // Echo repeat: stolen first, never steals
  bool freezeUnit = false;            // Roll: the region ends one gate length after its start (resolved at voice start)
  float gainJitter = 1.f;             // deterministic per-repeat variation, applied by the quality stage only
  float pitchJitter = 0.f;            // semitones, likewise
  float tailFrac = -1.f;              // Stutter/Roll: release as a fraction of the audible length, < 0 = default
  float panOffset = 0.f;              // added to the event pan (Stutter alternation)
  float pitchOffset = 0.f;            // semitones added to the event pitch (Stutter/Roll pitch ramp)
  float echoLpHz = 0.f;               // Echo tone: one-pole low-pass cutoff, 0 = none
  std::uint8_t tremMode = 255;        // 0 tremolo, 1 pan, 2 both, 255 none
  float tremDepth = 0.f;              // 0..1
  Ticks tremPeriodTicks = 0;
  std::int8_t panDir = 1;             // +1 starts moving right, -1 left
};

// Global fallbacks for blocks that say "use the global value" (E01, E02). Changing them needs a new Playback.
struct PlaybackOptions {
  std::uint8_t echoRate = 1;     // index into kEchoSpacingTicks
  std::uint8_t echoDecay = 55;   // percent of the previous level per repeat
  std::uint8_t echoTone = 50;    // percent
  std::uint8_t tremMode = 2;
  std::uint8_t tremRate = 1;     // index into kTremPeriodTicks
  std::uint8_t tremDepth = 60;   // percent
};

// Prepared, validated, immutable playback description (retriggers and gesture effects already expanded).
struct Playback {
  SourcePtr source;
  FlatEventList events;  // sorted by start
  std::vector<PreparedSlice> slices;  // parallel to events
  Ticks lengthTicks = 0; // loop length of the pattern
  // A source is loaded (so chops can be auditioned) but no pattern exists yet: the input passes through
  // unchanged and previews are mixed on top.
  bool passThrough = false;
};
Result<std::shared_ptr<const Playback>> makePlayback(SourcePtr source, const FlatEventList& events, Ticks lengthTicks,
                                                      const PlaybackOptions& options = PlaybackOptions{});
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
  // Quality stage (research section 7): raised-cosine fades of 0.5 to 5 ms by slice class, transient-aware slice
  // start, per-slice level trim, voice choke between kick/snare/hat tails, per-repeat variation in rolls. Off keeps the
  // v1 fades and voice handling bit for bit.
  bool slicePolish = false;
  std::uint8_t fxMask = fxbit::kAll;  // which gesture effects play (bypass without rebuilding the playback)
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
// Progress callback: receives the fraction done in [0, 1], non-decreasing. Returning false aborts the render.
using ProgressFn = std::function<bool(double)>;
// Renders through the same Renderer::process path as live playback. Output is planar stereo. `progress` (optional) is
// called after every 64th block and once at the end with 1.0; returning false aborts with ErrorCode::Cancelled. The
// callback never changes the rendered samples.
Result<std::vector<std::vector<float>>> renderOffline(std::shared_ptr<const Playback> playback, const OfflineSettings& settings,
                                                      const ProgressFn& progress = {});

}  // namespace chopfractal::render
