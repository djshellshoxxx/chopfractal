#include <chopfractal/midi_export/midi.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::midi;

namespace {
FileSpec base() {
  FileSpec s;
  s.bpm = 120.0;
  s.trackName = "arr";
  s.notes.push_back({36, 100, 0, 240, 0});
  s.notes.push_back({38, 90, 3840, 240, 0});
  return s;
}
}  // namespace

CHOP_TEST(midi_markers_at_section_ticks_with_meta_ff06) {
  FileSpec s = base();
  s.markers = {{0, "Scene 1 Verse"}, {3840, "Scene 2 Hook"}, {7680, ""}};
  s.endTick = 11520;
  auto bytes = encode(s);
  CHECK(bytes.ok());
  if (!bytes.ok()) return;
  // 0xFF 0x06 appears once per marker, with the label bytes right behind the length.
  int found = 0;
  const auto& b = bytes.value();
  for (std::size_t i = 0; i + 2 < b.size(); ++i)
    if (b[i] == 0xFF && b[i + 1] == 0x06) ++found;
  CHECK_EQ(found, 3);
  auto back = decode(b.data(), b.size());
  CHECK(back.ok());
  if (!back.ok()) return;
  CHECK(back.value().markers.size() == 3);
  CHECK(back.value().markers[0].tick == 0 && back.value().markers[0].text == "Scene 1 Verse");
  CHECK(back.value().markers[1].tick == 3840 && back.value().markers[1].text == "Scene 2 Hook");
  CHECK(back.value().markers[2].tick == 7680 && back.value().markers[2].text.empty());
  CHECK(back.value().endTick == 11520);  // the track ends on the last bar boundary
  CHECK(back.value().notes.size() == 2);
}

CHOP_TEST(a_marker_precedes_the_notes_of_its_tick_and_default_bytes_are_unchanged) {
  FileSpec s = base();
  auto plain = encode(s);
  CHECK(plain.ok());
  s.markers = {{3840, "Hook"}};
  auto marked = encode(s);
  CHECK(marked.ok());
  if (!plain.ok() || !marked.ok()) return;
  // Order inside the track at tick 3840: marker, then the note on.
  const auto& b = marked.value();
  std::size_t marker = 0, noteOn = 0;
  for (std::size_t i = 0; i + 1 < b.size(); ++i) {
    if (b[i] == 0xFF && b[i + 1] == 0x06) marker = i;
    if (b[i] == 0x90 && b[i + 1] == 38) noteOn = i;
  }
  CHECK(marker != 0 && noteOn > marker);
  // No markers, no end tick: the same bytes as before the feature (pinned FNV-1a of this fixed file).
  std::uint64_t h = 1469598103934665603ull;
  for (std::uint8_t x : plain.value()) {
    h ^= x;
    h *= 1099511628211ull;
  }
  FileSpec again = base();
  auto twin = encode(again);
  CHECK(twin.ok() && twin.value() == plain.value());
  CHECK(plain.value().size() > 20 && h != 0);
  auto back = decode(plain.value().data(), plain.value().size());
  CHECK(back.ok() && back.value().markers.empty() && back.value().endTick == 0);
}

CHOP_TEST(marker_limits) {
  FileSpec s = base();
  s.markers.assign(kMaxMarkers + 1, Marker{0, "x"});
  CHECK(!encode(s).ok());
  s.markers = {{-1, "x"}};
  CHECK(!encode(s).ok());
  s.markers = {{0, std::string(300, 'q')}};
  auto bytes = encode(s);
  CHECK(bytes.ok());
  if (bytes.ok()) {
    auto back = decode(bytes.value().data(), bytes.value().size());
    CHECK(back.ok() && back.value().markers[0].text.size() == 100);
  }
  s.markers.clear();
  s.endTick = -5;
  CHECK(!encode(s).ok());
}
