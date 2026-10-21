#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/composition/source_replace.hpp>
#include <chopfractal/composition/stretch.hpp>

#include <algorithm>
#include <cmath>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {

constexpr std::int64_t kBeat = 30000;  // 96 BPM at 48 kHz
constexpr std::int64_t kLoopFrames = 4 * kBeat;  // 2.5 s = one bar

// Four decaying noise clicks, one per beat, over a quiet tone.
std::shared_ptr<const render::SourceData> clickLoop(std::int64_t frames = kLoopFrames, int channels = 1) {
  auto d = std::make_shared<render::SourceData>();
  d->channels = channels;
  d->sampleRate = 48000;
  d->frames = frames;
  d->samples.assign(static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels), 0.f);
  Rng rng(11);
  for (int c = 0; c < channels; ++c)
    for (std::int64_t i = 0; i < frames; ++i) {
      const std::int64_t inBeat = i % kBeat;
      float v = 0.02f * std::sin(static_cast<float>(i) * 0.05f);
      if (inBeat < 3000) v += static_cast<float>(rng.uniform01() * 2.0 - 1.0) * std::exp(-static_cast<float>(inBeat) / 700.f) * 0.8f;
      d->samples[static_cast<std::size_t>(c) * static_cast<std::size_t>(frames) + static_cast<std::size_t>(i)] = v;
    }
  return d;
}

pattern::Settings settings() {
  pattern::Settings s;
  s.bars = 2;
  s.density = 0.8;
  s.seed = 7;
  s.variation = {0.6, 0.6, 0.6, 0.7};
  return s;
}

void setup(ProjectSession& s, bool withPattern = true, const std::string& path = "/music/loop.wav") {
  CHECK(s.loadSource(clickLoop(), "loop", path).ok());
  CHECK(s.editMarkers([](source::ChopMap& m) -> Status {
          for (int i = 0; i < 4; ++i)
            if (!m.addMarker(i * kBeat).ok()) return makeError(ErrorCode::Conflict, "marker");
          return Status{};
        }).ok());
  CHECK_EQ(s.chops()->chops.size(), 4u);
  if (withPattern) CHECK(s.generate(settings()).ok());
}

StretchRequest req(double src, double tgt, stretch::Quality q = stretch::Quality::Draft) { return StretchRequest{src, tgt, q}; }

bool stretchNow(ProjectSession& s, const StretchRequest& r) {
  auto plan = s.planStretch(r);
  CHECK(plan.ok());
  if (!plan.ok()) return false;
  auto out = runStretch(plan.value());
  CHECK(out.ok());
  if (!out.ok()) return false;
  const Status st = s.applyStretched(plan.value(), out.value());
  CHECK(st.ok());
  return st.ok();
}

}  // namespace

