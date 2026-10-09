// Integration tests for the second feature set: Fractal Rhythm, Evolve, Smart Setup, WAV / kit export,
// and per-event effects through the whole session.
#include <algorithm>
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/midi_export/midi.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;
namespace fs = std::filesystem;

namespace {

std::shared_ptr<const render::SourceData> loopAudio() {
  auto d = std::make_shared<render::SourceData>();
  d->channels = 1;
  d->sampleRate = 48000;
  d->frames = 96000;
  d->samples.assign(96000, 0.f);
  Rng rng(3);
  for (int k = 0; k < 8; ++k) {
    const int kind = k % 4 == 0 ? 0 : (k % 4 == 2 ? 1 : 2);
    for (int i = 0; i < 5000; ++i) {
      const float t = static_cast<float>(i);
      float v = 0.f;
      if (kind == 0) v = std::sin(t * 0.0105f) * std::exp(-t / 1500.f);
      if (kind == 1) v = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * std::exp(-t / 900.f);
      if (kind == 2) v = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * std::exp(-t / 250.f) * 0.6f;
      d->samples[static_cast<std::size_t>(k * 12000 + i)] = v * 0.8f;
    }
  }
  return d;
}

pattern::Settings settings(std::uint64_t seed = 42, int bars = 2) {
  pattern::Settings s;
  s.bars = bars;
  s.density = 0.7;
  s.seed = seed;
  s.variation = {0.6, 0.6, 0.6, 0.7};
  return s;
}

bool ready(ProjectSession& s, bool chop = true) {
  CHECK(s.loadSource(loopAudio(), "loop", "/music/loop.wav").ok());
  if (!chop) return s.hasSource();
  DetectOptions d;
  d.analysis.sensitivity = 0.8;
  CHECK(s.applyChops(d).ok());
  return s.hasSource();
}

std::size_t flatCount(const ProjectSession& s) { return pattern::flatten(*s.pattern(), *s.chops()).value().size(); }

bool audible(const std::vector<std::vector<float>>& a) {
  float peak = 0.f;
  for (float x : a[0]) {
    if (!std::isfinite(x)) return false;
    peak = std::max(peak, std::fabs(x));
  }
  return peak > 0.05f;
}

std::string tmpDir(const char* name) {
  auto p = fs::temp_directory_path() / "cf_features" / name;
  fs::remove_all(p);
  fs::create_directories(p);
  return p.string();
}

std::vector<std::uint8_t> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

std::size_t fileCount(const std::string& dir) {
  std::size_t n = 0;
  for (auto& e : fs::directory_iterator(dir)) {
    (void)e;
    ++n;
  }
  return n;
}

}  // namespace

CHOP_TEST(fractal_rhythm_installs_user_owned_bars_that_mutate_keeps) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK(s.generate(settings()).ok());
  auto bars = [](const ProjectSession& x) {
    std::vector<std::uint8_t> out;
    bytes::Writer w(out);
    for (const auto& b : x.pattern()->bars)
      for (const auto& bt : b.beats)
        for (const Event& e : bt.events) writeEvent(w, e);
    return out;
  };
  fractal::Settings fs;
  fs.motif = "x.xx";
  fs.depth = 2;
  fs.seed = 4;
  CHECK(s.applyFractal(fs).ok());
  CHECK_EQ(flatCount(s), 18u);  // 9 hits per bar, two bars
  for (const auto& b : s.pattern()->bars) CHECK(b.userOwned);
  CHECK(audible(s.renderOffline(render::OfflineSettings{}).value()));
  const auto before = bars(s);
  pattern::MutateOptions mo;
  mo.amount = 1.0;
  CHECK(s.mutate(mo).ok());
  CHECK(bars(s) == before);  // fractal bars are kept
  mo.includeEdited = true;
  mo.seed = 9;
  CHECK(s.mutate(mo).ok());
  CHECK(bars(s) != before);  // unless edited content is included

  // Undo returns to the fractal bars; save / reload reproduces them.
  CHECK(s.undo());
  CHECK(bars(s) == before);
  auto saved = s.saveState(true);
  CHECK(saved.ok());
  ProjectSession t;
  CHECK(t.loadState(saved.value().data(), saved.value().size(), [](const source::SourceInfo&) { return nullptr; }).ok());
  CHECK(t.pattern() && pattern::serialize(*t.pattern()) == pattern::serialize(*s.pattern()) && bars(t) == before);
  CHECK_EQ(flatCount(t), 18u);
}

