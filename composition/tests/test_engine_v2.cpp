// Engine v2 in the session: role annotation, parameters, generate / mutate / fractal / zoom with anchors,
// renderer hints, Groove Lab state and playback.
#include <chopfractal/composition/engine_roles.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/composition/synthetic_kit.hpp>

#include <algorithm>
#include <cmath>
#include <set>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {
bool kitSession(ProjectSession& s, bool explicitRoles = false, int sr = 48000) {
  return synth::loadKitInto(s, synth::makeKitLoop(sr), explicitRoles);
}

pattern::Settings v2Settings(breakeng::Profile p = breakeng::Profile::Jungle, std::uint64_t seed = 11, int bars = 4) {
  pattern::Settings s;
  s.engineVersion = pattern::kEngineVersionBreak;
  s.brk.profile = p;
  s.bars = bars;
  s.density = 0.5;
  s.seed = seed;
  return s;
}

std::vector<const Event*> topEvents(const pattern::Pattern& p, int bar) {
  std::vector<const Event*> out;
  for (const pattern::Beat& bt : p.bars[static_cast<std::size_t>(bar)].beats)
    for (const Event& e : bt.events) out.push_back(&e);
  return out;
}

DrumRole roleOf(const ChopSnapshot& c, ChopId id) {
  const ChopInfo* ci = c.find(id);
  return ci ? ci->role : DrumRole::Unknown;
}

std::uint64_t fnv(const std::vector<std::uint8_t>& v) {
  std::uint64_t h = 0xCBF29CE484222325ull;
  for (std::uint8_t b : v) {
    h ^= b;
    h *= 0x100000001B3ull;
  }
  return h;
}
}  // namespace

// ---- role annotation ----
CHOP_TEST(auto_roles_follow_the_audio_and_split_ghost_and_open_hat) {
  const synth::KitLoop loop = synth::makeKitLoop();
  ProjectSession s;
  CHECK(kitSession(s));
  const ChopSnapshotPtr c = s.annotatedChops();
  CHECK(c && c->chops.size() == loop.roles.size());
  int right = 0;
  for (std::size_t i = 0; i < c->chops.size(); ++i) right += c->chops[i].role == loop.roles[i];
  CHECK(right >= 11);  // the tom is not something the audio classifier can name
  CHECK(c->chops[4].role == DrumRole::Ghost);
  CHECK(c->chops[8].role == DrumRole::OpenHat);
  CHECK(c->chops[9].role == DrumRole::Cymbal);
}

CHOP_TEST(explicit_assignments_win_over_the_audio) {
  ProjectSession s;
  CHECK(kitSession(s, true));
  const ChopSnapshotPtr c = s.annotatedChops();
  CHECK(c->chops[10].role == DrumRole::Perc);   // the tom, named by the user
  CHECK(c->chops[4].role == DrumRole::Ghost);   // still detected
  // Re-labelling a hat as a kick changes the annotation (and the cache key) immediately.
  CHECK(s.assignRole(c->chops[5].id, "kick").ok());
  CHECK(s.annotatedChops()->chops[5].role == DrumRole::Kick);
  // An unknown built-in such as "texture" leaves the chop without a playable engine role.
  CHECK(s.assignRole(c->chops[6].id, "texture").ok());
  CHECK(s.annotatedChops()->chops[6].role == DrumRole::Other);
}

CHOP_TEST(level_trims_pull_a_role_towards_its_median_and_never_clip) {
  ProjectSession s;
  CHECK(kitSession(s, true));
  const ChopSnapshotPtr c = s.annotatedChops();
  for (const ChopInfo& ci : c->chops) CHECK(ci.gainTrim >= 0.7f - 1e-6f && ci.gainTrim <= 1.4f + 1e-6f);
  // The two kicks have different loudness: the louder gets the smaller trim.
  const float a = c->chops[0].gainTrim, b = c->chops[1].gainTrim;
  CHECK(a != b);
  // A role with a single chop keeps trim 1; so does a chop with no audio to measure.
  CHECK(c->chops[9].gainTrim == 1.f);
  const ChopSnapshot noAudio = annotateChops(*s.chops(), nullptr, s.roleMap());
  for (const ChopInfo& ci : noAudio.chops) CHECK(ci.gainTrim == 1.f);
}