CHOP_TEST(stretch_keeps_roles_pattern_rhythm_and_chop_ids) {
  ProjectSession s;
  setup(s);
  CHECK(s.assignRole(s.chops()->chops[0].id, "kick").ok());
  CHECK(s.assignRole(s.chops()->chops[2].id, "snare").ok());
  const pattern::Pattern& p0 = *s.pattern();
  const Event* hit = nullptr;
  for (const pattern::Beat& bt : p0.bars[0].beats)
    if (!bt.events.empty() && !hit) hit = &bt.events.front();
  CHECK(hit != nullptr);
  if (!hit) return;
  zoom::Settings zs;
  zs.subdivisions = 3;
  zs.density = zoom::Density::Dense;
  CHECK(s.zoomIn(hit->id, zs, 5).ok());

  const auto oldFlat = pattern::flatten(*s.pattern(), *s.chops()).value();
  const ChopSnapshot oldChops = *s.chops();
  const auto oldRoles = s.roleMap().all();
  const std::int64_t outFrames = std::llround(static_cast<double>(kLoopFrames) * 96.0 / 120.0);

  CHECK(stretchNow(s, req(96.0, 120.0)));

  CHECK_EQ(s.chopMap()->info().frames, outFrames);
  CHECK_EQ(s.chopMap()->info().frames, 96000);
  CHECK(s.chopMap()->info().path.empty());
  CHECK(s.chopMap()->info().name == "loop @120.0 BPM");
  CHECK_EQ(s.sourceData()->frames, outFrames);
  CHECK_EQ(s.chops()->chops.size(), oldChops.chops.size());
  for (std::size_t i = 0; i < oldChops.chops.size(); ++i) {
    CHECK(s.chops()->chops[i].id == oldChops.chops[i].id);
    CHECK_EQ(s.chops()->chops[i].range.start, stretch::mapPosition(oldChops.chops[i].range.start, kLoopFrames, outFrames));
    CHECK_EQ(s.chops()->chops[i].range.end, stretch::mapPosition(oldChops.chops[i].range.end, kLoopFrames, outFrames));
  }
  CHECK(s.roleMap().all() == oldRoles);
  const auto newFlat = pattern::flatten(*s.pattern(), *s.chops()).value();
  CHECK_EQ(newFlat.size(), oldFlat.size());
  for (std::size_t i = 0; i < std::min(newFlat.size(), oldFlat.size()); ++i) {
    CHECK(newFlat[i].id == oldFlat[i].id);
    CHECK(newFlat[i].chop == oldFlat[i].chop);
    CHECK_EQ(newFlat[i].start, oldFlat[i].start);  // musical time: the rhythm is untouched
    CHECK_EQ(newFlat[i].duration, oldFlat[i].duration);
    CHECK_EQ(newFlat[i].region.start, stretch::mapPosition(oldFlat[i].region.start, kLoopFrames, outFrames));
    CHECK_EQ(newFlat[i].region.end, stretch::mapPosition(oldFlat[i].region.end, kLoopFrames, outFrames));
  }
  // History restarts with exactly one node.
  CHECK_EQ(s.history().size(), 1u);
  const auto all = s.history().listAll();
  CHECK(all.size() == 1 && all[0].label == "Stretched to 120.0 BPM");
  CHECK(s.playback() && !s.playback()->passThrough && s.playback()->source == s.sourceData());
  const auto notes = s.takeNotices();
  CHECK(!notes.empty() && notes.back().text == "Stretched to 120.0 BPM (Draft).");
  // The click attacks survived the stretch: the first sample after each scaled marker is the original click start.
  for (const ChopInfo& c : s.chops()->chops) {
    float peak = 0.f;
    for (std::int64_t i = 0; i < 40; ++i) peak = std::max(peak, std::fabs(s.sourceData()->samples[static_cast<std::size_t>(c.range.start + i)]));
    CHECK(peak > 0.3f);
  }
}

CHOP_TEST(stretch_works_without_a_pattern_and_for_stereo) {
  ProjectSession s;
  CHECK(s.loadSource(clickLoop(kLoopFrames, 2), "loop").ok());
  CHECK(s.editMarkers([](source::ChopMap& m) -> Status { return m.addMarker(kBeat).ok() ? Status{} : makeError(ErrorCode::Conflict, "x"); }).ok());
  CHECK(stretchNow(s, req(96.0, 72.0, stretch::Quality::Standard)));
  CHECK_EQ(s.sourceData()->channels, 2);
  CHECK_EQ(s.sourceData()->frames, std::llround(kLoopFrames * 96.0 / 72.0));
  CHECK(s.pattern() == nullptr);
  CHECK(s.playback() && s.playback()->passThrough);
  CHECK_EQ(s.history().size(), 0u);
}