CHOP_TEST(fractal_rhythm_respects_locks_and_the_event_cap) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK(s.generate(settings()).ok());
  const auto bar0 = pattern::serialize(*s.pattern());
  CHECK(s.edit([](const pattern::Pattern& p, const ChopSnapshot&) { return pattern::setLock(p, pattern::ScopeRef{ScopeLevel::Bar, 0, 0, EventId{}}, true); }).ok());
  fractal::Settings fs;
  fs.depth = 1;
  CHECK(!s.applyFractal(fs, {0}).ok());          // an explicitly named locked bar is refused
  CHECK(s.applyFractal(fs).ok());                // otherwise it is skipped
  const auto& bars = s.pattern()->bars;
  CHECK(bars[0].locked && !bars[0].userOwned);
  CHECK(bars[1].userOwned);
  CHECK(!s.applyFractal(fs, {7}).ok());          // out of range
  fractal::Settings deep;
  deep.motif = "xxxxxxxx";
  deep.depth = 3;
  auto big = s.applyFractal(deep, {1});
  CHECK(!big.ok() && big.error().code == ErrorCode::LimitExceeded);   // 512 hits do not fit beside bar 0
  CHECK(bars[1].userOwned);                                           // and the pattern was left as it was
  (void)bar0;
  ProjectSession none;
  CHECK(!none.applyFractal(fs).ok());
}

CHOP_TEST(evolve_steps_in_place_keeps_locks_and_does_not_flood_history) {
  ProjectSession s, twin;
  for (ProjectSession* p : {&s, &twin}) {
    CHECK(ready(*p));
    CHECK(p->generate(settings(7, 4)).ok());
  }
  CHECK(s.edit([](const pattern::Pattern& p, const ChopSnapshot&) { return pattern::setLock(p, pattern::ScopeRef{ScopeLevel::Bar, 1, 0, EventId{}}, true); }).ok());
  auto lockedBar = [](const ProjectSession& x) {
    std::vector<std::uint8_t> out;
    bytes::Writer w(out);
    for (const auto& bt : x.pattern()->bars[1].beats)
      for (const Event& e : bt.events) writeEvent(w, e);
    return out;
  };
  const auto lockedBefore = lockedBar(s);
  auto idle = s.evolveStep();
  CHECK(idle.ok() && !idle.value());              // not started: nothing happens
  evolve::Settings es;
  es.enabled = true;
  es.everyLoops = 2;
  es.amount = 0.8;
  es.startSeed = 11;
  CHECK(s.setEvolve(es).ok() && twin.setEvolve(es).ok() && s.evolveRunning());
  const std::size_t nodes = s.history().size();
  int applied = 0;
  bool changed = false;
  const auto start = pattern::serialize(*s.pattern());
  for (int loop = 0; loop < 12; ++loop) {
    auto r = s.evolveStep();
    CHECK(r.ok());
    if (r.ok() && r.value()) {
      ++applied;
      changed = changed || pattern::serialize(*s.pattern()) != start;
      CHECK(audible(s.renderOffline(render::OfflineSettings{}).value()));
    }
    CHECK(lockedBar(s) == lockedBefore);          // the locked bar never moves
  }
  CHECK_EQ(applied, 6);
  CHECK(changed);
  CHECK_EQ(s.history().size(), nodes);            // steps are undoable but not family-tree nodes
  CHECK(s.keepEvolved().ok() && s.history().size() == nodes + 1);
  CHECK(s.undo());

  // Deterministic: the unlocked twin takes the same steps.
  ProjectSession a, b;
  for (ProjectSession* p : {&a, &b}) {
    CHECK(ready(*p));
    CHECK(p->generate(settings(7, 4)).ok());
    CHECK(p->setEvolve(es).ok());
    for (int loop = 0; loop < 8; ++loop) CHECK(p->evolveStep().ok());
  }
  CHECK(pattern::serialize(*a.pattern()) == pattern::serialize(*b.pattern()));

  // Settings persist; a loaded project never starts evolving by itself.
  auto saved = a.saveState(true);
  CHECK(saved.ok());
  ProjectSession c;
  CHECK(c.loadState(saved.value().data(), saved.value().size(), [](const source::SourceInfo&) { return nullptr; }).ok());
  CHECK(!c.evolveRunning() && c.evolveSettings().everyLoops == 2 && c.evolveSettings().startSeed == 11 && !c.evolveSettings().enabled);
  evolve::Settings bad = es;
  bad.everyLoops = 3;
  CHECK(!a.setEvolve(bad).ok());
  CHECK(a.setEvolve(evolve::Settings{}).ok() && !a.evolveRunning());
  ProjectSession empty;
  evolve::Settings on = es;
  CHECK(!empty.setEvolve(on).ok());               // needs a pattern
}

