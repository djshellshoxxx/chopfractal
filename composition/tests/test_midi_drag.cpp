// F13: MIDI drag-out, per-variation export, velocity curves, Producer Kit bytes and the persisted MIDI settings.
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/midi_export/midi.hpp>
#include <chopfractal/wav_export/wav.hpp>

#include <algorithm>
#include <cmath>
#include <set>

#include "session_fixture.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;
using fixture::fnv1a;
using fixture::readFile;

namespace {
std::string tmp(const char* name) { return fixture::tmpDir("cf_midi_drag", name); }

// A session with three variations (generate, generate, mutate) so there are inactive nodes to export.
void threeVariations(ProjectSession& s) {
  CHECK(fixture::ready(s, true, 11, 2));
  CHECK(s.generate(fixture::settings(12, 2)).ok());
  pattern::MutateOptions mo;
  mo.seed = 5;
  mo.amount = 0.5;
  CHECK(s.mutate(mo, nullptr).ok());
}
}  // namespace

// Pinned against the code as it was BEFORE the F13 refactor (spec step 1): default options must never change the file.
CHOP_TEST(kit_pattern_mid_is_byte_identical_after_refactor) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 77, 2));
  KitOptions k;
  k.bpm = 128;
  k.loops = 2;
  auto mid = s.renderMidi(k);
  CHECK(mid.ok() && fnv1a(mid.value()) == 7325608272239143083ull);
  KitOptions k2;
  k2.baseNote = 60;
  auto mid2 = s.renderMidi(k2);
  CHECK(mid2.ok() && fnv1a(mid2.value()) == 2872596221132922558ull);
  const std::string dir = tmp("kit");
  CHECK(s.exportKit(dir, k).ok());
  CHECK(readFile(dir + "/chopfractal.mid") == mid.value());
  // the new entry point with default settings is the same function
  MidiExportOptions mo;
  mo.bpm = 128;
  mo.loops = 2;
  mo.trackName = "chopfractal";
  auto viaNew = s.renderCurrentMidi(mo);
  CHECK(viaNew.ok() && viaNew.value() == mid.value());
}

CHOP_TEST(velocity_curves) {
  CHECK(midiVelocity(0.5f, VelocityCurve::Linear) == 64);   // 63.5 rounds up
  CHECK(midiVelocity(0.5f, VelocityCurve::Soft) == 32);     // 31.75
  CHECK(midiVelocity(0.5f, VelocityCurve::Hard) == 90);     // 127 * 0.7071 = 89.8
  CHECK(midiVelocity(0.5f, VelocityCurve::Fixed, 100) == 100);
  CHECK(midiVelocity(4.0f, VelocityCurve::Linear) == 127);
  CHECK(midiVelocity(4.0f, VelocityCurve::Soft) == 127);
  CHECK(midiVelocity(4.0f, VelocityCurve::Hard) == 127);
  CHECK(midiVelocity(4.0f, VelocityCurve::Fixed, 77) == 77);
  for (VelocityCurve c : {VelocityCurve::Linear, VelocityCurve::Soft}) CHECK(midiVelocity(0.001f, c) == 1);
  CHECK(midiVelocity(0.001f, VelocityCurve::Hard) == 4);  // 127 * sqrt(0.001); the formula wins over the spec's loose "-> 1"
  CHECK(midiVelocity(0.f, VelocityCurve::Linear) == 1);
}

CHOP_TEST(velocity_curve_and_channel_reach_the_file) {
  ProjectSession s;
  CHECK(fixture::ready(s));
  MidiExportOptions o;
  o.curve = VelocityCurve::Fixed;
  o.fixedVelocity = 90;
  o.channel = 9;
  o.baseNote = 60;
  auto bytes = s.renderCurrentMidi(o);
  CHECK(bytes.ok());
  auto dec = midi::decode(bytes.value().data(), bytes.value().size());
  CHECK(dec.ok() && !dec.value().notes.empty());
  for (const midi::Note& n : dec.value().notes) CHECK(n.velocity == 90 && n.channel == 9 && n.note >= 60);
}

