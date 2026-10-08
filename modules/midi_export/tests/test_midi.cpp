#include <chopfractal/midi_export/midi.hpp>
#include <cmath>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::midi;

CHOP_TEST(round_trip_preserves_notes_tempo_and_meter) {
  FileSpec s;
  s.bpm = 93.5;
  s.numerator = 7;
  s.denominator = 8;
  s.trackName = "ChopFractal";
  s.notes = {{38, 90, 960, 240, 0}, {36, 127, 0, 480, 0}, {42, 1, 3000, 10, 9}};
  auto b = encode(s);
  CHECK(b.ok());
  auto d = decode(b.value().data(), b.value().size());
  CHECK(d.ok());
  if (!d.ok()) return;
  CHECK_NEAR(d.value().bpm, 93.5, 0.01);
  CHECK(d.value().numerator == 7 && d.value().denominator == 8 && d.value().trackName == "ChopFractal");
  CHECK_EQ(d.value().notes.size(), 3u);
  CHECK(d.value().notes[0].note == 36 && d.value().notes[0].start == 0 && d.value().notes[0].duration == 480 && d.value().notes[0].velocity == 127);
  CHECK(d.value().notes[2].note == 42 && d.value().notes[2].channel == 9 && d.value().notes[2].duration == 10);
  CHECK(encode(s).value() == b.value());  // deterministic
}

CHOP_TEST(same_pitch_overlaps_are_clipped_and_note_off_precedes_note_on) {
  FileSpec s;
  s.notes = {{36, 100, 0, 960, 0}, {36, 100, 480, 480, 0}, {36, 100, 480, 480, 0}};
  auto d = decode(encode(s).value().data(), encode(s).value().size());
  CHECK(d.ok());
  if (!d.ok()) return;
  // First note is cut at 480; of the two identical notes starting at 480 the first has zero length and is dropped.
  CHECK_EQ(d.value().notes.size(), 2u);
  CHECK(d.value().notes[0].duration == 480 && d.value().notes[1].start == 480);

  FileSpec t;
  t.notes = {{40, 100, 0, 480, 0}, {40, 100, 480, 480, 0}};  // back to back: off then on at tick 480
  auto e = encode(t).value();
  auto f = decode(e.data(), e.size());
  CHECK(f.ok() && f.value().notes.size() == 2 && f.value().notes[0].duration == 480);
}

CHOP_TEST(invalid_input_is_rejected) {
  FileSpec s;
  s.notes = {{128, 100, 0, 10, 0}};
  CHECK(!encode(s).ok());
  s.notes = {{36, 0, 0, 10, 0}};
  CHECK(!encode(s).ok());
  s.notes = {{36, 100, 0, 0, 0}};
  CHECK(!encode(s).ok());
  s.notes = {{36, 100, -1, 10, 0}};
  CHECK(!encode(s).ok());
  s.notes = {{36, 100, 0, 10, 16}};
  CHECK(!encode(s).ok());
  s.notes.clear();
  s.bpm = 5;
  CHECK(!encode(s).ok());
  s.bpm = 120;
  s.denominator = 3;
  CHECK(!encode(s).ok());
  s.denominator = 4;
  s.notes.assign(kMaxNotes + 1, Note{36, 100, 0, 10, 0});
  CHECK(!encode(s).ok());
}

CHOP_TEST(decoder_survives_truncation_and_corruption) {
  FileSpec s;
  s.notes = {{36, 100, 0, 480, 0}, {38, 100, 480, 480, 0}};
  const auto good = encode(s).value();
  for (std::size_t cut = 0; cut + 1 < good.size(); ++cut) CHECK(!decode(good.data(), cut).ok());
  for (std::size_t i = 0; i < good.size(); ++i) {
    auto bad = good;
    bad[i] ^= 0xFF;
    (void)decode(bad.data(), bad.size());  // must not crash or hang
  }
  CHECK(!decode(nullptr, 0).ok());
}