CHOP_TEST(smart_setup_suggests_without_changing_anything_and_never_overwrites_roles) {
  ProjectSession s;
  CHECK(ready(s, false));
  CHECK(s.chopMap()->markers().empty());          // nothing detected yet
  auto setup = s.smartSetup(110.0);
  CHECK(setup.ok() && s.chops() && !s.chops()->chops.empty());
  if (!setup.ok()) return;
  CHECK_EQ(setup.value().roles.size(), s.chops()->chops.size());
  CHECK(!setup.value().loops.empty() && setup.value().loops[0].bpm >= 60.0);
  const auto markersBefore = s.chopMap()->markers().size();
  CHECK(s.roleMap().all().empty());               // suggesting changed nothing

  const ChopId first = s.chops()->chops[0].id;
  CHECK(s.assignRole(first, "texture").ok());     // the user's choice
  auto applied = s.acceptRoleSuggestions(setup.value().roles);
  CHECK(applied.ok());
  CHECK(s.roleMap().roleOf(first) == "texture");
  for (const auto& r : setup.value().roles)
    if (r.chop != first && r.role != insight::Role::Other) CHECK(s.roleMap().roleOf(r.chop) == insight::roleName(r.role));
  CHECK_EQ(s.chopMap()->markers().size(), markersBefore);
  auto strict = s.acceptRoleSuggestions(setup.value().roles, 2.0);   // nothing reaches this confidence
  CHECK(strict.ok() && strict.value() == 0);
  CHECK(s.generate(settings()).ok());             // roles feed generation as usual
}