CHOP_TEST(variation_midi_matches_activated_pattern_midi) {
  ProjectSession s;
  threeVariations(s);
  for (int i = 0; i < 7; ++i) {  // a ten-node tree
    if (i % 2 == 0) CHECK(s.generate(fixture::settings(100 + static_cast<std::uint64_t>(i), 2)).ok());
    else {
      pattern::MutateOptions mo;
      mo.seed = 200 + static_cast<std::uint64_t>(i);
      mo.amount = 0.4;
      CHECK(s.mutate(mo, nullptr).ok());
    }
  }
  const auto nodes = s.history().listAll();
  CHECK(nodes.size() == 10);
  MidiExportOptions o;
  o.bpm = 133;
  o.loops = 2;
  o.curve = VelocityCurve::Soft;
  for (const auto& n : nodes) {
    auto direct = s.renderVariationMidi(n.id, o);
    CHECK(direct.ok());
    CHECK(s.activate(n.id, Quantize::Immediate).ok());
    auto viaActive = s.renderCurrentMidi(o);
    CHECK(viaActive.ok() && direct.ok() && viaActive.value() == direct.value());
  }
}

CHOP_TEST(variation_midi_decodes_with_expected_notes) {
  ProjectSession s;
  threeVariations(s);
  const auto nodes = s.history().listAll();
  MidiExportOptions o;
  o.bpm = 100;
  o.loops = 3;
  o.baseNote = 40;
  auto bytes = s.renderVariationMidi(nodes.front().id, o);
  CHECK(bytes.ok());
  auto dec = midi::decode(bytes.value().data(), bytes.value().size());
  CHECK(dec.ok());
  // expected: the retrigger-expanded (and effect-expanded) events of that node, once per loop
  CHECK(s.activate(nodes.front().id, Quantize::Immediate).ok());
  const auto& expected = s.playback()->events;
  CHECK(!expected.empty());
  CHECK(std::fabs(dec.value().bpm - 100.0) < 1e-6);
  const int n = static_cast<int>(s.chops()->chops.size());
  for (const midi::Note& note : dec.value().notes) CHECK(note.note >= 40 && note.note < 40 + n);
  // overlaps of the same pitch are clipped, never added: notes <= events x loops
  CHECK(dec.value().notes.size() <= expected.size() * 3);
  CHECK(dec.value().notes.size() + 8 >= expected.size() * 3 / 2);
  Ticks maxEnd = 0;
  for (const midi::Note& note : dec.value().notes) maxEnd = std::max(maxEnd, note.start + note.duration);
  CHECK(maxEnd <= 3 * s.playback()->lengthTicks + 1);
}

CHOP_TEST(deleted_chop_events_are_skipped_and_counted) {
  ProjectSession s;
  threeVariations(s);
  const auto nodes = s.history().listAll();
  const history::NodeId first = nodes.front().id;
  CHECK(s.activate(first, Quantize::Immediate).ok());
  const ChopId victim = pattern::flatten(*s.pattern(), *s.chops()).value().front().chop;
  CHECK(s.activate(nodes.back().id, Quantize::Immediate).ok());
  CHECK(s.editMarkers([&](source::ChopMap& m) { return m.deleteMarker(victim); }).ok());
  int skipped = -1;
  auto bytes = s.renderVariationMidi(first, MidiExportOptions{}, &skipped);
  CHECK(bytes.ok() && skipped > 0);
  auto dec = midi::decode(bytes.value().data(), bytes.value().size());
  CHECK(dec.ok());
  for (const midi::Note& n : dec.value().notes) CHECK(n.note < 36 + static_cast<int>(s.chops()->chops.size()));
}

