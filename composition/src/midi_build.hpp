#pragma once
// Shared by the Producer Kit and scene export: the MIDI file that replays a playback on chop-numbered notes.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/chop_contracts/chop.hpp>
#include <chopfractal/midi_export/midi.hpp>

#include <algorithm>
#include <cmath>

namespace chopfractal::composition::detail {

inline Result<std::vector<std::uint8_t>> buildPatternMidi(const render::Playback& playback, const ChopSnapshot& chops, TimeSignature ts, double bpm, int baseNote,
                                                           int loops, const std::string& name) {
  midi::FileSpec spec;
  spec.bpm = bpm;
  spec.numerator = ts.numerator;
  spec.denominator = ts.denominator;
  spec.trackName = name.substr(0, 60);
  const std::size_t n = chops.chops.size();
  for (int loop = 0; loop < loops; ++loop)
    for (const FlatEvent& e : playback.events) {
      std::size_t idx = n;
      for (std::size_t i = 0; i < n; ++i)
        if (chops.chops[i].id == e.chop) idx = i;
      if (idx == n) continue;
      midi::Note note;
      note.note = baseNote + static_cast<int>(idx);
      note.velocity = std::min(127, std::max(1, static_cast<int>(std::lround(static_cast<double>(e.tx.level) * 127.0))));
      note.start = static_cast<Ticks>(loop) * playback.lengthTicks + e.start;
      note.duration = e.duration;
      spec.notes.push_back(note);
    }
  return midi::encode(spec);
}

}  // namespace chopfractal::composition::detail
