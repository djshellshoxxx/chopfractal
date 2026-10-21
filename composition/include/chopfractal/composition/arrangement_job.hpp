#pragma once
// F18 Arrangement export, the render job. An ArrangementPlan is an immutable snapshot taken on the message thread
// (ProjectSession::startArrangementExport): the scene playbacks, the MIDI notes of each scene pattern, the output stage
// and the file settings. runArrangement() is a pure function of the plan, so the bytes are identical for equal plans and
// live edits cannot reach it. ArrangementJob runs it on one worker thread. The audio thread is never involved.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/chop_contracts/result.hpp>
#include <chopfractal/composition/arrangement.hpp>
#include <chopfractal/midi_export/midi.hpp>
#include <chopfractal/wav_export/wav.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace chopfractal::composition {

struct PlannedSection {
  std::string label;                                  // "Scene 3 Verse": cue label, MIDI marker and cue-list name
  int bars = 4, repeats = 1;                          // as arranged
  int patternBars = 4;                                // bars of the scene's pattern
  std::shared_ptr<const render::Playback> playback;   // the scene's playback; shares the project's source data
  std::vector<midi::Note> notes;                      // one pass of the scene pattern (start within lengthTicks), tick 960 grid
  Ticks lengthTicks = 0;                              // of one pass
};

struct ArrangementPlan {
  std::vector<PlannedSection> sections;
  Arrangement options;
  double bpm = 120.0;                // whole BPM (rounded as playback does)
  wav::Options wav;                  // format and sample rate (the render rate)
  render::RenderParams params;       // output stage as of planning: gain, polish, effect mask, limiter; matchGainDb is ignored
  bool levelMatch = false;           // recompute a match gain per scene (F16) and apply it to that section
  double matchTrimDb = 0.0;
  std::string root;                  // export folder (created when missing)
  std::string name;                  // already sanitized
  std::string trackName;             // MIDI track name
  // Test hook, called on the worker after every progress update with the overall fraction.
  std::function<void(float)> observer;
};

struct ArrangementProgress {
  int section = 0;       // 1-based section being rendered; 0 before the first
  int sections = 0;
  double fraction = 0.0; // 0..1, monotonic
};

struct ArrangementResult {
  Status status;
  std::string wavPath, midiPath, cuePath;  // empty when not written
  int bars = 0;
  double seconds = 0.0;
  std::int64_t frames = 0;
  float peak = 0.f;
  std::uint64_t clipped = 0;
};

// Shared with the job thread; every field is an atomic.
struct ArrangementControl {
  std::atomic<bool> cancel{false};
  std::atomic<int> section{0};
  std::atomic<int> sections{0};
  std::atomic<float> fraction{0.f};
};

// Frames per bar for a whole-BPM tempo at a sample rate: round(60 / bpm * 4 * rate).
std::int64_t barFrames(double bpm, double sampleRate);
// Transition overlap in frames (0 for Cut).
std::int64_t crossfadeFrames(Transition t, double sampleRate);
// Gain of the incoming section at overlap frame i (0 <= i < N); the outgoing tail uses 1 - it, so the pair always sums to 1.
inline float crossfadeGainIn(std::int64_t i, std::int64_t N) { return static_cast<float>(i + 1) / static_cast<float>(N + 1); }
// Total frames of the main part (no tail) and of the whole file for a plan.
std::int64_t mainFrames(const ArrangementPlan&);
std::int64_t totalFrames(const ArrangementPlan&);

// Synchronous: render, assemble, write. Checks `control.cancel` between render blocks and before every file step; a cancel
// or an error removes every file this run made and returns the status in `.status` (ErrorCode::Cancelled for a cancel).
ArrangementResult runArrangement(const ArrangementPlan& plan, ArrangementControl& control);

// Same rendering without any file: the assembled mix as planar stereo (latency-compensated, limited when the plan says so)
// plus the section start frames. For tests and for callers that only need the audio.
struct ArrangementAudio {
  std::vector<std::vector<float>> planar;
  std::vector<std::int64_t> sectionStarts;
  std::vector<float> matchGainDb;  // per section
};
Result<ArrangementAudio> renderArrangement(const ArrangementPlan& plan, ArrangementControl& control);

class ArrangementJob {
 public:
  // Starts the worker thread. The plan is owned by the job.
  static std::unique_ptr<ArrangementJob> start(ArrangementPlan plan);
  ~ArrangementJob();  // cancels and joins
  ArrangementJob(const ArrangementJob&) = delete;
  ArrangementJob& operator=(const ArrangementJob&) = delete;

  ArrangementProgress progress() const;  // atomics, any thread
  bool done() const;
  const ArrangementResult& result();     // joins the worker; valid after done()
  void cancel();                         // the worker stops at its next block
  const std::string& name() const { return plan_.name; }

 private:
  explicit ArrangementJob(ArrangementPlan plan);
  ArrangementPlan plan_;
  ArrangementControl control_;
  ArrangementResult result_;
  std::atomic<bool> done_{false};
  std::thread thread_;
};

}  // namespace chopfractal::composition