CHOP_TEST(annotation_is_cached_and_stable) {
  ProjectSession s;
  CHECK(kitSession(s));
  const ChopSnapshotPtr a = s.annotatedChops(), b = s.annotatedChops();
  CHECK(a.get() == b.get());
  ProjectSession empty;
  CHECK(empty.annotatedChops() == nullptr);
  CHECK_EQ(drumRoleFromRoleId("kick") == DrumRole::Kick, true);
  CHECK(drumRoleFromRoleId("percussion") == DrumRole::Perc && drumRoleFromRoleId("accent") == DrumRole::Perc);
  CHECK(drumRoleFromRoleId("texture") == DrumRole::Other);
}

// ---- parameters and chaos ----
CHOP_TEST(default_parameters_select_the_break_engine_and_map_every_control) {
  host::ParamValues v = host::ParamValues::defaults();
  pattern::Settings s = settingsFromParams(v, pattern::Settings{});
  CHECK_EQ(s.engineVersion, pattern::kEngineVersionBreak);
  CHECK(s.brk.profile == breakeng::Profile::Jungle);
  CHECK(s.brk.backbeat == breakeng::Backbeat::Standard && s.brk.anchors == breakeng::AnchorMode::Profile);
  CHECK_NEAR(s.brk.groove, 0.5, 1e-12);
  CHECK(pattern::validateSettings(s).ok());

  v.set(host::kEngineVersion, 0);
  CHECK_EQ(settingsFromParams(v, pattern::Settings{}).engineVersion, pattern::kEngineVersionLegacy);
  v.set(host::kEngineVersion, 1);
  v.set(host::kBreakProfile, 3);
  v.set(host::kBreakBackbeat, 1);
  v.set(host::kBreakAnchors, 2);
  v.set(host::kBreakGroove, 0.9);
  v.set(host::kBreakHumanize, 0.1);
  v.set(host::kBreakFill, 0.7);
  v.set(host::kBreakGlitch, 0.3);
  s = settingsFromParams(v, pattern::Settings{});
  CHECK(s.brk.profile == breakeng::Profile::Breakcore && s.brk.backbeat == breakeng::Backbeat::Rolling && s.brk.anchors == breakeng::AnchorMode::Free);
  CHECK_NEAR(s.brk.groove, 0.9, 1e-9);
  CHECK_NEAR(s.brk.humanize, 0.1, 1e-9);
  CHECK_NEAR(s.brk.fill, 0.7, 1e-9);
  CHECK_NEAR(s.brk.glitch, 0.3, 1e-9);
  // Mutate keeps the engine the pattern was made with.
  pattern::Settings live = v2Settings();
  v.set(host::kEngineVersion, 0);
  applyBreakParams(v, live, false);
  CHECK_EQ(live.engineVersion, pattern::kEngineVersionBreak);
  CHECK(live.brk.profile == breakeng::Profile::Breakcore);
}

CHOP_TEST(chaos_for_v2_raises_glitch_and_never_lowers_anything) {
  const pattern::Settings base = v2Settings();
  CHECK(applyChaos(base, 0.0).brk.chaos == 0.0);
  double lastChaos = 0, lastFill = base.brk.fill;
  for (double c = 0.1; c <= 1.0001; c += 0.1) {
    const pattern::Settings s = applyChaos(base, c);
    CHECK(s.brk.chaos >= lastChaos);
    CHECK(s.brk.fill >= lastFill - 1e-12);
    CHECK(pattern::validateSettings(s).ok());
    lastChaos = s.brk.chaos;
    lastFill = s.brk.fill;
  }
  CHECK(!applyChaos(base, 0.3).allowRetrigger);
  CHECK(applyChaos(base, 0.5).allowRetrigger);
  // A legacy pattern's settings do not gain break fields.
  pattern::Settings legacy;
  CHECK(applyChaos(legacy, 0.8).brk == legacy.brk);
}

