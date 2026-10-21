#pragma once
// JUCE VST3 shell. This class is glue only: parameters come from the host adapter's manifest, host time
// goes through translateHostTime(), audio goes through Renderer::process(), and project state goes through
// ProjectSession. The audio thread never touches the session or its lock; it reads parameter atomics and
// the renderer's lock-free mailbox only.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <chopfractal/composition/export_job.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/plugin_host_adapter/host_time.hpp>
#include <chopfractal/plugin_host_adapter/parameters.hpp>

// A project export that runs on a worker thread (F01 section 5).
struct ExportRequest {
  enum class Kind { Wav, Kit } kind = Kind::Wav;
  std::string path;                       // the WAV file, or the kit directory
  chopfractal::composition::ExportWavOptions wav;
  chopfractal::composition::KitOptions kit;
};
struct ExportStatus {
  enum class State { Idle, Running, Done, Failed, Cancelled } state = State::Idle;
  float fraction = 0.f;
};

class ChopFractalProcessor : public juce::AudioProcessor, private juce::Timer {
 public:
  ChopFractalProcessor();
  ~ChopFractalProcessor() override;

  // ---- juce::AudioProcessor ----
  const juce::String getName() const override { return "ChopFractal"; }
  void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
  void releaseResources() override {}
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  using juce::AudioProcessor::processBlock;
  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return "Default"; }  // VST3 requires every advertised program to have a name
  void changeProgramName(int, const juce::String&) override {}
  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  // ---- shared with the editor (message thread) ----
  juce::AudioProcessorValueTreeState apvts;

  // Runs `f(session)` under the session lock, then refreshes the audio-side hints derived from it.
  template <class F>
  void withSession(F&& f) {
    const juce::ScopedLock lock(sessionLock_);
    f(session_);
    syncAudioInfo();
  }
  chopfractal::host::ParamValues currentParams() const;
  void pollAudioFlags();  // acts on loop-boundary / loop-midpoint events from the audio thread (also called by tests)
  void loadFileAsync(const juce::File& file, bool relink = false);  // relink: re-attach the audio of a project whose file went missing                 // decodes on a worker thread, then loads on the message thread
  bool startPlayback();  // local preview; validates that a generated pattern and its source exist
  void stopPlayback();
  void followHostTransport();  // return to normal DAW transport after local preview
  void audition(chopfractal::ChopId chop);                    // preview a chop through the output
  juce::String statusMessage() const;
  void setStatusMessage(const juce::String& message);

  std::atomic<bool> embedSource{false};
  std::atomic<double> manualBpm{120.0};
  std::atomic<bool> hostPlaying{false};
  std::atomic<bool> usedFallbackTempo{true};
  std::atomic<bool> usedFallbackMeter{true};
  std::atomic<int> hostBeatsPerBar{4};   // the meter the host last reported (4/4 when it gives none)
  std::atomic<int> hostBeatUnit{4};
  std::atomic<double> activeBpm{120.0};        // tempo the last block used (host tempo, else the manual fallback)
  // Position within the pattern in quarter notes (for the Orbit View playhead); negative when not playing.
  double playheadQuarters() const { return playheadQuarters_.load(std::memory_order_relaxed); }

  // ---- export on a worker thread (message thread unless noted) ----
  // Plans under the session lock, then renders and writes on a juce::Thread. Conflict "An export is already running."
  // while one runs; planning errors (nothing to render, file exists, ...) are returned and nothing starts. The final
  // outcome arrives as the status message and as exportStatus().state (Done, Failed or Cancelled).
  chopfractal::Status startExport(const ExportRequest& request);
  void cancelExport();                // asks the worker to stop; no partial files remain
  ExportStatus exportStatus() const;  // any thread (atomics)
  juce::String exportDisplayName() const;  // file or directory name of the current or last export
  // A reference-only audio restore is decoding on a worker (setStateInformation returned before it finished).
  bool restoreInFlight() const { return restoresRunning_.load() > 0; }
  // Number of decodeFile calls made by the calling thread so far (lets tests prove setStateInformation does no decoding).
  static int decodeCallsOnThisThread();

  // Decodes WAV/AIFF (and other basic formats) within the spec's size limits.
  static std::shared_ptr<const chopfractal::render::SourceData> decodeFile(const juce::File& file, juce::String& error);

 private:
  void timerCallback() override { pollAudioFlags(); }
  void syncAudioInfo();
  void restoreAudio(const std::string& path, chopfractal::SourceId id, std::uint64_t ticket);
  void pollOutputStage();  // message thread: gesture-effect settings, level-match measurement, export parameters, latency
  static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

  // Declaration order matters: the renderer must outlive the session that publishes into its mailbox.
  chopfractal::render::Renderer renderer_;
  chopfractal::composition::ProjectSession session_;
  juce::CriticalSection sessionLock_;
  std::array<std::atomic<float>*, chopfractal::host::kParamCount> paramPtrs_{};
  std::array<double, 5> lastGrooveParams_{};  // Groove Lab knobs as of the last poll (message thread): the session is touched only when one moved
  std::atomic<double> sampleRate_{48000.0};
  // pollOutputStage state (message thread only).
  chopfractal::pattern::FxPassSettings appliedFx_;
  chopfractal::render::PlaybackOptions appliedOptions_;
  bool fxApplied_ = false;
  struct MatchKey {
    const void* playback = nullptr;
    double trim = 0, ceiling = 0, bpm = 0;
    bool on = false, limiter = false;
    bool operator==(const MatchKey& o) const { return playback == o.playback && trim == o.trim && ceiling == o.ceiling && bpm == o.bpm && on == o.on && limiter == o.limiter; }
  } matchKey_;
  int reportedLatency_ = 0;
  std::atomic<double> patternQuarters_{0.0};
  std::atomic<std::uint32_t> boundaryCount_{0};
  std::atomic<std::uint32_t> midpointCount_{0};  // set by the audio thread at each loop midpoint (drives Evolve)
  std::atomic<double> playheadQuarters_{-1.0};
  // Zero follows the DAW. Later commands carry a sequence and a playing bit. Only the audio
  // thread resets the renderer/clock; even Stop then Play between callbacks restarts at zero.
  std::atomic<std::uint64_t> transportCommand_{0};
  std::uint64_t appliedTransportCommand_ = 0;
  double freeRunQuarters_ = 0.0;  // audio thread only: local clock for hosts without a position
  juce::ThreadPool loadPool_{juce::ThreadPool::Options{}.withNumberOfThreads(1)};  // file decoding; drained in the destructor
  juce::ThreadPool restorePool_{juce::ThreadPool::Options{}.withNumberOfThreads(1)};  // audio of a reference-only project; own pool so Locate Source never waits behind it
  std::atomic<int> restoresRunning_{0};
  // The "epoch" of every project replacement: setStateInformation and loadFileAsync bump it, and a worker result
  // that finds a different value was superseded and is dropped.
  std::atomic<std::uint64_t> loadTicket_{0};
  class ExportWorker;
  void finishExport(const chopfractal::Result<chopfractal::composition::ExportReport>& result);
  std::unique_ptr<ExportWorker> exportWorker_;
  chopfractal::composition::ExportProgress exportProgress_;
  std::atomic<int> exportState_{static_cast<int>(ExportStatus::State::Idle)};
  ExportRequest::Kind exportKind_ = ExportRequest::Kind::Wav;
  juce::String exportName_;
  mutable juce::CriticalSection exportLock_;
  juce::String status_;
  mutable juce::CriticalSection statusLock_;

  JUCE_DECLARE_WEAK_REFERENCEABLE(ChopFractalProcessor)
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChopFractalProcessor)
};
