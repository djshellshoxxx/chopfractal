// Minimal consumer: write two notes to a MIDI file in memory and read them back.
#include <chopfractal/midi_export/midi.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::midi;
  FileSpec spec;
  spec.bpm = 96;
  spec.notes = {{36, 100, 0, 480, 0}, {38, 90, 960, 480, 0}};
  auto bytes = encode(spec);
  if (!bytes.ok()) return 1;
  auto back = decode(bytes.value().data(), bytes.value().size());
  if (!back.ok() || back.value().notes.size() != 2) return 1;
  std::printf("midi: %zu bytes\n", bytes.value().size());
  return 0;
}