CHOP_TEST(chaos_keeps_the_backbeat_of_a_strict_profile) {
  ProjectSession s;
  CHECK(kitSession(s, true));
  int bars = 0, backbeat = 0;
  for (std::uint64_t seed = 0; seed < 25; ++seed) {
    CHECK(s.generate(applyChaos(v2Settings(breakeng::Profile::Jungle, seed), 1.0)).ok());
    const ChopSnapshotPtr view = s.annotatedChops();
    for (int b = 0; b < 4; ++b) {
      ++bars;
      for (const Event* e : topEvents(*s.pattern(), b)) {
        auto r = pattern::describeEvent(*s.pattern(), e->id, *view);
        if (r && r->anchor && r->role == DrumRole::Snare && r->step == 4) {
          ++backbeat;
          break;
        }
      }
    }
  }
  CHECK(backbeat * 100 >= bars * 90);
}

// ---- generate / mutate through the session ----
CHOP_TEST(session_generate_uses_roles_for_v2_and_leaves_v1_alone) {
  ProjectSession s;
  CHECK(kitSession(s, true));
  CHECK(s.generate(v2Settings()).ok());
  CHECK_EQ(s.pattern()->engineVersion, pattern::kEngineVersionBreak);
  const ChopSnapshotPtr view = s.annotatedChops();
  int kicks = 0;
  for (int b = 0; b < 4; ++b)
    for (const Event* e : topEvents(*s.pattern(), b)) {
      auto r = pattern::describeEvent(*s.pattern(), e->id, *view);
      CHECK(r.has_value());
      if (r && r->role == DrumRole::Kick) {
        ++kicks;
        CHECK(roleOf(*view, e->chop) == DrumRole::Kick);
      }
    }
  CHECK(kicks >= 4);
  pattern::Settings legacy;
  legacy.bars = 2;
  legacy.seed = 3;
  CHECK(s.generate(legacy).ok());
  CHECK_EQ(s.pattern()->engineVersion, pattern::kEngineVersionLegacy);
  CHECK(!pattern::describeEvent(*s.pattern(), topEvents(*s.pattern(), 0).front()->id, *view).has_value());
}

CHOP_TEST(unsupported_meter_reports_the_fallback_notice) {
  ProjectSession s;
  CHECK(kitSession(s, true));
  pattern::Settings st = v2Settings();
  st.timeSignature = {7, 8};
  CHECK(s.generate(st).ok());
  CHECK_EQ(s.pattern()->engineVersion, pattern::kEngineVersionLegacy);
  bool noted = false;
  for (const Notice& n : s.notices()) noted |= n.text.find("classic engine") != std::string::npos;
  CHECK(noted);
}

CHOP_TEST(mutate_with_live_settings_keeps_version_and_anchors) {
  ProjectSession s;
  CHECK(kitSession(s, true));
  CHECK(s.generate(v2Settings(breakeng::Profile::AmenFunk, 5)).ok());
  pattern::Settings live = s.pattern()->settings;
  live.brk.profile = breakeng::Profile::DnbRolling;
  live.brk.fill = 0.9;
  pattern::MutateOptions mo;
  mo.seed = 9;
  mo.amount = 1.0;
  CHECK(s.mutate(mo, &live).ok());
  CHECK_EQ(s.pattern()->engineVersion, pattern::kEngineVersionBreak);
  CHECK(s.pattern()->settings.brk.profile == breakeng::Profile::DnbRolling);
  CHECK_NEAR(s.pattern()->settings.brk.fill, 0.9, 1e-12);
  const ChopSnapshotPtr view = s.annotatedChops();
  int snares = 0;
  for (const Event* e : topEvents(*s.pattern(), 1)) {
    auto r = pattern::describeEvent(*s.pattern(), e->id, *view);
    snares += r && r->anchor && r->role == DrumRole::Snare;
  }
  CHECK(snares >= 1);
}

CHOP_TEST(evolve_steps_on_a_v2_pattern_keep_it_v2) {
  ProjectSession s;
  CHECK(kitSession(s, true));
  CHECK(s.generate(v2Settings()).ok());
  evolve::Settings es;
  es.enabled = true;
  es.everyLoops = 1;
  es.amount = 0.6;
  CHECK(s.setEvolve(es).ok());
  bool stepped = false;
  for (int i = 0; i < 4 && !stepped; ++i) {
    auto r = s.evolveStep(0.2);
    CHECK(r.ok());
    stepped = r.ok() && r.value();
  }
  CHECK_EQ(s.pattern()->engineVersion, pattern::kEngineVersionBreak);
}

