// F10: the session-level A/B morph (preview, commit, cancel, interruption) with a real source and playback.
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cmath>

#include "session_fixture.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {

bool hasNotice(ProjectSession& s, const std::string& text) {
  bool found = false;
  for (const Notice& n : s.takeNotices()) found = found || n.text == text;
  return found;
}

// A at slot 0 (seed 21), B at slot 1 (seed 22); the live pattern is B.
bool storeAB(ProjectSession& s) {
  CHECK(fixture::ready(s, true, 21, 2));
  CHECK(s.storeSnapshot(0).ok());
  CHECK(s.generate(fixture::settings(22, 2)).ok());
  CHECK(s.storeSnapshot(1).ok());
  return s.hasSnapshot(0) && s.hasSnapshot(1);
}

std::vector<std::uint8_t> patternBytes(const ProjectSession& s) { return pattern::serialize(*s.pattern()); }

bool sameEvents(const FlatEventList& a, const FlatEventList& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a[i].id != b[i].id || a[i].chop != b[i].chop || a[i].start != b[i].start || a[i].duration != b[i].duration || a[i].tx.level != b[i].tx.level) return false;
  return true;
}

}  // namespace

CHOP_TEST(begin_requires_both_slots_and_a_source) {
  ProjectSession empty;
  CHECK(!empty.beginMorph(0.5).ok());
  ProjectSession s;
  CHECK(fixture::ready(s, true, 21, 2));
  auto none = s.beginMorph(0.5);
  CHECK(!none.ok() && none.error().message == "Store A and B first.");
  CHECK(s.storeSnapshot(0).ok());
  CHECK(!s.beginMorph(0.5).ok());  // only A
  CHECK(s.generate(fixture::settings(22, 2)).ok());
  CHECK(s.storeSnapshot(1).ok());
  CHECK(!s.beginMorph(-0.1).ok() && !s.beginMorph(1.1).ok() && !s.beginMorph(std::nan("")).ok());
  CHECK(!s.morphActive());
  CHECK(s.beginMorph(0.5).ok() && s.morphActive() && s.morphAmount() == 0.5);
}

CHOP_TEST(preview_publishes_a_playback_without_touching_pattern_history_or_undo) {
  ProjectSession s;
  CHECK(storeAB(s));
  const auto bytes = patternBytes(s);
  const std::size_t nodes = s.history().listAll().size();
  const auto savedBefore = s.saveState(false).value();
  const auto before = s.playback()->events;
  CHECK(s.beginMorph(0.5).ok());
  for (double t : {0.1, 0.3, 0.9, 0.0, 1.0}) CHECK(s.setMorph(t).ok());
  CHECK(patternBytes(s) == bytes && s.history().listAll().size() == nodes);
  CHECK(s.saveState(false).value() == savedBefore);
  CHECK(s.hasSnapshot(0) && s.hasSnapshot(1));
  CHECK(s.setMorph(0.5).ok() && !sameEvents(s.playback()->events, before));
  s.cancelMorph();
  CHECK(!s.morphActive() && sameEvents(s.playback()->events, before));
  CHECK(s.undo());  // the undo stack is exactly what it was: one step back is the generate before B
  CHECK(patternBytes(s) != bytes);
}

CHOP_TEST(preview_audio_at_the_endpoints_equals_a_and_b) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 21, 2));
  render::OfflineSettings rs;
  rs.cycles = 1;
  const auto audioA = s.renderOffline(rs).value();
  CHECK(s.storeSnapshot(0).ok());
  CHECK(s.generate(fixture::settings(22, 2)).ok());
  const auto audioB = s.renderOffline(rs).value();
  CHECK(s.storeSnapshot(1).ok());
  CHECK(audioA != audioB);
  CHECK(s.beginMorph(0.0).ok());
  CHECK(s.renderOffline(rs).value() == audioA);
  CHECK(s.setMorph(1.0).ok());
  CHECK(s.renderOffline(rs).value() == audioB);
  CHECK(s.setMorph(0.5).ok());
  const auto mid = s.renderOffline(rs).value();
  CHECK(mid != audioA && mid != audioB);
}

CHOP_TEST(commit_creates_a_node_labelled_with_the_percent_and_one_undo_step) {
  ProjectSession s;
  CHECK(storeAB(s));
  const auto before = patternBytes(s);
  const std::size_t nodes = s.history().listAll().size();
  CHECK(s.beginMorph(0.37).ok());
  const auto previewEvents = s.playback()->events;
  CHECK(s.commitMorph().ok());
  CHECK(!s.morphActive());
  CHECK(s.history().listAll().size() == nodes + 1);
  CHECK(s.history().listAll().back().label == "Morph A>B 37%");
  CHECK(sameEvents(s.playback()->events, previewEvents));  // the committed pattern plays what the preview played
  CHECK(hasNotice(s, "Committed the morph as a new variation."));
  CHECK(patternBytes(s) != before);
  CHECK(s.undo());
  CHECK(patternBytes(s) == before);
  CHECK(s.commitMorph().ok() == false);  // nothing to commit any more
}