CHOP_TEST(option_errors_are_reported_with_hints) {
  ProjectSession s;
  threeVariations(s);
  const int n = static_cast<int>(s.chops()->chops.size());
  MidiExportOptions o;
  o.baseNote = 127;
  auto over = s.renderCurrentMidi(o);
  CHECK(!over.ok() && over.error().code == ErrorCode::OutOfRange && over.error().hint == 127 - n + 1);
  o = MidiExportOptions{};
  o.loops = 17;
  CHECK(!s.renderCurrentMidi(o).ok());
  o.loops = 0;
  CHECK(!s.renderCurrentMidi(o).ok());
  o = MidiExportOptions{};
  o.channel = 16;
  CHECK(!s.renderCurrentMidi(o).ok());
  o = MidiExportOptions{};
  o.curve = VelocityCurve::Fixed;
  o.fixedVelocity = 0;
  CHECK(!s.renderCurrentMidi(o).ok());
  auto missing = s.renderVariationMidi(9999, MidiExportOptions{});
  CHECK(!missing.ok() && missing.error().code == ErrorCode::NotFound);
  CHECK(!s.renderVariationWav(9999, ExportWavOptions{}).ok());
  ProjectSession empty;
  CHECK(!empty.renderCurrentMidi(MidiExportOptions{}).ok());
  CHECK(!empty.renderVariationMidi(1, MidiExportOptions{}).ok());
}

CHOP_TEST(rendering_for_drag_never_changes_the_session) {
  ProjectSession s;
  threeVariations(s);
  const auto before = s.saveState(false).value();
  const std::size_t nodes = s.history().listAll().size();
  for (const auto& n : s.history().listAll()) {
    CHECK(s.renderVariationMidi(n.id, MidiExportOptions{}).ok());
    CHECK(s.renderVariationWav(n.id, ExportWavOptions{}).ok());
  }
  CHECK(s.renderKitFiles(KitOptions{}).ok());
  CHECK(s.saveState(false).value() == before);
  CHECK(s.history().listAll().size() == nodes);
  CHECK(!s.hasPendingActivation());
}

CHOP_TEST(variation_wav_equals_exportWav_for_the_active_node) {
  ProjectSession s;
  threeVariations(s);
  ExportWavOptions o;
  o.bpm = 120;
  o.loops = 2;
  const std::string dir = tmp("wav");
  CHECK(s.exportWav(dir + "/a.wav", o).ok());
  const auto active = s.history().active();
  auto var = s.renderVariationWav(active, o);
  auto cur = s.renderCurrentWav(o);
  CHECK(var.ok() && cur.ok() && var.value() == readFile(dir + "/a.wav") && cur.value() == var.value());
  // a different node renders something else, and exports to the same bytes as rendering it
  const auto first = s.history().listAll().front().id;
  CHECK(first != active);
  auto other = s.renderVariationWav(first, o);
  CHECK(other.ok() && other.value() != var.value());
  auto rep = s.exportVariationWav(first, dir + "/b.wav", o);
  CHECK(rep.ok() && readFile(dir + "/b.wav") == other.value() && rep.value().frames > 0);
  CHECK(!s.exportVariationWav(first, dir + "/b.wav", o).ok());  // never overwrites without the flag
}

CHOP_TEST(export_variation_midi_writes_the_rendered_bytes_and_refuses_overwrite) {
  ProjectSession s;
  threeVariations(s);
  const auto first = s.history().listAll().front().id;
  const std::string dir = tmp("varmidi");
  MidiExportOptions o;
  o.loops = 2;
  auto rep = s.exportVariationMidi(first, dir + "/v.mid", o, false);
  CHECK(rep.ok() && rep.value().notes > 0 && rep.value().files.size() == 1);
  CHECK(readFile(dir + "/v.mid") == s.renderVariationMidi(first, o).value());
  CHECK(!s.exportVariationMidi(first, dir + "/v.mid", o, false).ok());
  CHECK(s.exportVariationMidi(first, dir + "/v.mid", o, true).ok());
}