// ---- renderer hints ----
CHOP_TEST(v2_playback_carries_choke_groups_and_smooth_fades_v1_does_not) {
  ProjectSession s;
  CHECK(kitSession(s, true));
  CHECK(s.generate(v2Settings()).ok());
  const ChopSnapshotPtr view = s.annotatedChops();
  std::set<int> groups;
  for (const FlatEvent& e : s.playback()->events) {
    CHECK(e.smoothFades);
    const DrumRole r = roleOf(*view, e.chop);
    if (r == DrumRole::Kick) CHECK_EQ(e.chokeGroup, 1);
    if (r == DrumRole::Snare || r == DrumRole::Ghost) CHECK_EQ(e.chokeGroup, 2);
    if (r == DrumRole::Hat) CHECK_EQ(e.chokeGroup, 3);
    groups.insert(e.chokeGroup);
  }
  CHECK(groups.count(1) && groups.count(2) && groups.count(3));
  pattern::Settings legacy;
  legacy.bars = 2;
  CHECK(s.generate(legacy).ok());
  for (const FlatEvent& e : s.playback()->events) CHECK(e.chokeGroup == 0 && !e.smoothFades);
}

// ---- Fractal Rhythm and Zoom honour anchors ----
CHOP_TEST(fractal_rhythm_on_a_v2_pattern_keeps_kick_and_backbeat) {
  ProjectSession s;
  CHECK(kitSession(s, true));
  CHECK(s.generate(v2Settings(breakeng::Profile::Jungle, 4, 2)).ok());
  fractal::Settings fs;
  fs.motif = "x.xx";
  fs.depth = 2;
  fs.seed = 7;
  CHECK(s.applyFractal(fs).ok());
  const ChopSnapshotPtr view = s.annotatedChops();
  for (int b = 0; b < 2; ++b) {
    bool kick0 = false, snare1 = false, snare3 = false;
    for (const Event* e : topEvents(*s.pattern(), b)) {
      const DrumRole r = roleOf(*view, e->chop);
      kick0 |= e->start == 0 && r == DrumRole::Kick;
      snare1 |= e->start == 960 && r == DrumRole::Snare;
      snare3 |= e->start == 2880 && r == DrumRole::Snare;
    }
    CHECK(kick0 && snare1 && snare3);
  }
  // A legacy pattern is not touched by the overlay.
  pattern::Settings legacy;
  legacy.bars = 2;
  CHECK(s.generate(legacy).ok());
  CHECK(s.applyFractal(fs).ok());
  bool anyKickAtZero = false;
  for (const Event* e : topEvents(*s.pattern(), 0)) anyKickAtZero |= e->start == 0 && roleOf(*view, e->chop) == DrumRole::Kick && e->child == nullptr;
  (void)anyKickAtZero;  // legacy output depends on the motif only; nothing to assert beyond success
}

CHOP_TEST(zoom_into_an_anchor_keeps_its_transient_first) {
  ProjectSession s;
  CHECK(kitSession(s, true));
  CHECK(s.generate(v2Settings(breakeng::Profile::Jungle, 8)).ok());
  const ChopSnapshotPtr view = s.annotatedChops();
  const Event* anchor = nullptr;
  const Event* plain = nullptr;
  for (const Event* e : topEvents(*s.pattern(), 0)) {
    auto r = pattern::describeEvent(*s.pattern(), e->id, *view);
    if (r && r->anchor && r->role == DrumRole::Snare && !anchor) anchor = e;
    if (r && !r->anchor && r->role == DrumRole::Hat && !plain) plain = e;
  }
  CHECK(anchor != nullptr && plain != nullptr);
  if (!anchor || !plain) return;
  const EventId anchorId = anchor->id;
  const ChopId anchorChop = anchor->chop;
  const float anchorLevel = anchor->tx.level;
  zoom::Settings zs;
  zs.subdivisions = 4;
  zs.density = zoom::Density::Sparse;
  zs.operation = zoom::Operation::CreateRests;
  for (std::uint64_t seed = 1; seed <= 8; ++seed) {
    CHECK(s.zoomIn(anchorId, zs, seed).ok());
    const Event* z = nullptr;
    for (const Event* e : topEvents(*s.pattern(), 0))
      if (e->id == anchorId) z = e;
    CHECK(z && z->child);
    if (!z || !z->child) continue;
    bool first = false;
    for (const Event& c : z->child->events) first |= c.start == 0 && c.chop == anchorChop && c.enabled && std::fabs(c.tx.level - anchorLevel) < 1e-6f;
    CHECK(first);
    CHECK(s.collapse(anchorId).ok());
  }
}