CHOP_TEST(commit_with_a_custom_label_and_cancel_leaves_the_pattern_alone) {
  ProjectSession s;
  CHECK(storeAB(s));
  const auto before = patternBytes(s);
  CHECK(s.beginMorph(0.6).ok());
  s.cancelMorph();
  CHECK(patternBytes(s) == before && !s.morphActive());
  CHECK(s.takeNotices().empty());
  CHECK(s.beginMorph(0.6).ok());
  CHECK(s.commitMorph("my blend").ok());
  CHECK(s.history().listAll().back().label == "my blend");
}

CHOP_TEST(other_commands_end_the_morph) {
  const char* ended = "Morph ended because the pattern was changed.";
  auto fresh = [](ProjectSession& s) {
    CHECK(storeAB(s));
    CHECK(s.beginMorph(0.4).ok());
    s.takeNotices();
  };
  {
    ProjectSession s;
    fresh(s);
    CHECK(s.generate(fixture::settings(30, 2)).ok());
    CHECK(!s.morphActive() && hasNotice(s, ended));
  }
  {
    ProjectSession s;
    fresh(s);
    pattern::MutateOptions mo;
    mo.seed = 3;
    mo.amount = 0.5;
    CHECK(s.mutate(mo, nullptr).ok());
    CHECK(!s.morphActive() && hasNotice(s, ended));
  }
  {
    ProjectSession s;
    fresh(s);
    CHECK(s.undo());
    CHECK(!s.morphActive() && hasNotice(s, ended));
  }
  {
    ProjectSession s;
    fresh(s);
    CHECK(s.recallSnapshot(0).ok());
    CHECK(!s.morphActive() && hasNotice(s, ended));
  }
  {
    ProjectSession s;
    fresh(s);
    CHECK(s.activate(s.history().listAll().front().id, Quantize::Immediate).ok());
    CHECK(!s.morphActive() && hasNotice(s, ended));
  }
  {
    ProjectSession s;
    fresh(s);
    const auto bytes = s.saveState(false).value();
    CHECK(s.loadState(bytes.data(), bytes.size(), [](const source::SourceInfo&) { return fixture::loopAudio(); }).ok());
    CHECK(!s.morphActive() && hasNotice(s, ended));
  }
  {
    ProjectSession s;
    fresh(s);
    CHECK(s.loadSource(fixture::loopAudio(), "other").ok());
    CHECK(!s.morphActive() && hasNotice(s, ended));
  }
  {
    ProjectSession s;
    fresh(s);
    evolve::Settings es;
    es.enabled = true;
    auto r = s.setEvolve(es);
    CHECK(!r.ok() && r.error().message == "Cancel the morph before switching Evolve on.");
    CHECK(s.morphActive());
    s.cancelMorph();
    CHECK(s.setEvolve(es).ok());
    CHECK(!s.beginMorph(0.5).ok());  // and not the other way round either
  }
  {
    ProjectSession s;
    fresh(s);
    CHECK(s.edit([](const pattern::Pattern& p, const ChopSnapshot&) -> Result<pattern::Pattern> { return pattern::clearLocks(p); }).ok());
    CHECK(!s.morphActive() && hasNotice(s, ended));
  }
}

CHOP_TEST(store_slot_while_morphing_refreshes_the_preview) {
  ProjectSession s;
  CHECK(storeAB(s));
  CHECK(s.beginMorph(0.5).ok());
  const auto before = s.playback()->events;
  CHECK(s.storeSnapshot(0).ok());  // slot A now holds the live pattern (B): A == B, so the blend is B
  CHECK(s.morphActive());
  CHECK(!sameEvents(s.playback()->events, before));
  // A == B == the live pattern, so the blend plays the live pattern's own events
  const auto live = pattern::flatten(*s.pattern(), *s.chops()).value();
  CHECK(s.playback()->events.size() >= live.size());
}

CHOP_TEST(failed_set_morph_keeps_previous_preview) {
  ProjectSession s;
  CHECK(storeAB(s));
  CHECK(s.beginMorph(0.3).ok());
  const auto preview = s.playback()->events;
  CHECK(!s.setMorph(std::nan("")).ok() && !s.setMorph(-1).ok() && !s.setMorph(2).ok());
  CHECK(s.morphAmount() == 0.3 && sameEvents(s.playback()->events, preview) && s.morphActive());
  ProjectSession idle;
  CHECK(storeAB(idle));
  CHECK(!idle.setMorph(0.5).ok());  // not started
  // a pattern whose length differs from the stored slot cannot be morphed, and the failure leaves things as they were
  ProjectSession d;
  CHECK(fixture::ready(d, true, 21, 2));
  CHECK(d.storeSnapshot(0).ok());
  CHECK(d.generate(fixture::settings(22, 4)).ok());
  CHECK(d.storeSnapshot(1).ok());
  auto r = d.beginMorph(0.5);
  CHECK(!r.ok() && r.error().code == ErrorCode::Conflict && !d.morphActive());
  CHECK(r.error().message == "A and B differ in pattern length, meter or grid, so they cannot be morphed.");
}

