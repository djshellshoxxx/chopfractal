#pragma once
// Deterministic Standard MIDI File (format 0) encode/decode for note sequences. 960 ticks per quarter,
// the same grid as the pattern engine, so exported hits land on exact ticks.
#include <chopfractal/chop_contracts/result.hpp>
#include <chopfractal/chop_contracts/time.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::midi {

struct Note {
  int note = 36;       // 0..127
  int velocity = 100;  // 1..127
  Ticks start = 0;
  Ticks duration = 0;
  int channel = 0;     // 0..15
};

// A marker meta event (0xFF 0x06), e.g. a section name.
struct Marker {
  Ticks tick = 0;
  std::string text;  // at most 100 bytes are written
};
constexpr std::size_t kMaxMarkers = 256;

struct FileSpec {
  double bpm = 120.0;  // 20..999
  int numerator = 4;
  int denominator = 4;  // 1, 2, 4, 8, 16 or 32
  std::string trackName;
  std::vector<Note> notes;
  std::vector<Marker> markers;  // written before the notes of the same tick
  Ticks endTick = 0;            // end of track is placed here when it lies after the last event (0 = right after the last event)
};

constexpr std::size_t kMaxNotes = 65536;

// Same-pitch overlaps are clipped to the next note's start; notes left with no length are dropped.
Result<std::vector<std::uint8_t>> encode(const FileSpec& spec);
// Reads files produced by encode() (and ordinary single-track format 0 files at 960 ticks per quarter).
Result<FileSpec> decode(const std::uint8_t* data, std::size_t size);

}  // namespace chopfractal::midi
