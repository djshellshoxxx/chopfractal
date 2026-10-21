#pragma once
// F18 Arrangement export, the model: an ordered list of scene sections (bars x repeats each) plus the export options,
// its project-state encoding (section tag 16) and the helpers that fill it from the scene list. No audio and no threads
// here; the render job is in arrangement_job.hpp. Portable-style: only contracts and the scenes module.
#include <chopfractal/chop_contracts/result.hpp>
#include <chopfractal/scenes/scenes.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::composition {

constexpr int kMinArrangementBars = 8;
constexpr int kMaxArrangementBars = 32;
constexpr std::size_t kMaxArrangementSections = 32;
constexpr int kMaxSectionRepeats = 8;
constexpr std::uint16_t kArrangementSchema = 1;
constexpr double kArrangementTailSeconds = 2.0;
constexpr double kArrangementTailFadeSeconds = 0.05;
// Per channel; 32 bars at 60 BPM and 96 kHz plus the tail fit. Larger is refused (spec section 6, "Size & limits").
constexpr std::int64_t kMaxArrangementFrames = 96000LL * 140;

struct ArrangementSection {
  std::uint32_t sceneId = 0;
  std::uint8_t bars = 4;     // 1, 2, 4 or 8
  std::uint8_t repeats = 1;  // 1..8
  friend bool operator==(const ArrangementSection& a, const ArrangementSection& b) {
    return a.sceneId == b.sceneId && a.bars == b.bars && a.repeats == b.repeats;
  }
};

enum class Transition : std::uint8_t { Cut = 0, Crossfade10 = 1, Crossfade50 = 2 };

struct Arrangement {
  std::vector<ArrangementSection> sections;  // 1..32 entries
  Transition transition = Transition::Crossfade10;
  bool fadeOut = false;      // cosine fade over the last bar; replaces the tail
  bool tail = true;          // append the 2.0 s decay of the last hit
  bool writeMidi = true;
  bool writeMarkers = true;  // the cue-list file, WAV cue chunk and MIDI markers

  int totalBars() const;  // sum(bars * repeats)
  bool valid() const;     // sections non-empty and 8 <= totalBars <= 32
  bool isDefault() const; // no sections and every option at its default
  friend bool operator==(const Arrangement& a, const Arrangement& b) {
    return a.sections == b.sections && a.transition == b.transition && a.fadeOut == b.fadeOut && a.tail == b.tail &&
           a.writeMidi == b.writeMidi && a.writeMarkers == b.writeMarkers;
  }
};

// Field checks for what can be stored: at most 32 sections, bars in {1,2,4,8}, repeats 1..8, known transition. The total
// may be outside 8..32 (such a list is stored and shown, but cannot be exported).
Status validateStored(const Arrangement&);

// Tag 16 payload (without the u16 schema the section API adds): u8 version=1, u8 flags (bit0 fadeOut, bit1 tail, bit2
// writeMidi, bit3 writeMarkers), u8 transition, u8 count, then count x {u32 sceneId, u8 bars, u8 repeats}. Empty for
// the default arrangement (the section is then absent from the project).
std::vector<std::uint8_t> serialize(const Arrangement&);
Result<Arrangement> deserialize(const std::uint8_t* data, std::size_t size);

// The bar counts the section picker offers for a scene whose pattern has `patternBars` bars: the multiples of it in
// {1,2,4,8}; when there is none (a 3-bar or a 16-bar pattern) all four, and the render loops or truncates.
std::vector<int> allowedBars(int patternBars);
// The bars a new section for such a scene starts with: the smallest allowed value of at least 4, else the largest.
int defaultBars(int patternBars);
// Info line for a section that cuts its pattern short (empty when it does not): "Scene 2 has 4 bars; using the first 2".
std::string truncationNote(int sceneNumber, int patternBars, int sectionBars);

// 16 bars from the scenes round-robin, about 4 bars each (defaultBars), options untouched. Empty list gives no sections.
Arrangement autoFill16(const scenes::SceneList&, const Arrangement& keepOptions = {});
// The scene chain (list order following each scene's Next: Advance goes on, Repeat and Stop end it, Jump follows the
// target until a scene repeats) with repeats = loops (1..8) and defaultBars, trimmed so the total never passes 32 bars.
Arrangement fromSceneChain(const scenes::SceneList&, const Arrangement& keepOptions = {});

}  // namespace chopfractal::composition