CHOP_TEST(stretch_plan_refuses_out_of_range_and_oversize) {
  ProjectSession empty;
  CHECK(!empty.planStretch(req(96, 120)).ok());
  ProjectSession s;
  setup(s);
  auto expectCode = [&](const StretchRequest& r, ErrorCode code) {
    auto p = s.planStretch(r);
    CHECK(!p.ok());
    if (!p.ok()) CHECK(p.error().code == code);
  };
  expectCode(req(96, 20), ErrorCode::OutOfRange);        // ratio 4.8
  expectCode(req(20, 96), ErrorCode::OutOfRange);        // ratio 0.208
  expectCode(req(96, 1000), ErrorCode::OutOfRange);      // BPM above 999
  expectCode(req(10, 20), ErrorCode::OutOfRange);        // BPM below 20
  expectCode(req(std::nan(""), 100), ErrorCode::OutOfRange);
  expectCode(req(96, 96.04), ErrorCode::InvalidArgument);  // within 0.05
  CHECK(!s.planStretch(req(96, 96.04)).ok() && s.planStretch(req(96, 96.04)).error().message == "Already at the target tempo.");
  CHECK(s.planStretch(req(96, 192)).ok());                // exactly 0.5x
  CHECK(s.planStretch(req(96, 48)).ok());                 // exactly 2x
  CHECK(!s.planStretch(req(96, 192.1)).ok());

  ProjectSession big;
  auto d = std::make_shared<render::SourceData>();
  d->channels = 1;
  d->sampleRate = 48000;
  d->frames = 40 * 48000;
  d->samples.assign(static_cast<std::size_t>(d->frames), 0.f);
  d->samples[100] = 0.5f;
  CHECK(big.loadSource(d, "long").ok());
  auto over = big.planStretch(req(100, 50));  // 80 s
  CHECK(!over.ok());
  if (!over.ok()) {
    CHECK(over.error().code == ErrorCode::LimitExceeded);
    CHECK(over.error().message == "The stretched audio would be 80.0 s; the maximum is 60 s.");
    CHECK_EQ(over.error().hint, 60 * 48000);
  }
  CHECK(big.planStretch(req(100, 70)).ok());  // 57.1 s

  // A project whose audio is missing cannot be stretched.
  ProjectSession withPattern;
  setup(withPattern);
  auto bytes = withPattern.saveState(false);
  CHECK(bytes.ok());
  ProjectSession missing;
  CHECK(missing.loadState(bytes.value().data(), bytes.value().size(), nullptr).ok());
  CHECK(missing.sourceMissing());
  auto m = missing.planStretch(req(96, 120));
  CHECK(!m.ok() && m.error().code == ErrorCode::NotFound);
}

CHOP_TEST(stretch_plan_reports_marker_collisions_with_count_before_any_work) {
  ProjectSession s;
  CHECK(s.loadSource(clickLoop(), "loop").ok());
  CHECK(s.editMarkers([](source::ChopMap& m) -> Status {
          for (std::int64_t p : {1000, 1100, 1200, 50000}) m.addMarker(p);
          return Status{};
        }).ok());
  const auto before = s.saveState(true).value();
  auto p = s.planStretch(req(100, 200));  // 0.5x: 100 frames become 50
  CHECK(!p.ok());
  if (!p.ok()) {
    CHECK(p.error().code == ErrorCode::Conflict);
    CHECK_EQ(p.error().hint, 2);
    CHECK(p.error().message == "Stretching to 200.0 BPM would make 2 chops shorter than 64 frames. Remove or merge tiny chops first, or use a smaller change.");
  }
  CHECK(s.saveState(true).value() == before);
}

CHOP_TEST(stretch_apply_conflicts_if_source_changed) {
  ProjectSession s;
  setup(s);
  auto plan = s.planStretch(req(96, 120));
  CHECK(plan.ok());
  if (!plan.ok()) return;
  auto out = runStretch(plan.value());
  CHECK(out.ok());
  if (!out.ok()) return;
  auto other = std::make_shared<render::SourceData>(*clickLoop());
  other->samples[5] += 0.25f;  // same length, different audio
  CHECK(s.loadSource(other, "other", "/x.wav").ok());
  const auto before = s.saveState(true).value();
  const Status st = s.applyStretched(plan.value(), out.value());
  CHECK(!st.ok());
  if (!st.ok()) {
    CHECK(st.error().code == ErrorCode::Conflict);
    CHECK(st.error().message == "The source changed while stretching; nothing was applied.");
  }
  CHECK(s.saveState(true).value() == before);
  ProjectSession cleared;
  CHECK(!cleared.applyStretched(plan.value(), out.value()).ok());
}

