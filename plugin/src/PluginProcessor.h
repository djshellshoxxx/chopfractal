#pragma once
// JUCE VST3 shell. This class is glue only: parameters come from the host adapter's manifest, host time
// goes through translateHostTime(), audio goes through Renderer::process(), and project state goes through
// ProjectSession. The audio thread never touches the session or its lock; it reads parameter atomics and
// the renderer's lock-free mailbox only.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/input_capture/capture.hpp>
#include <chopfractal/plugin_host_adapter/host_time.hpp>
#include <chopfractal/plugin_host_adapter/parameters.hpp>

#include "CaptureController.h"
#include "PadInput.h"
#include "StretchController.h"

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
  bool acceptsMidi() const override { return true; }
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
  CaptureController& captureController() { return capture_; }
  // ---- live pads ----
  StretchController& stretchController() { return stretch_; }
  int activeAuxBuses() const { return auxBuses_.load(std::memory_order_relaxed); }  // aux stereo outputs the host enabled
  PadInput& pads() { return pads_; }
  void padTrigger(int note, int velocity, bool on) { pads_.trigger(note, velocity, on); }
  void setRecordArmed(bool armed);
  void discardTake();
  void flushTake(const char* why = nullptr);  // merges the recorded take into the pattern (one undo step)
  juce::ThreadPool& workerPool() { return loadPool_; }
  double currentSampleRate() const { return sampleRate_.load(std::memory_order_relaxed); }
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

  static constexpr double kMaxSourceSeconds = 60.0;
  static constexpr juce::int64 kMaxDecodedBytes = 100ll * 1024 * 1024;

  // Decodes WAV/AIFF (and other basic formats) within the spec's size limits.
  static std::shared_ptr<const chopfractal::render::SourceData> decodeFile(const juce::File& file, juce::String& error);

 private:
  static BusesProperties makeBusLayout();
  void timerCallback() override { pollAudioFlags(); }
  void syncAudioInfo();
  static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

  // Declaration order matters: the renderer must outlive the session that publishes into its mailbox.
  chopfractal::render::Renderer renderer_;
  chopfractal::composition::ProjectSession session_;
  CaptureController capture_{*this};
  PadInput pads_;
  StretchController stretch_{*this};
  juce::CriticalSection sessionLock_;
  std::array<std::atomic<float>*, chopfractal::host::kParamCount> paramPtrs_{};
  std::atomic<double> sampleRate_{48000.0};
  std::atomic<int> auxBuses_{0};
  std::atomic<double> patternQuarters_{0.0};
  bool sceneTransportPlaying_ = false;  // message thread: last host play state passed to the scene sequencer
  std::atomic<std::uint32_t> boundaryCount_{0};
  std::atomic<std::uint32_t> midpointCount_{0};  // set by the audio thread at each loop midpoint (drives Evolve)
  std::atomic<double> playheadQuarters_{-1.0};
  // Zero follows the DAW. Later commands carry a sequence and a playing bit. Only the audio
  // thread resets the renderer/clock; even Stop then Play between callbacks restarts at zero.
  std::atomic<std::uint64_t> transportCommand_{0};
  std::uint64_t appliedTransportCommand_ = 0;
  double freeRunQuarters_ = 0.0;  // audio thread only: local clock for hosts without a position
  juce::ThreadPool loadPool_{juce::ThreadPool::Options{}.withNumberOfThreads(1)};  // file decoding; drained in the destructor
  std::atomic<std::uint64_t> loadTicket_{0};
  juce::String status_;
  mutable juce::CriticalSection statusLock_;

  JUCE_DECLARE_WEAK_REFERENCEABLE(ChopFractalProcessor)
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChopFractalProcessor)
};