CHOP_TEST(wav_export_matches_the_offline_render_and_never_overwrites_silently) {
  ProjectSession s;
  CHECK(ready(s));
  const std::string dir = tmpDir("wav");
  ExportWavOptions o;
  o.wav.sampleRate = 48000;
  o.wav.format = wav::Format::Pcm24;
  o.bpm = 120;
  o.loops = 2;
  o.tailSeconds = 0.25;
  CHECK(!s.exportWav(dir + "/a.wav", o).ok());    // no pattern yet
  CHECK(s.generate(settings()).ok());
  auto rep = s.exportWav(dir + "/a.wav", o);
  CHECK(rep.ok());
  if (!rep.ok()) return;
  render::OfflineSettings rs;
  rs.sampleRate = 48000;
  rs.bpm = 120;
  rs.cycles = 2;
  rs.tailSeconds = 0.25;
  const auto ref = s.renderOffline(rs).value();
  const auto bytes = readFile(dir + "/a.wav");
  auto dec = wav::decode(bytes.data(), bytes.size());
  CHECK(dec.ok() && dec.value().planar.size() == 2 && dec.value().planar[0].size() == ref[0].size());
  CHECK_EQ(rep.value().frames, ref[0].size());
  if (dec.ok() && dec.value().planar[0].size() == ref[0].size())
    for (std::size_t i = 0; i < ref[0].size(); i += 97) CHECK_NEAR(dec.value().planar[0][i], ref[0][i], 2.0 / 8388608.0);
  CHECK(rep.value().peak > 0.05f && rep.value().clipped == 0);

  auto again = s.exportWav(dir + "/a.wav", o);
  CHECK(!again.ok() && again.error().code == ErrorCode::Conflict);
  CHECK(readFile(dir + "/a.wav") == bytes);       // untouched
  o.overwrite = true;
  CHECK(s.exportWav(dir + "/a.wav", o).ok());
  ExportWavOptions bad = o;
  bad.loops = 0;
  CHECK(!s.exportWav(dir + "/b.wav", bad).ok());
  bad = o;
  bad.tailSeconds = 99;
  CHECK(!s.exportWav(dir + "/b.wav", bad).ok());
  bad = o;
  bad.wav.sampleRate = 100;
  CHECK(!s.exportWav(dir + "/b.wav", bad).ok());
  bad = o;
  bad.bpm = 1;
  CHECK(!s.exportWav(dir + "/b.wav", bad).ok());
  CHECK(!fs::exists(dir + "/b.wav"));
  o.wav.format = wav::Format::Float32;
  o.wav.sampleRate = 96000;
  CHECK(s.exportWav(dir + "/c.wav", o).ok());
}

CHOP_TEST(producer_kit_writes_slices_midi_and_a_map) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK(s.generate(settings()).ok());
  CHECK(s.assignRole(s.chops()->chops[0].id, "kick").ok());
  const std::string dir = tmpDir("kit");
  KitOptions k;
  k.bpm = 100;
  k.baseNote = 36;
  auto rep = s.exportKit(dir, k);
  CHECK(rep.ok());
  if (!rep.ok()) return;
  const std::size_t n = s.chops()->chops.size();
  CHECK_EQ(rep.value().files.size(), n + 2);
  CHECK(fs::exists(dir + "/slice_01_kick.wav") && fs::exists(dir + "/slice_02_chop.wav") && fs::exists(dir + "/chopfractal.mid") && fs::exists(dir + "/kit.txt"));

  // A slice is the chop's audio (past the short fades).
  const auto sb = readFile(dir + "/slice_01_kick.wav");
  auto sd = wav::decode(sb.data(), sb.size());
  CHECK(sd.ok());
  const ChopInfo& c0 = s.chops()->chops[0];
  if (sd.ok()) {
    CHECK_EQ(sd.value().planar[0].size(), static_cast<std::size_t>(c0.range.length()));
    const std::size_t mid = static_cast<std::size_t>(c0.range.length() / 2);
    CHECK_NEAR(sd.value().planar[0][mid], loopAudio()->samples[static_cast<std::size_t>(c0.range.start) + mid], 2.0 / 8388608.0);
    CHECK(sd.value().planar[0][0] == 0.f);        // faded in
  }

  // The MIDI file replays the pattern on those slices.
  const auto mb = readFile(dir + "/chopfractal.mid");
  auto md = midi::decode(mb.data(), mb.size());
  CHECK(md.ok());
  if (md.ok()) {
    CHECK_NEAR(md.value().bpm, 100.0, 0.01);
    std::size_t expected = 0;
    for (const FlatEvent& e : s.playback()->events) expected += s.chops()->find(e.chop) != nullptr;
    CHECK(md.value().notes.size() <= expected && md.value().notes.size() + 4 >= expected);   // same-pitch overlaps may merge
    std::set<int> chopNotes;
    for (const auto& note : md.value().notes) {
      CHECK(note.note >= 36 && note.note < 36 + static_cast<int>(n));
      CHECK(note.velocity >= 1 && note.velocity <= 127);
      chopNotes.insert(note.note);
    }
    CHECK(!chopNotes.empty());
  }
  CHECK(readFile(dir + "/kit.txt").size() > 20);

  // Pre-flight collision: nothing is written when any target exists.
  const std::string dir2 = tmpDir("kit2");
  { std::ofstream(dir2 + "/kit.txt") << "mine"; }
  auto clash = s.exportKit(dir2, k);
  CHECK(!clash.ok() && clash.error().code == ErrorCode::Conflict);
  CHECK_EQ(fileCount(dir2), 1u);
  CHECK(readFile(dir2 + "/kit.txt").size() == 4);
  k.overwrite = true;
  CHECK(s.exportKit(dir2, k).ok());
  KitOptions bad = k;
  bad.baseNote = 125;                             // would run past note 127
  CHECK(!s.exportKit(tmpDir("kit3"), bad).ok());
  CHECK_EQ(fileCount(tmpDir("kit3")), 0u);
  bad = k;
  bad.loops = 17;
  CHECK(!s.exportKit(tmpDir("kit4"), bad).ok());
  ProjectSession none;
  CHECK(!none.exportKit(dir, k).ok());
}