CHOP_TEST(stretch_apply_is_all_or_nothing) {
  ProjectSession s;
  setup(s);
  auto plan = s.planStretch(req(96, 120));
  CHECK(plan.ok());
  if (!plan.ok()) return;
  auto out = runStretch(plan.value());
  CHECK(out.ok());
  if (!out.ok()) return;
  const auto before = s.saveState(true).value();
  const auto playbackBefore = s.playback();

  // 1) A stretched buffer that does not match the plan is refused.
  auto wrong = std::make_shared<render::SourceData>(*out.value());
  wrong->frames -= 1;
  wrong->samples.resize(static_cast<std::size_t>(wrong->frames));
  CHECK(!s.applyStretched(plan.value(), wrong).ok());
  auto stereo = std::make_shared<render::SourceData>(*out.value());
  stereo->channels = 2;
  CHECK(!s.applyStretched(plan.value(), stereo).ok());
  CHECK(!s.applyStretched(plan.value(), nullptr).ok());
  CHECK(s.saveState(true).value() == before);

  // 2) Markers edited after planning so that they collide at the new scale: the rescale fails, nothing changes.
  CHECK(s.editMarkers([](source::ChopMap& m) -> Status { return m.addMarker(kBeat + 70).ok() ? Status{} : makeError(ErrorCode::Conflict, "x"); }).ok());
  const auto edited = s.saveState(true).value();
  const Status st = s.applyStretched(plan.value(), out.value());
  CHECK(!st.ok());
  if (!st.ok()) CHECK(st.error().code == ErrorCode::Conflict);
  CHECK(s.saveState(true).value() == edited);
  CHECK(s.playback() != nullptr);
  (void)playbackBefore;
}

CHOP_TEST(stretched_project_roundtrips_embedded) {
  ProjectSession s;
  setup(s);
  CHECK(s.sourceNeedsEmbed() == false);  // file-backed source
  CHECK(stretchNow(s, req(96, 120)));
  CHECK(s.sourceNeedsEmbed());
  auto saved = s.saveState(s.sourceNeedsEmbed());
  CHECK(saved.ok());
  if (!saved.ok()) return;
  ProjectSession t;
  CHECK(t.loadState(saved.value().data(), saved.value().size(), nullptr).ok());  // no file to resolve: audio rides in the project
  CHECK(t.hasSource() && !t.sourceMissing());
  CHECK(t.wasEmbedded());
  CHECK_EQ(t.chopMap()->info().frames, 96000);
  CHECK(t.chopMap()->info().name == "loop @120.0 BPM");
  CHECK(t.sourceData()->samples == s.sourceData()->samples);
  CHECK(t.chopMap()->serialize() == s.chopMap()->serialize());
  CHECK(pattern::serialize(*t.pattern()) == pattern::serialize(*s.pattern()));
  CHECK(t.history().size() == 1);
  CHECK(t.saveState(true).value() == s.saveState(true).value());
  // Stretching again replaces the suffix instead of stacking it.
  CHECK(stretchNow(t, req(120, 100)));
  CHECK(t.chopMap()->info().name == "loop @100.0 BPM");
  CHECK(!(ProjectSession{}.sourceNeedsEmbed()));
}

CHOP_TEST(stretch_then_undo_restores_original_bytes) {
  ProjectSession s;
  setup(s);
  CHECK(s.assignRole(s.chops()->chops[1].id, "hat").ok());
  const auto original = s.saveState(true).value();
  SourceReplaceUndo undo;
  CHECK(!undo.available());
  CHECK(!undo.restore(s).ok());
  CHECK(undo.capture(s).ok());
  CHECK(undo.available());
  CHECK(stretchNow(s, req(96, 120)));
  CHECK(s.saveState(true).value() != original);
  CHECK(undo.restore(s).ok());
  CHECK(!undo.available());
  CHECK(s.saveState(true).value() == original);
  CHECK_EQ(s.sourceData()->frames, kLoopFrames);
  CHECK(s.playback() && !s.playback()->passThrough);
  // A later capture replaces the earlier one; clear() ends the window.
  CHECK(undo.capture(s).ok());
  CHECK(undo.capture(s).ok());
  undo.clear();
  CHECK(!undo.available());
}