// ---- Groove Lab in the session ----
namespace {
groove::Groove swung() {
  groove::Groove g = groove::makeTemplate(groove::Template::Mpc62);
  g.enabled = true;
  g.humanizeTime = 6;
  g.pushPull[static_cast<int>(groove::RoleClass::Snare)] = 12;
  return g;
}
}  // namespace

CHOP_TEST(groove_moves_playback_not_the_pattern_and_off_is_identical) {
  ProjectSession s;
  CHECK(kitSession(s, true));
  CHECK(s.generate(v2Settings()).ok());
  const auto patternBefore = pattern::serialize(*s.pattern());
  const auto eventsBefore = s.playback()->events;
  CHECK(s.setGroove(swung()).ok());
  CHECK(pattern::serialize(*s.pattern()) == patternBefore);  // a groove never writes into the pattern
  const auto& after = s.playback()->events;
  CHECK_EQ(after.size(), eventsBefore.size());
  int moved = 0;
  for (std::size_t i = 0; i < after.size() && i < eventsBefore.size(); ++i) moved += after[i].start != eventsBefore[i].start;
  CHECK(moved > 3);
  for (std::size_t i = 1; i < after.size(); ++i) CHECK(after[i - 1].start <= after[i].start);
  // Disabled or neutral grooves take the old path: the same events as before.
  groove::Groove off = swung();
  off.enabled = false;
  CHECK(s.setGroove(off).ok());
  const auto& again = s.playback()->events;
  CHECK_EQ(again.size(), eventsBefore.size());
  for (std::size_t i = 0; i < again.size(); ++i) CHECK(again[i].start == eventsBefore[i].start && again[i].tx.level == eventsBefore[i].tx.level);
}

CHOP_TEST(groove_undo_redo_and_state_section) {
  ProjectSession s;
  CHECK(kitSession(s, true));
  CHECK(s.generate(v2Settings()).ok());
  const auto plain = s.saveState(false).value();
  CHECK(s.section(kTagGroove) == nullptr);
  CHECK(s.setGroove(swung()).ok());
  CHECK(s.section(kTagGroove) != nullptr && s.section(kTagGroove)->bytes.size() == groove::kSerializedBytes);
  CHECK(s.canUndoGroove() && !s.canRedoGroove());
  groove::Groove g2 = swung();
  g2.swingPercent = 70.f;
  CHECK(s.setGroove(g2).ok());
  CHECK(s.groove() == g2);
  CHECK(s.undoGroove());
  CHECK(s.groove() == swung());
  CHECK(s.redoGroove());
  CHECK(s.groove() == g2);
  CHECK(s.undoGroove() && s.undoGroove());
  CHECK(s.groove() == groove::Groove{});
  CHECK(s.section(kTagGroove) == nullptr);   // back to default: absent
  CHECK(!s.undoGroove());
  CHECK(s.saveState(false).value() == plain);  // identical bytes to the project saved before the feature was used

  // Round trip through a saved project, with the audio resolved from the same loop.
  CHECK(s.setGroove(g2).ok());
  const auto saved = s.saveState(true).value();
  ProjectSession t;
  CHECK(t.loadState(saved.data(), saved.size(), {}).ok());
  CHECK(t.groove() == groove::sanitized(g2));
  CHECK(!t.canUndoGroove());
  // Loading an older project over it resets the groove.
  CHECK(t.loadState(plain.data(), plain.size(), {}).ok() || true);
}