CHOP_TEST(effects_flow_through_generation_zoom_state_and_audio) {
  ProjectSession s;
  CHECK(ready(s));
  pattern::Settings fx = settings(5, 2);
  fx.density = 1.0;
  fx.variation.event = 1.0;
  fx.allowFilter = fx.allowGlide = fx.allowCrunch = true;
  fx.fxIntensity = 1.0;
  CHECK(s.generate(fx).ok());
  EventId withFx;
  for (const auto& bar : s.pattern()->bars)
    for (const auto& bt : bar.beats)
      for (const Event& e : bt.events)
        if (e.fx.active() && !withFx.valid()) withFx = e.id;
  CHECK(withFx.valid());
  CHECK(audible(s.renderOffline(render::OfflineSettings{}).value()));

  // Children inherit the parent's effects.
  zoom::Settings zs;
  zs.subdivisions = 3;
  CHECK(s.zoomIn(withFx, zs, 3).ok());
  const Event* zoomed = pattern::findEvent(*s.pattern(), withFx);
  CHECK(zoomed && zoomed->child && !zoomed->child->events.empty());
  if (zoomed && zoomed->child)
    for (const Event& c : zoomed->child->events) CHECK(c.fx == zoomed->fx);

  // State round trip keeps effects exactly and the audio identical.
  const auto audio = s.renderOffline(render::OfflineSettings{}).value();
  auto saved = s.saveState(true);
  CHECK(saved.ok());
  ProjectSession t;
  CHECK(t.loadState(saved.value().data(), saved.value().size(), [](const source::SourceInfo&) { return nullptr; }).ok());
  CHECK(pattern::serialize(*t.pattern()) == pattern::serialize(*s.pattern()));
  CHECK(t.renderOffline(render::OfflineSettings{}).value() == audio);

  // Host parameters drive the effect settings.
  host::ParamValues pv = host::ParamValues::defaults();
  pv.set(host::kAllowFilter, 1);
  pv.set(host::kAllowGlide, 1);
  pv.set(host::kFxIntensity, 0.75);
  const pattern::Settings fromHost = settingsFromParams(pv, pattern::Settings{});
  CHECK(fromHost.allowFilter && fromHost.allowGlide && !fromHost.allowCrunch && fromHost.fxIntensity == 0.75);
  pattern::Settings live = fromHost;
  live.bars = s.pattern()->settings.bars;
  live.timeSignature = s.pattern()->settings.timeSignature;
  live.grid = s.pattern()->settings.grid;
  CHECK(t.mutate(pattern::MutateOptions{}, &live).ok());
  CHECK(t.pattern()->settings.allowFilter && !t.pattern()->settings.allowCrunch && t.pattern()->settings.fxIntensity == 0.75);

  // A manual effect edit is an ordinary undoable edit.
  const EventId any = pattern::findEvent(*s.pattern(), withFx) ? withFx : EventId{};
  EventFx edit;
  edit.filter = FilterType::LowPass;
  edit.cutoff = 0.3f;
  CHECK(s.edit([&](const pattern::Pattern& p, const ChopSnapshot&) { return pattern::setEventFx(p, any, edit); }).ok());
  CHECK(pattern::findEvent(*s.pattern(), any)->fx == edit);
  CHECK(s.undo() && !(pattern::findEvent(*s.pattern(), any)->fx == edit));
}

