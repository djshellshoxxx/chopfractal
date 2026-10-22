#pragma once
// Performance pads (F07): MIDI notes (or on-screen pads) trigger chops on top of the running pattern. These are the
// portable types; the voices live in the renderer. Notes map to chops through an immutable PadBank that the composition
// root publishes through Renderer::padMailbox().
#include <chopfractal/audio_renderer/gestures.hpp>
#include <chopfractal/chop_contracts/chop.hpp>

#include <array>
#include <cstdint>

namespace chopfractal::render {

constexpr int kPadNotes = 128;
constexpr int kMaxPadEventsPerBlock = 256;

enum class PadMode : std::uint8_t { Poly = 0, Mono = 1, Gate = 2 };
enum class VelocityCurve : std::uint8_t { Linear = 0, Soft = 1, Hard = 2, Fixed = 3 };

// x = clamp(v, 1, 127) / 127: Linear x, Soft sqrt(x), Hard x * x, Fixed 1. Velocity 127 is exactly 1.0 for every curve.
float velocityGain(VelocityCurve curve, int velocity);

struct PadEvent {
  enum class Type : std::uint8_t { NoteOn, NoteOff, AllNotesOff };
  std::int32_t frame = 0;  // within the block; events must be sorted by frame
  Type type = Type::NoteOn;
  std::uint8_t note = 0;
  std::uint8_t velocity = 100;
};

struct PadParams {
  PadMode mode = PadMode::Poly;
  VelocityCurve curve = VelocityCurve::Linear;
};

// One MIDI note: the region it plays, the chop's own fades (0 = renderer defaults) and a choke group (0 = none, 1..4).
struct PadSlot {
  SampleRange region;  // region.empty() = unmapped
  std::uint32_t fadeInFrames = 0, fadeOutFrames = 0;
  std::uint8_t chokeGroup = 0;
};
struct PadBank {
  std::array<PadSlot, kPadNotes> slots{};  // indexed by MIDI note; immutable once published
};

// What the audio thread is given next to the host block. Pointers are only read during the call.
struct LiveInput {
  const PadEvent* pads = nullptr;
  int numPads = 0;
  PadParams padParams;
  const GestureEvent* gestures = nullptr;  // F08, sorted by frame
  int numGestures = 0;
  GestureParams gestureParams;
};

}  // namespace chopfractal::render
