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
#include <chopfractal/plugin_host_adapter/host_time.hpp>
#include <chopfractal/plugin_host_adapter/parameters.hpp>

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
  void audition(chopfractal::ChopId chop);                    // preview a chop through the output
  juce::String statusMessage() const;
  void setStatusMessage(const juce::String& message);

  std::atomic<bool> embedSource{false};
  std::atomic<double> manualBpm{120.0};
  std::atomic<bool> hostPlaying{false};
  std::atomic<bool> usedFallbackTempo{true};
  std::atomic<bool> usedFallbackMeter{true};
  std::atomic<double> activeBpm{120.0};        // tempo the last block used (host tempo, else the manual fallback)
  // Position within the pattern in quarter notes (for the Orbit View playhead); negative when not playing.
  double playheadQuarters() const { return playheadQuarters_.load(std::memory_order_relaxed); }

  // Decodes WAV/AIFF (and other basic formats) within the spec's size limits.
  static std::shared_ptr<const chopfractal::render::SourceData> decodeFile(const juce::File& file, juce::String& error);

 private:
  void timerCallback() override { pollAudioFlags(); }
  void syncAudioInfo();
  static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

  // Declaration order matters: the renderer must outlive the session that publishes into its mailbox.
  chopfractal::render::Renderer renderer_;
  chopfractal::composition::ProjectSession session_;
  juce::CriticalSection sessionLock_;
  std::array<std::atomic<float>*, chopfractal::host::kParamCount> paramPtrs_{};
  std::atomic<double> sampleRate_{48000.0};
  std::atomic<double> patternQuarters_{0.0};
  std::atomic<bool> boundaryFlag_{false};
  std::atomic<bool> midpointFlag_{false};  // set by the audio thread at each loop midpoint (drives Evolve)
  std::atomic<double> playheadQuarters_{-1.0};
  juce::String status_;
  mutable juce::CriticalSection statusLock_;

  JUCE_DECLARE_WEAK_REFERENCEABLE(ChopFractalProcessor)
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChopFractalProcessor)
};