CHOP_TEST(morph_survives_marker_edits_via_sanitize) {
  ProjectSession s;
  CHECK(storeAB(s));
  CHECK(s.beginMorph(0.5).ok());
  s.takeNotices();
  const ChopId victim = s.chops()->chops[2].id;
  CHECK(s.editMarkers([&](source::ChopMap& m) { return m.deleteMarker(victim); }).ok());
  CHECK(s.morphActive());
  for (const FlatEvent& e : s.playback()->events) CHECK(e.chop != victim);
  CHECK(s.setMorph(0.8).ok());
  CHECK(s.undoMarkers().ok());
  CHECK(s.morphActive());
}

CHOP_TEST(ab_slots_persist_and_morph_works_after_reload) {
  ProjectSession s;
  CHECK(storeAB(s));
  CHECK(s.beginMorph(0.5).ok());
  const auto bytes = s.saveState(false).value();
  ProjectSession t;
  CHECK(t.loadState(bytes.data(), bytes.size(), [](const source::SourceInfo&) { return fixture::loopAudio(); }).ok());
  CHECK(!t.morphActive());
  CHECK(t.hasSnapshot(0) && t.hasSnapshot(1));
  CHECK(t.beginMorph(0.25).ok() && t.morphActive());
}

CHOP_TEST(display_pattern_returns_the_preview_while_active) {
  ProjectSession s;
  CHECK(storeAB(s));
  CHECK(s.displayPattern() == s.pattern());
  CHECK(s.beginMorph(0.5).ok());
  CHECK(s.displayPattern() != s.pattern() && s.displayPattern() != nullptr);
  CHECK(pattern::serialize(*s.displayPattern()) != pattern::serialize(*s.pattern()));
  CHECK(s.setMorph(1.0).ok());
  CHECK(pattern::serialize(*s.displayPattern()) == pattern::serialize(*s.pattern()));  // t = 1 is B, which is the live pattern
  s.cancelMorph();
  CHECK(s.displayPattern() == s.pattern());
  ProjectSession none;
  CHECK(none.displayPattern() == nullptr);
}

CHOP_TEST(morph_with_gesture_effects_commits_a_valid_pattern) {
  ProjectSession s;
  CHECK(storeAB(s));
  pattern::FxPassSettings fx;
  fx.echoAmount = 0.6;
  CHECK(s.setFx(fx, render::PlaybackOptions{}).ok());
  s.takeNotices();
  CHECK(s.hasSnapshot(0) && s.hasSnapshot(1));
  CHECK(s.beginMorph(0.5).ok());
  CHECK(s.commitMorph().ok());
  CHECK(pattern::validate(*s.pattern()).ok());
}

// Slider drags in the inspector preview a hit edit on the audio side only: no pattern change, no undo step, no history node.
CHOP_TEST(preview_edit_publishes_audio_without_touching_the_pattern_or_history) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 31, 2));
  const auto before = patternBytes(s);
  const auto playbackBefore = s.playback();
  const std::size_t nodes = s.history().size();
  EventId target;
  for (const auto& bar : s.pattern()->bars)
    for (const auto& beat : bar.beats)
      if (!target.valid() && !beat.events.empty()) target = beat.events[0].id;
  CHECK(target.valid());
  auto quiet = [&](float level) {
    return s.previewEdit([&](const pattern::Pattern& p, const ChopSnapshot&) {
      const Event* e = pattern::findEvent(p, target);
      EventTransform tx = e->tx;
      tx.level = level;
      return pattern::setEventTransform(p, target, tx);
    });
  };
  CHECK(quiet(0.25f).ok());
  CHECK(s.playback() != playbackBefore);  // the renderer got a new playback
  CHECK(patternBytes(s) == before);       // the stored pattern did not move
  CHECK_EQ(s.history().size(), nodes);
  CHECK(quiet(0.5f).ok());  // repeated previews are fine and always start from the stored pattern
  CHECK(patternBytes(s) == before);
  CHECK(s.cancelEditPreview().ok());  // dropping the preview restores the stored pattern's audio
  CHECK(!s.previewEdit([&](const pattern::Pattern& p, const ChopSnapshot&) { return pattern::setEventTransform(p, EventId{9999999}, EventTransform{}); }).ok());
  ProjectSession none;
  CHECK(!none.previewEdit([](const pattern::Pattern& p, const ChopSnapshot&) -> Result<pattern::Pattern> { return p; }).ok());
}
