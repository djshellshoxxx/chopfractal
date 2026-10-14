// Integration tests for the A/B morph (docs/specs/roadmap/F10-ab-morph.md).
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <cmath>
#include <memory>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {

std::shared_ptr<const render::SourceData> loop() {
  auto d = std::make_shared<render::SourceData>();
  d->channels = 1;
  d->sampleRate = 48000;
  d->frames = 96000;
  d->samples.assign(96000, 0.f);
  Rng rng(3);
  for (int k = 0; k < 8; ++k)
    for (int i = 0; i < 5000; ++i)
      d->samples[static_cast<std::size_t>(k * 12000 + i)] = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * std::exp(-static_cast<float>(i) / 900.f) * 0.8f;
  return d;
}

pattern::Settings cfg(std::uint64_t seed) {
  pattern::Settings s;
  s.bars = 2;
  s.density = 0.7;
  s.seed = seed;
  return s;
}

bool ready(ProjectSession& s) {
  CHECK(s.loadSource(loop(), "loop", "/music/loop.wav").ok());
  DetectOptions d;
  d.analysis.sensitivity = 0.8;
  CHECK(s.applyChops(d).ok());
  CHECK(s.generate(cfg(1)).ok());
  CHECK(s.storeSnapshot(0).ok());
  CHECK(s.generate(cfg(2)).ok());
  CHECK(s.storeSnapshot(1).ok());
  return s.pattern() != nullptr;
}

std::vector<std::vector<float>> audio(const ProjectSession& s) {
  auto r = s.renderOffline(render::OfflineSettings{});
  CHECK(r.ok());
  return r.ok() ? r.value() : std::vector<std::vector<float>>(2);
}

}  // namespace

CHOP_TEST(morph_begin_requires_both_slots_and_a_source) {
  ProjectSession empty;
  CHECK(!empty.beginMorph().ok());
  ProjectSession s;
  CHECK(s.loadSource(loop(), "loop", "/m.wav").ok());
  DetectOptions d;
  d.analysis.sensitivity = 0.8;
  CHECK(s.applyChops(d).ok());
  CHECK(s.generate(cfg(1)).ok());
  CHECK(s.storeSnapshot(0).ok());
  auto st = s.beginMorph();
  CHECK(!st.ok() && st.error().message == "Store A and B first.");
  CHECK(!s.morphActive());
  CHECK(!s.setMorph(0.5).ok());
  CHECK(!s.commitMorph().ok());
}

CHOP_TEST(morph_preview_changes_playback_only_and_endpoints_match_a_and_b) {
  ProjectSession s;
  CHECK(ready(s));
  const auto bytesBefore = pattern::serialize(*s.pattern());
  const std::size_t nodes = s.history().size();
  const bool couldUndo = s.undo() && s.redo();
  CHECK(s.beginMorph(0.5).ok());
  CHECK(s.morphActive());
  for (double t : {0.1, 0.9, 0.37, 0.0, 1.0}) CHECK(s.setMorph(t).ok());
  CHECK(pattern::serialize(*s.pattern()) == bytesBefore);
  CHECK_EQ(s.history().size(), nodes);
  CHECK(couldUndo);
  CHECK(s.displayPattern() != s.pattern());
  // t = 1 equals B (the current pattern), t = 0 equals A
  const auto atB = audio(s);
  CHECK(s.setMorph(0.0).ok());
  const auto atA = audio(s);
  s.cancelMorph();
  const auto real = audio(s);
  CHECK(real == atB);
  CHECK(atA != atB);
  CHECK(s.recallSnapshot(0).ok());
  CHECK(audio(s) == atA);
  CHECK(s.displayPattern() == s.pattern());
}

CHOP_TEST(morph_commit_makes_one_labelled_node_and_one_undo_step) {
  ProjectSession s;
  CHECK(ready(s));
  const auto before = pattern::serialize(*s.pattern());
  const std::size_t nodes = s.history().size();
  CHECK(s.beginMorph(0.37).ok());
  const auto previewAudio = audio(s);
  CHECK(s.commitMorph().ok());
  CHECK(!s.morphActive());
  CHECK_EQ(s.history().size(), nodes + 1);
  CHECK(s.history().listAll().back().label == "Morph A>B 37%");
  CHECK(audio(s) == previewAudio);  // what was previewed is what plays
  CHECK(s.undo());
  CHECK(pattern::serialize(*s.pattern()) == before);
}

CHOP_TEST(other_commands_end_the_morph_with_a_notice) {
  auto armed = [](ProjectSession& s) {
    CHECK(ready(s));
    CHECK(s.beginMorph().ok());
    s.takeNotices();
  };
  auto ended = [](ProjectSession& s) {
    bool note = false;
    for (const auto& n : s.takeNotices()) note |= n.text == "Morph ended because the pattern was changed.";
    return !s.morphActive() && note;
  };
  { ProjectSession s; armed(s); CHECK(s.generate(cfg(5)).ok()); CHECK(ended(s)); }
  { ProjectSession s; armed(s); CHECK(s.mutate(pattern::MutateOptions{}).ok()); CHECK(ended(s)); }
  { ProjectSession s; armed(s); CHECK(s.undo()); CHECK(ended(s)); }
  { ProjectSession s; armed(s); CHECK(s.recallSnapshot(0).ok()); CHECK(ended(s)); }
  { ProjectSession s; armed(s); CHECK(s.activate(s.history().active(), Quantize::Immediate).ok()); CHECK(ended(s)); }
  { ProjectSession s; armed(s); auto st = s.saveState(true); CHECK(st.ok()); CHECK(s.loadState(st.value().data(), st.value().size(), nullptr).ok()); CHECK(ended(s)); }
  { ProjectSession s; armed(s); s.clearSource(); CHECK(ended(s)); }
  {
    ProjectSession s;
    armed(s);
    evolve::Settings e;
    e.enabled = true;
    auto st = s.setEvolve(e);
    CHECK(!st.ok() && st.error().message == "Cancel the morph before switching Evolve on.");
    CHECK(s.morphActive());
  }
  {
    ProjectSession s;
    CHECK(ready(s));
    evolve::Settings e;
    e.enabled = true;
    CHECK(s.setEvolve(e).ok());
    CHECK(!s.beginMorph().ok());
  }
}

CHOP_TEST(storing_a_slot_refreshes_the_preview_and_a_failed_update_keeps_it) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK(s.beginMorph(0.5).ok());
  const auto before = audio(s);
  CHECK(s.generate(cfg(9)).ok());  // ends the morph; restart it
  CHECK(s.beginMorph(0.5).ok());
  const auto mid = audio(s);
  CHECK(s.storeSnapshot(0).ok());  // A becomes the current pattern: the blend changes immediately
  CHECK(audio(s) != mid);
  CHECK(before.size() == 2);
  CHECK(!s.setMorph(1.5).ok());
  CHECK(!s.setMorph(std::nan("")).ok());
  CHECK_EQ(s.morphAmount(), 0.5);
  CHECK(s.morphActive());
}