CHOP_TEST(embed_cap_is_generous_but_adjustable) {
  CHECK_EQ(kMaxEmbeddedSourceBytes, 128u * 1024u * 1024u);
  ProjectSession s;
  CHECK(ready(s));
  CHECK_EQ(s.embeddedSourceCap(), kMaxEmbeddedSourceBytes);
  CHECK(s.saveState(true).ok());
  CHECK(s.setEmbeddedSourceCap(1000).ok());
  auto small = s.saveState(true);
  CHECK(!small.ok() && small.error().code == ErrorCode::LimitExceeded && small.error().hint == 1000);
  CHECK(s.saveState(false).ok());
  CHECK(!s.setEmbeddedSourceCap(kCodecCeilingBytes).ok());
}

// ---- regression tests for the code audit ----
namespace {
// A child event that uses `chop` only inside a zoomed hit (so deleting that chop strands it).
Status giveChildOnlyChop(ProjectSession& s, EventId parent, ChopId chop) {
  return s.edit([&](const pattern::Pattern& p, const ChopSnapshot& chops) -> Result<pattern::Pattern> {
    const Event* e = pattern::findEvent(p, parent);
    if (!e) return makeError(ErrorCode::NotFound, "no event");
    auto child = std::make_shared<NestedPattern>();
    child->windowDuration = e->duration;
    child->depth = 1;
    Event c;
    c.id = EventId{kDerivedIdBit | 0x777};
    c.chop = chop;
    c.sourceOverride = true;
    c.start = 0;
    c.duration = e->duration;
    child->events.push_back(c);
    return pattern::setChild(p, parent, child, chops);
  });
}
}  // namespace

CHOP_TEST(deleting_a_chop_used_only_inside_a_zoom_is_repaired_not_wedged) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK(s.generate(settings(3, 2)).ok());
  std::set<std::uint64_t> used;
  EventId host;
  for (const auto& b : s.pattern()->bars)
    for (const auto& bt : b.beats)
      for (const Event& e : bt.events) {
        used.insert(e.chop.value);
        host = e.id;
      }
  ChopId spare;
  for (const ChopInfo& c : s.chops()->chops)
    if (!used.count(c.id.value)) spare = c.id;
  if (!spare.valid()) return;  // every chop is used at top level: nothing to strand
  CHECK(giveChildOnlyChop(s, host, spare).ok());
  CHECK(s.editMarkers([&](source::ChopMap& m) { return m.deleteMarker(spare); }).ok());
  CHECK(pattern::flatten(*s.pattern(), *s.chops()).ok());   // the stranded zoom was dropped
  pattern::MutateOptions mo;
  CHECK(s.mutate(mo).ok());                                  // and the session still works
}

