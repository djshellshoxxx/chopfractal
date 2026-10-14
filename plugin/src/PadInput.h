#pragma once
// Live pad input for the processor: MIDI notes and on-screen pad presses become PadEvents for the renderer, and (when
// recording) timestamped take events that the message thread later pairs into hits. Lock-free between threads.
#include <chopfractal/audio_renderer/pads.hpp>
#include <chopfractal/audio_renderer/spsc_queue.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <vector>

class PadInput {
 public:
  PadInput() { takeBuffer_.reserve(kMaxTakeHits); }

  // ---- message thread ----
  bool trigger(int note, int velocity, bool on);  // false when the queue is full
  void setChannel(int channel) { channel_.store(channel, std::memory_order_relaxed); }
  void setRecordArmed(bool armed) { recordArmed_.store(armed, std::memory_order_relaxed); }
  bool recordArmed() const { return recordArmed_.load(std::memory_order_relaxed); }
  void pollTake();                                         // drains the take queue into the take buffer
  std::vector<chopfractal::composition::TakeHit> takeHits();  // moves the waiting hits out
  int waitingHits() const { return static_cast<int>(takeBuffer_.size()); }
  void discardTake();
  std::uint32_t activity(int word) { return activity_[static_cast<std::size_t>(word) & 3].exchange(0, std::memory_order_relaxed); }
  bool takeBufferWasFull() { return bufferFull_.exchange(false); }
  std::uint32_t droppedEvents() const { return dropped_.load(std::memory_order_relaxed); }
  bool noPositionWhileArmed() { return noPosition_.exchange(false); }
  void noPositionFlag() { noPosition_.store(true, std::memory_order_relaxed); }  // audio thread

  // ---- audio thread ----
  // Returns this block's pad events (sorted by frame). `recordPpq` < 0 means the take is not being recorded.
  const chopfractal::render::PadEvent* collect(const juce::MidiBuffer& midi, int frames, double recordPpq, double bpm, double sampleRate, int& count);

  static constexpr std::size_t kMaxTakeHits = 4096;

 private:
  struct UiEvent {
    std::uint8_t note = 0, velocity = 0;
    bool on = false;
  };
  struct TakeEvent {
    double ppq = 0.0;
    std::uint8_t note = 0, velocity = 0;
    bool on = false;
  };

  void pushTake(double ppq, std::uint8_t note, std::uint8_t velocity, bool on);

  chopfractal::render::SpscQueue<UiEvent, 128> ui_;
  chopfractal::render::SpscQueue<TakeEvent, 1024> take_;
  std::array<chopfractal::render::PadEvent, chopfractal::render::kMaxPadEventsPerBlock> events_{};
  std::array<std::atomic<std::uint32_t>, 4> activity_{};
  std::atomic<int> channel_{0};
  std::atomic<bool> recordArmed_{false};
  std::atomic<std::uint32_t> dropped_{0};
  std::atomic<bool> bufferFull_{false};
  std::atomic<bool> noPosition_{false};
  std::vector<chopfractal::composition::TakeHit> takeBuffer_;
  std::array<int, 128> open_{};  // index into takeBuffer_ of the latest unreleased hit per note, -1 none
  bool openInit_ = false;
};
