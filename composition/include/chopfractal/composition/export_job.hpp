#pragma once
// A planned export that can run on any thread. Planning (message thread, under the session lock) validates the
// request exactly like ProjectSession::exportWav/exportKit and snapshots the immutable data the export needs;
// run() then renders, encodes and writes without touching the session, reports progress and honours a cancel flag.
// No threads are created here: the plugin shell owns the worker (docs/specs/roadmap/F01 section 5).
#include <chopfractal/composition/project_session.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace chopfractal::composition {

struct ExportProgress {
  std::atomic<float> fraction{0.f};  // 0..1, written by run(), read by anyone
  std::atomic<bool> cancel{false};   // set by anyone; run() stops at its next check and returns ErrorCode::Cancelled
  // Optional, set before run(): called on the running thread after every fraction update (tests use it to cancel at an exact point).
  std::function<void(float)> observer;
};

class ExportJob {
 public:
  enum class Kind { Wav, Kit };

  // Same validation, same order and same messages as ProjectSession::exportWav / exportKit.
  static Result<ExportJob> planWav(const ProjectSession& session, std::string path, const ExportWavOptions& options);
  static Result<ExportJob> planKit(const ProjectSession& session, std::string directory, const KitOptions& options);

  // WAV: render (fraction 0 to 0.9), encode (0.95), atomic write (1.0); a cancel before the write leaves no file.
  // Kit: fraction = files written / (n + 2); a cancel is checked before each file and removes the files this run
  // created (files that existed before and were overwritten stay), then returns Cancelled.
  Result<ExportReport> run(ExportProgress& progress) const;

  Kind kind() const { return kind_; }
  const std::string& displayName() const { return name_; }  // file name (WAV) or directory name (kit)
  const std::string& target() const { return target_; }     // the path planned
  std::size_t fileCount() const { return kind_ == Kind::Wav ? 1 : targets_.size(); }

 private:
  ExportJob() = default;
  Result<ExportReport> runWav(ExportProgress&) const;
  Result<ExportReport> runKit(ExportProgress&) const;

  Kind kind_ = Kind::Wav;
  std::string target_, name_;
  ExportWavOptions wavOptions_;
  KitOptions kitOptions_;
  std::shared_ptr<const render::Playback> playback_;      // WAV
  render::RenderParams renderParams_;                     // WAV: output stage as of planning (limiter, match gain, polish, effect mask)
  std::shared_ptr<const render::SourceData> source_;      // kit
  ChopSnapshotPtr chops_;                                 // kit
  std::vector<std::string> targets_;                      // kit: slice files, <name>.mid, kit.txt (full paths)
  std::vector<std::uint8_t> midi_;                        // kit: pattern.mid bytes, rendered while planning
  std::string map_;                                       // kit: kit.txt
};

}  // namespace chopfractal::composition