CHOP_TEST(map_position_matches_chop_map_rescale_and_helpers) {
  source::SourceInfo info;
  info.id = SourceId{5};
  info.name = "x";
  info.sampleRate = 48000;
  info.frames = 123457;
  auto m = source::ChopMap::create(info);
  CHECK(m.ok());
  if (!m.ok()) return;
  for (std::int64_t p = 100; p < 123000; p += 4099) CHECK(m.value().addMarker(p).ok());
  source::SourceInfo ni = info;
  ni.frames = 98766;
  auto r = source::ChopMap::rescaled(m.value(), ni, 123457, 98766);
  CHECK(r.ok());
  if (!r.ok()) return;
  for (std::size_t i = 0; i < m.value().markers().size(); ++i)
    CHECK_EQ(r.value().markers()[i].position, stretch::mapPosition(m.value().markers()[i].position, 123457, 98766));

  CHECK(looksStretched("loop @120.0 BPM"));
  CHECK(looksStretched("a @b @96 BPM"));
  CHECK(!looksStretched("loop"));
  CHECK(!looksStretched("loop @x BPM"));
  CHECK(!looksStretched("loop BPM"));
  CHECK(!looksStretched("@ BPM"));
  CHECK_EQ(maxStretchFrames(48000, 2), 60 * 48000);
  CHECK_EQ(maxStretchFrames(384000, 1), source::kMaxSourceFrames);

  const StretchPreview ok = previewStretch(96, 120, kLoopFrames, 48000, 1);
  CHECK(ok.canApply && ok.level == StretchPreview::Level::Ok);
  CHECK(ok.text == "96.0 -> 120.0 BPM: 125% speed, length x0.800 (2.5 s -> 2.0 s)");
  const StretchPreview warn = previewStretch(96, 160, kLoopFrames, 48000, 1);
  CHECK(warn.canApply && warn.level == StretchPreview::Level::Warning && warn.advice == "Large change: High quality is recommended.");
  const StretchPreview range = previewStretch(96, 20, kLoopFrames, 48000, 1);
  CHECK(!range.canApply && range.level == StretchPreview::Level::Error && range.text == "Out of range: the tempo change must stay between 0.5x and 2x.");
  const StretchPreview same = previewStretch(96, 96.03, kLoopFrames, 48000, 1);
  CHECK(!same.canApply && same.level == StretchPreview::Level::Hidden && same.text == "Already at the target tempo.");
  const StretchPreview big = previewStretch(100, 50, 40 * 48000, 48000, 1);
  CHECK(!big.canApply && big.text == "The stretched audio would be 80.0 s; the maximum is 60 s.");
  CHECK(!previewStretch(10, 50, 1000, 48000, 1).canApply);

  std::vector<insight::LoopSuggestion> sugg(4);
  for (int i = 0; i < 4; ++i) sugg[static_cast<std::size_t>(i)] = {i + 1, 1, 96.0 + i, 0.5, "note " + std::to_string(i)};
  const auto choices = loopLengthChoices(sugg, 2.5);
  CHECK_EQ(choices.size(), 3u + 5u + 1u);
  CHECK(choices[0].text == "note 0" && choices[0].kind == LoopLengthChoice::Kind::Suggestion);
  CHECK(choices[3].text == "1 bar" && choices[4].text == "2 bars");
  CHECK_NEAR(choices[3].bpm, 96.0, 1e-9);
  CHECK_NEAR(sourceBpmForBars(4, 10.0), 96.0, 1e-9);
  CHECK(choices.back().text == "Custom BPM" && choices.back().kind == LoopLengthChoice::Kind::Custom);

  // suggestLoop feeds the combo: a 4-bar loop at 96 BPM lasts 10 s, so its note is the first entry.
  const auto fromInsight = loopLengthChoices(insight::suggestLoop(10.0, 120.0), 10.0);
  CHECK(fromInsight.size() >= 6u);
  CHECK(fromInsight[0].kind == LoopLengthChoice::Kind::Suggestion);
  bool has96 = false;
  for (const LoopLengthChoice& c : fromInsight)
    if (c.text == "4 bars at 96.0 BPM") has96 = true;
  CHECK(has96);
}
