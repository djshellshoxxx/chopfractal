#pragma once
// Tempo-match stretch types for the composition root (docs/specs/roadmap/F14-tempo-match-stretch.md).
// The heavy part (stretch::stretch) runs on a worker over the immutable snapshot in StretchPlan::input; the
// session only plans (message thread, cheap) and applies (message thread, short, all-or-nothing).
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/chop_contracts/ids.hpp>
#include <chopfractal/chop_contracts/result.hpp>
#include <chopfractal/time_stretch/stretch.hpp>

#include <chopfractal/chop_insight/insight.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chopfractal::composition {

struct StretchRequest {
  double sourceBpm = 120.0;
  double targetBpm = 120.0;
  stretch::Quality quality = stretch::Quality::Standard;
};

struct StretchPlan {
  stretch::Plan plan;                                // regions from 0, enabled markers, in
  std::shared_ptr<const render::SourceData> input;  // immutable snapshot taken at plan time
  SourceId inputId;                                  // identity check at apply time
  StretchRequest request;
};

// Runs the stretch on the calling thread (worker). Returns the new source buffer (same rate and channels,
// plan.outFrames frames), or Cancelled / LimitExceeded from the engine.
Result<std::shared_ptr<const render::SourceData>> runStretch(const StretchPlan& plan, const stretch::Progress& progress = {});

constexpr double kMinStretchBpm = 20.0;
constexpr double kMaxStretchBpm = 999.0;
constexpr double kSameTempoEpsilon = 0.05;         // BPM values closer than this are "already at the target tempo"
constexpr std::int64_t kMaxStretchSeconds = 60;
constexpr std::size_t kMaxStretchBytes = 100u * 1024u * 1024u;  // input and output are each limited to this

// Largest permitted stretched length in frames: min(60 s, the source frame cap, 100 MiB of float samples).
std::int64_t maxStretchFrames(int sampleRate, int channels);

// Live text for the "Tempo match" line (pure, so the UI is thin and the strings are tested here).
struct StretchPreview {
  enum class Level : std::uint8_t { Hidden, Ok, Warning, Error };
  Level level = Level::Ok;
  bool canApply = false;
  std::string text;             // "96.0 -> 120.0 BPM: 125% speed, length x0.800 (8.0 s -> 6.4 s)" or the problem
  std::string advice;           // amber: "Large change: High quality is recommended."
  double ratio = 1.0;           // outFrames / inFrames
  std::int64_t outFrames = 0;
};
StretchPreview previewStretch(double sourceBpm, double targetBpm, std::int64_t inFrames, int sampleRate, int channels);

// Entries of the "Loop length" combo: up to 3 loop suggestions (text = LoopSuggestion::note), then 1, 2, 4, 8 and
// 16 bars, then Custom BPM. Choosing a bar count sets the source BPM to bars * 4 * 60 / seconds (4/4, like suggestLoop).
struct LoopLengthChoice {
  enum class Kind : std::uint8_t { Suggestion, Bars, Custom };
  Kind kind = Kind::Custom;
  std::string text;
  int bars = 0;
  double bpm = 0;  // 0 for Custom
};
double sourceBpmForBars(int bars, double seconds);
std::vector<LoopLengthChoice> loopLengthChoices(const std::vector<insight::LoopSuggestion>& suggestions, double seconds);

// "loop @120.0 BPM" -> true: the name carries a stretch suffix, so stretching again compounds artifacts.
bool looksStretched(const std::string& sourceName);

}  // namespace chopfractal::composition