CHOP_TEST(marker_edits_are_transactional_and_undo_never_restores_dead_chops) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK(s.generate(settings(5, 2)).ok());
  const std::size_t markers = s.chopMap()->markers().size();
  ChopId used = s.pattern()->bars[0].beats[0].events.empty() ? ChopId{} : s.pattern()->bars[0].beats[0].events[0].chop;
  for (const auto& bt : s.pattern()->bars[0].beats)
    if (!bt.events.empty() && !used.valid()) used = bt.events[0].chop;
  CHECK(used.valid());
  CHECK(!s.editMarkers([](source::ChopMap& m) { return m.deleteMarker(ChopId{987654}); }).ok());   // bad edit: nothing changes
  CHECK_EQ(s.chopMap()->markers().size(), markers);
  CHECK(s.editMarkers([&](source::ChopMap& m) { return m.deleteMarker(used); }).ok());
  CHECK(pattern::flatten(*s.pattern(), *s.chops()).ok());
  for (int i = 0; i < 10 && s.undo(); ++i) CHECK(pattern::flatten(*s.pattern(), *s.chops()).ok());   // every step it does restore is playable
  CHECK(pattern::flatten(*s.pattern(), *s.chops()).ok());
  CHECK(s.undoMarkers().ok());
  CHECK_EQ(s.chopMap()->markers().size(), markers);
}

CHOP_TEST(roles_survive_disabling_and_re_enabling_a_chop) {
  ProjectSession s;
  CHECK(ready(s));
  const ChopId c = s.chops()->chops[0].id;
  CHECK(s.assignRole(c, "kick").ok());
  CHECK(s.editMarkers([&](source::ChopMap& m) { return m.setEnabled(c, false); }).ok());
  CHECK(s.editMarkers([&](source::ChopMap& m) { return m.setEnabled(c, true); }).ok());
  CHECK(s.roleMap().roleOf(c) == "kick");
}

CHOP_TEST(per_event_locks_block_bar_replacement_and_duplication) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK(s.generate(settings(9, 2)).ok());
  EventId locked;
  for (const auto& bt : s.pattern()->bars[1].beats)
    if (!bt.events.empty() && !locked.valid()) locked = bt.events[0].id;
  if (!locked.valid()) return;
  CHECK(s.edit([&](const pattern::Pattern& p, const ChopSnapshot&) { return pattern::setLock(p, pattern::ScopeRef{ScopeLevel::Event, 0, 0, locked}, true); }).ok());
  const auto before = pattern::serialize(*s.pattern());
  auto dup = pattern::duplicateBar(*s.pattern(), 0, 1);
  CHECK(!dup.ok() && dup.error().code == ErrorCode::Blocked);
  auto repl = pattern::setBarEvents(*s.pattern(), 1, {}, *s.chops());
  CHECK(!repl.ok() && repl.error().code == ErrorCode::Blocked);
  fractal::Settings fs;
  fs.depth = 1;
  CHECK(s.applyFractal(fs).ok());                    // the bar holding the locked hit is skipped, the other is replaced
  CHECK(pattern::findEvent(*s.pattern(), locked) != nullptr);
  (void)before;
}

CHOP_TEST(absurd_event_times_and_rule_labels_are_rejected) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK(s.generate(settings(2, 2)).ok());
  const ChopId c = s.chops()->chops[0].id;
  CHECK(!pattern::addEvent(*s.pattern(), 0, c, 1, INT64_MAX, *s.chops()).ok());
  Event e;
  e.id = EventId{kDerivedIdBit | 5};
  e.chop = c;
  e.start = Ticks{1} << 62;
  e.duration = Ticks{1} << 62;
  CHECK(!validateEvent(e).ok());
  roles::RuleSet rules;
  roles::Rule r;
  r.id = 1;
  r.label = std::string(200, 'x');
  rules.rules.push_back(r);
  CHECK(!s.setRules(rules).ok());                    // would otherwise save and then fail to load
  r.label = "ok";
  rules.rules[0] = r;
  CHECK(s.setRules(rules).ok());
  auto saved = s.saveState(true);
  CHECK(saved.ok());
  ProjectSession t;
  CHECK(t.loadState(saved.value().data(), saved.value().size(), [](const source::SourceInfo&) { return nullptr; }).ok());
}
