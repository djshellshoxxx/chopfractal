#pragma once
// Types for playing chops live (MIDI notes or on-screen pads): events, behavior, and the note -> chop map.
#include <chopfractal/chop_contracts/chop.hpp>

#include <array>
#include <cstdint>

namespace chopfractal::render {

constexpr int kPadNotes = 128;
constexpr int kMaxPadEventsPerBlock = 256;

enum class PadMode : std::uint8_t { Poly = 0, Mono = 1, Gate = 2 };
enum class VelocityCurve : std::uint8_t { Linear = 0, Soft = 1, Hard = 2, Fixed = 3 };

// v is clamped to 1..127; x = v / 127. Linear x, Soft sqrt(x), Hard x*x, Fixed 1.
float velocityGain(VelocityCurve curve, int velocity);

struct PadEvent {
  enum class Type : std::uint8_t { NoteOn, NoteOff, AllNotesOff };
  std::int32_t frame = 0;  // within the host block; events must be sorted by frame
  Type type = Type::NoteOn;
  std::uint8_t note = 0;
  std::uint8_t velocity = 100;
};

struct PadParams {
  PadMode mode = PadMode::Poly;
  VelocityCurve curve = VelocityCurve::Linear;
};

struct PadSlot {
  SampleRange region;  // empty() = unmapped
  std::uint32_t fadeInFrames = 0;
  std::uint32_t fadeOutFrames = 0;
  std::uint8_t chokeGroup = 0;  // 0 = none
};

struct PadBank {
  std::array<PadSlot, kPadNotes> slots{};  // indexed by MIDI note; immutable once published
};

// What the plugin hands the renderer for one host block.
struct LiveInput {
  const PadEvent* pads = nullptr;
  int numPads = 0;
  PadParams padParams;
};

}  // namespace chopfractal::render