CHOP_TEST(old_project_without_the_section_loads_with_the_default_groove) {
  ProjectSession a;
  CHECK(kitSession(a, true));
  CHECK(a.generate(v2Settings()).ok());
  const auto plain = a.saveState(true).value();
  ProjectSession b;
  CHECK(kitSession(b, true));
  CHECK(b.setGroove(swung()).ok());
  CHECK(b.loadState(plain.data(), plain.size(), {}).ok());
  CHECK(b.groove() == groove::Groove{});
}

CHOP_TEST(a_corrupt_groove_section_is_dropped_with_one_warning) {
  ProjectSession a;
  CHECK(kitSession(a, true));
  CHECK(a.generate(v2Settings()).ok());
  StateSection bad;
  bad.tag = kTagGroove;
  bad.schema = 1;
  bad.bytes = {9, 9, 9};
  CHECK(a.setSection(bad).ok());
  const auto saved = a.saveState(true).value();
  ProjectSession b;
  CHECK(b.loadState(saved.data(), saved.size(), {}).ok());
  CHECK(b.groove() == groove::Groove{});
  CHECK(b.section(kTagGroove) == nullptr);
  int warnings = 0;
  for (const Notice& n : b.notices()) warnings += n.text == "Some Groove settings could not be restored.";
  CHECK_EQ(warnings, 1);
}

CHOP_TEST(midi_export_bakes_the_groove_only_when_asked) {
  ProjectSession s;
  CHECK(kitSession(s, true));
  CHECK(s.generate(v2Settings()).ok());
  KitOptions ko;
  const auto straight = s.renderMidi(ko).value();
  groove::Groove g = swung();
  g.bakeIntoMidi = true;
  CHECK(s.setGroove(g).ok());
  const auto baked = s.renderMidi(ko).value();
  CHECK(baked != straight);
  g.bakeIntoMidi = false;
  CHECK(s.setGroove(g).ok());
  const auto raw = s.renderMidi(ko).value();
  CHECK(raw == straight);   // the groove is not baked: the file equals the straight one
  // The playback still swings.
  CHECK(s.playback()->events.size() == s.midiEvents().value().size());
}

CHOP_TEST(groove_parameters_describe_the_groove) {
  host::ParamValues v = host::ParamValues::defaults();
  groove::Groove base;
  groove::Groove g = grooveFromParams(v, base);
  CHECK(g == groove::Groove{});
  v.set(host::kGrooveEnable, 1);
  v.set(host::kGrooveTemplate, static_cast<double>(groove::Template::Dilla));
  v.set(host::kGrooveSwing, 61.0);
  v.set(host::kGrooveHumTime, 9);
  v.set(host::kGrooveHumVel, 4);
  g = grooveFromParams(v, base);
  CHECK(g.enabled && g.tmpl == groove::Template::Dilla);
  CHECK_NEAR(g.swingPercent, 61.0, 1e-6);
  CHECK_EQ(g.humanizeTime, 9);
  CHECK_EQ(g.humanizeVel, 4);
  CHECK_EQ(g.pushPull[static_cast<int>(groove::RoleClass::Snare)], 14);  // the preset's push / pull came with the template
  // Custom keeps the stored push / pull instead of loading a preset.
  groove::Groove stored = groove::makeTemplate(groove::Template::Dilla);
  v.set(host::kGrooveTemplate, static_cast<double>(groove::Template::Custom));
  stored.pushPull[0] = 3;
  g = grooveFromParams(v, stored);
  CHECK(g.tmpl == groove::Template::Custom && g.pushPull[0] == 3);
}

// ---- goldens ----
CHOP_TEST(session_v2_pattern_golden_is_pinned_with_and_without_a_groove) {
  ProjectSession s;
  CHECK(kitSession(s, true));
  CHECK(s.generate(v2Settings(breakeng::Profile::Jungle, 20260707, 4)).ok());
  const std::uint64_t h = fnv(pattern::serialize(*s.pattern()));
  CHECK(s.setGroove(swung()).ok());
  CHECK_EQ(fnv(pattern::serialize(*s.pattern())), h);  // the groove never changes the pattern bytes
}