CHOP_TEST(renderKitFiles_equal_exportKit_files) {
  ProjectSession s;
  CHECK(fixture::ready(s));
  KitOptions k;
  k.bpm = 140;
  k.loops = 2;
  k.curve = VelocityCurve::Hard;
  const std::string dir = tmp("kitfiles");
  CHECK(s.exportKit(dir, k).ok());
  auto files = s.renderKitFiles(k);
  CHECK(files.ok());
  CHECK(files.value().size() == s.chops()->chops.size() + 1);
  for (const KitFile& f : files.value()) CHECK(readFile(dir + "/" + f.name) == f.bytes);
  CHECK(files.value().back().name == "chopfractal.mid");
  CHECK(files.value().front().name.rfind("slice_01_", 0) == 0);
}

CHOP_TEST(drag_kit_refuses_more_than_63_chops) {
  ProjectSession s;
  CHECK(fixture::ready(s));
  CHECK(s.editMarkers([&](source::ChopMap& m) -> Status {
         for (int i = 1; i <= 70; ++i)
           if (!m.addMarker(static_cast<std::int64_t>(i) * 1300).ok()) return makeError(ErrorCode::InvalidArgument, "marker");
         return Status{};
       }).ok());
  CHECK(s.chops()->chops.size() > 63);
  auto r = s.renderKitFiles(KitOptions{});
  CHECK(!r.ok() && r.error().code == ErrorCode::LimitExceeded);
}

CHOP_TEST(midi_works_without_audio_but_audio_paths_refuse) {
  ProjectSession a;
  threeVariations(a);
  MidiExportOptions o;
  const auto withAudio = a.renderCurrentMidi(o).value();
  const auto bytes = a.saveState(false).value();
  ProjectSession b;
  CHECK(b.loadState(bytes.data(), bytes.size(), [](const source::SourceInfo&) { return std::shared_ptr<const render::SourceData>(); }).ok());
  CHECK(b.sourceMissing());
  auto m = b.renderCurrentMidi(o);
  CHECK(m.ok());
  auto dec = midi::decode(m.value().data(), m.value().size());
  auto decAudio = midi::decode(withAudio.data(), withAudio.size());
  CHECK(dec.ok() && decAudio.ok() && !dec.value().notes.empty());
  CHECK(b.renderVariationMidi(b.history().listAll().front().id, o).ok());
  CHECK(!b.renderVariationWav(b.history().listAll().front().id, ExportWavOptions{}).ok());
  CHECK(!b.renderKitFiles(KitOptions{}).ok());
}

CHOP_TEST(midi_settings_roundtrip_in_state_and_defaults_leave_the_bytes_unchanged) {
  ProjectSession s;
  CHECK(fixture::ready(s));
  const auto plain = s.saveState(false).value();
  ProjectExtras e = s.extras();
  e.exportUi.formatIndex = 2;  // forces the export block, midi still default: three bytes as before F13
  CHECK(s.setExtras(e).ok());
  const auto threeByte = s.saveState(false).value();
  e.exportUi.midi = MidiExportSettings{60, VelocityCurve::Fixed, 90, 4, 9};
  CHECK(s.setExtras(e).ok());
  const auto withMidi = s.saveState(false).value();
  CHECK(withMidi.size() == threeByte.size() + 5);
  ProjectSession t;
  CHECK(t.loadState(withMidi.data(), withMidi.size(), [](const source::SourceInfo&) { return fixture::loopAudio(); }).ok());
  CHECK(t.extras().exportUi.midi == (MidiExportSettings{60, VelocityCurve::Fixed, 90, 4, 9}));
  CHECK(t.extras().exportUi.formatIndex == 2);
  ProjectSession u;
  CHECK(u.loadState(threeByte.data(), threeByte.size(), [](const source::SourceInfo&) { return fixture::loopAudio(); }).ok());
  CHECK(u.extras().exportUi.midi == MidiExportSettings{});
  e.exportUi = ExportUiSettings{};
  CHECK(s.setExtras(e).ok());
  CHECK(s.saveState(false).value() == plain);
  MidiExportSettings bad;
  bad.channel = 16;
  e.exportUi.midi = bad;
  CHECK(!s.setExtras(e).ok());
  bad = MidiExportSettings{};
  bad.baseNote = 128;
  e.exportUi.midi = bad;
  CHECK(!s.setExtras(e).ok());
}
