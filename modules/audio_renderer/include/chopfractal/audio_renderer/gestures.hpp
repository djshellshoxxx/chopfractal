#pragma once
// Live gestures (F08): momentary performance moves applied on top of the pattern while a control is held. Stutter
// freezes the last hit into a retriggered slice, Reverse plays every voice backwards, Tape Stop slows everything to a halt
// and spins it back up on release. They are not host parameters and are never stored in the project.
#include <cstdint>

namespace chopfractal::render {

constexpr int kMaxGestureEventsPerBlock = 64;
constexpr int kStutterRates = 3;  // 0 = 1/8 note, 1 = 1/16, 2 = 1/32

enum class GestureKind : std::uint8_t { Stutter = 0, Reverse = 1, TapeStop = 2 };

struct GestureEvent {
  std::int32_t frame = 0;  // within the block; events must be sorted by frame
  GestureKind kind = GestureKind::Stutter;
  bool on = false;
  std::uint8_t rate = 1;   // Stutter only: index 0..kStutterRates-1
};

struct GestureParams {
  float tapeStopMs = 400.f;  // time to reach a standstill, 20..4000
  float spinUpMs = 150.f;    // time to get back to speed, 20..4000
};

// Ticks of one stutter repeat for a rate index (clamped); 960 ticks are a quarter note.
std::int64_t stutterPeriodTicks(int rateIndex);

// A MIDI continuous controller as a gesture: CC 20 Stutter, 21 Reverse, 22 Tape Stop; value >= 64 is held. Returns false
// for any other controller. `frame` and `rate` are copied into the event.
bool gestureFromCc(int controller, int value, int frame, std::uint8_t rate, GestureEvent& out);

}  // namespace chopfractal::render
