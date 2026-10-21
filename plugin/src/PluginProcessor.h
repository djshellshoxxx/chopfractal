#pragma once
// JUCE VST3 shell. This class is glue only: parameters come from the host adapter's manifest, host time
// goes through translateHostTime(), audio goes through Renderer::process(), and project state goes through
// ProjectSession. The audio thread never touches the session or its lock; it reads parameter atomics and
// the renderer's lock-free mailbox only.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <vector>
#include <atomic>
#include <chopfractal/audio_renderer/meter.hpp>
#include <chopfractal/audio_renderer/spsc_queue.hpp>
#include <chopfractal/composition/export_job.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/composition/source_replace.hpp>
#include <chopfractal/input_capture/capture.hpp>
#include <chopfractal/plugin_ui_adapter/capture_view.hpp>
#include <chopfractal/plugin_host_adapter/host_time.hpp>
#include <chopfractal/plugin_host_adapter/parameters.hpp>

#include "NoticeLog.h"

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
  bool acceptsMidi() const override { return true; }  // performance pads (F07); producesMidi and isMidiEffect stay false
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  void setNonRealtime(bool isNonRealtime) noexcept override;
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
  void setStatusMessage(const juce::String& message, Severity severity = Severity::Info);  // also appends non-empty text to the notice log
  void setProgressMessage(const juce::String& message);  // the status line only, not the log: progress ticks would flood it
  Severity statusSeverity() const;
  NoticeLog& noticeLog() { return noticeLog_; }
  chopfractal::render::MeterReading takeMeter() { return renderer_.meter().take(); }
  float takeLimiterGr() { return renderer_.limiterTelemetry().takeGr(); }  // UI thread: most negative gain reduction (dB, <= 0) since the last call  // UI thread: the maximum since the last call

  std::atomic<bool> embedSource{false};
  std::atomic<double> manualBpm{120.0};
  std::atomic<bool> hostPlaying{false};
  std::atomic<bool> usedFallbackTempo{true};
  std::atomic<bool> usedFallbackMeter{true};
  std::atomic<bool> hostHasPosition{false};  // the host gave a song position (ppq) in the last block
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

  // ---- input capture (F12; docs/specs/roadmap/F12-input-capture.md) ----
  // Source limits shared by file decoding and capture (the capture module's constants are asserted equal).
  static constexpr double kMaxSourceSeconds = 60.0;
  static constexpr juce::int64 kMaxDecodedBytes = 100ll * 1024 * 1024;
  chopfractal::capture::CaptureEngine& captureEngine() { return capture_; }
  // Arm: fills the engine settings from the host (input channels, sample rate, tempo, meter). An unkept capture in the
  // Captured state is discarded first (the editor asks the user before calling). Refusals are returned and shown as the
  // status message.
  chopfractal::Status armCapture(chopfractal::capture::CaptureMode mode, int bars, double maxSeconds);
  // Capture button / Stop button / Discard button.
  chopfractal::Status startCapture() { return capture_.startNow(); }
  void stopCapture() { capture_.requestStop(); }
  void discardCapture() { capture_.discard(); capture_.reap(); }
  // Keep: moves the captured audio into the project on a worker, replaces the source (embedded in the project), and, with
  // `chopAfter`, runs Smart Setup. One level of undo (Undo Keep) is stored first.
  void keepCaptureAsync(bool chopAfter);
  bool captureKeepInFlight() const { return keepInFlight_.load(); }
  bool undoKeep();
  bool canUndoKeep() const { return canUndoKeep_.load(); }
  void clearUndoKeep();  // the undo window ends with the next Generate, Mutate, Load or Capture
  bool captureBlocked() const { return captureBlocked_.load(); }  // an offline render is running
  // Tests only: pretend the host gave this many input channels (-1 = use the real count). The bus layout rules never allow
  // fewer than one, so this is the only way to reach the "no input" refusal.
  void overrideInputChannelsForTesting(int channels) { inputOverride_.store(channels); }
  // Tests only: stop the 30 Hz poll so the test decides when pollAudioFlags() runs (wall-clock ticks make a replay non-deterministic).
  void stopPollTimerForTesting() { stopTimer(); }
  // Everything buildCaptureView() needs, read from the engine, the host state and the session (message thread).
  chopfractal::ui::CaptureUiInput captureUiInput(chopfractal::capture::CaptureMode mode, int bars, double maxSeconds);

  // ---- multiple outputs (F11; docs/specs/roadmap/F11-multi-output-stems.md) ----
  // How many of the eight aux outputs ("Out 2" .. "Out 9") the host enabled for the current run, and which ones (bit i is
  // "Out i + 2"). Read on the 30 Hz timer for the Outputs status line and the "off in host" labels.
  int activeAuxBuses() const;
  bool auxBusEnabled(int bus) const { return bus >= 1 && bus <= chopfractal::render::kMaxAuxBuses && ((auxMask_.load() >> (bus - 1)) & 1) != 0; }

  // ---- performance pads (F07; docs/specs/roadmap/F07-performance-pads.md) ----
  // Any thread. The press reaches the audio thread through a fixed-size queue and sounds one block later; a full queue drops it.
  void padTrigger(int note, int velocity, bool on);
  void padAllNotesOff();
  // Project-level pad choices (base note, MIDI channel, quantize, merge); republishes the note bank.
  chopfractal::Status setPadSettings(const chopfractal::composition::PadSettings& settings);
  chopfractal::Status setChokeGroup(chopfractal::ChopId chop, std::uint8_t group);
  chopfractal::composition::PadSettings padSettings();
  // Record: while armed, the transport plays and the host reports a song position, pad presses become take hits. The take
  // merges into the pattern at the next loop midpoint, when Record is switched off, and when the transport stops.
  void setRecordArmed(bool armed);
  bool recordArmed() const { return recordArmed_.load(); }
  void discardTake();
  int takeHits() const { return takeCount_.load(); }
  std::uint64_t takeDropped() const { return takeDropped_.load(); }  // hits lost because the audio-to-UI queue was full
  std::uint64_t padDropped() const { return padInputDropped_.load() + renderer_.padDropped(); }
  // Bit n of word n >> 5 is set when note n started since the last call (MIDI and on-screen pads). Editor timer.
  std::array<std::uint32_t, 4> takePadActivity();
  int activePadVoices() const { return renderer_.activePadVoices(); }

  // Decodes WAV/AIFF (and other basic formats) within the spec's size limits.
  static std::shared_ptr<const chopfractal::render::SourceData> decodeFile(const juce::File& file, juce::String& error);

 private:
  void applyUserAudioDefaults();
  void timerCallback() override { pollAudioFlags(); }
  void syncAudioInfo();
  void restoreAudio(const std::string& path, chopfractal::SourceId id, std::uint64_t ticket);
  void pollPads(bool midpointCrossed);  // message thread: drains the take queue and merges takes
  void mergePendingTake();
  void pollCapture();     // message thread: frees idle capture memory and reports how a capture ended
  void finishKeep(std::uint64_t ticket, std::shared_ptr<const chopfractal::render::SourceData> data, std::string name, bool chopAfter, bool wrapped, double seconds, bool failed);
  void pollOutputStage();  // message thread: gesture-effect settings, level-match measurement, export parameters, latency
  static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

  // Declaration order matters: the renderer must outlive the session that publishes into its mailbox.
  chopfractal::render::Renderer renderer_;
  chopfractal::composition::ProjectSession session_;
  juce::CriticalSection sessionLock_;
  std::array<std::atomic<float>*, chopfractal::host::kParamCount> paramPtrs_{};
  std::array<double, 5> lastGrooveParams_{};  // Groove Lab knobs as of the last poll (message thread): the session is touched only when one moved
  std::atomic<double> sampleRate_{48000.0};
  chopfractal::capture::CaptureEngine capture_;
  static BusesProperties makeBuses();
  std::atomic<int> auxMask_{0};
  // ---- pads ----
  struct UiPad {
    std::uint8_t note = 0, velocity = 0, kind = 0;  // kind: 0 note off, 1 note on, 2 all notes off
  };
  struct TakeEvent {
    double ppq = 0.0;
    std::uint8_t note = 0, velocity = 0;
    bool on = false;
  };
  chopfractal::render::SpscQueue<UiPad, 128> uiPadQueue_;      // any thread -> audio thread
  chopfractal::render::SpscQueue<TakeEvent, 1024> takeQueue_;  // audio thread -> message thread
  std::array<chopfractal::render::PadEvent, chopfractal::render::kMaxPadEventsPerBlock> padEvents_{};  // audio thread
  std::array<std::atomic<std::uint32_t>, 4> padActivity_{};
  std::atomic<int> padChannel_{0};
  std::atomic<bool> recordArmed_{false};
  std::atomic<std::uint64_t> padInputDropped_{0};
  std::atomic<std::uint64_t> takeDropped_{0};
  std::atomic<int> takeCount_{0};
  std::vector<chopfractal::composition::TakeHit> takeBuffer_;  // message thread
  std::array<int, 128> openHit_{};                             // message thread: index of the open hit per note, -1 none
  bool takeFullReported_ = false;
  bool wasPlayingForTake_ = false;
  std::atomic<bool> captureBlocked_{false};
  std::atomic<int> inputOverride_{-1};
  int captureInputChannels() const { const int o = inputOverride_.load(); return o >= 0 ? o : getTotalNumInputChannels(); }
  std::atomic<bool> keepInFlight_{false};
  std::atomic<bool> canUndoKeep_{false};
  chopfractal::capture::CaptureState lastCaptureState_ = chopfractal::capture::CaptureState::Idle;  // message thread
  chopfractal::composition::SourceReplaceUndo keepUndo_;
  bool undoToEmpty_ = false;      // the capture was kept into an empty project: undo clears the source
  bool preKeepEmbed_ = false;
  chopfractal::SourceId keptSourceId_{};
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
  bool lastHostPlaying_ = false;  // message thread: last transport state handed to the scene sequencer
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
  Severity statusSeverity_ = Severity::Info;
  NoticeLog noticeLog_;
  mutable juce::CriticalSection statusLock_;

  JUCE_DECLARE_WEAK_REFERENCEABLE(ChopFractalProcessor)
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChopFractalProcessor)
};
