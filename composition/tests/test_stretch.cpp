// Integration tests for tempo matching (docs/specs/roadmap/F14-tempo-match-stretch.md).
#include <chopfractal/composition/project_session.hpp>
#include <cmath>
#include <memory>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {

// One bar at 96 BPM (2.5 s): a decaying tone burst on every beat.
std::shared_ptr<const render::SourceData> clickLoop() {
  auto d = std::make_shared<render::SourceData>();
  d->channels = 1;
  d->sampleRate = 48000;
  d->frames = 120000;
  d->samples.assign(120000, 0.f);
  for (int beat = 0; beat < 4; ++beat)
    for (int i = 0; i < 6000; ++i)
      d->samples[static_cast<std::size_t>(beat * 30000 + i)] = 0.8f * static_cast<float>(std::exp(-i / 1500.0) * std::sin(2.0 * 3.14159265358979 * (200.0 + 100.0 * beat) * i / 48000.0));
  return d;
}

pattern::Settings cfg() {
  pattern::Settings s;
  s.bars = 1;
  s.density = 0.9;
  s.seed = 4;
  return s;
}

struct Rig {
  ProjectSession s;
  Rig() {
    CHECK(s.loadSource(clickLoop(), "break", "/music/break.wav").ok());
    CHECK(s.editMarkers([](source::ChopMap& m) {
      for (int beat = 0; beat < 4; ++beat) {
        auto id = m.addMarker(beat * 30000, true);
        if (!id.ok()) return Status{id.error()};
      }
      return Status{};
    }).ok());
    CHECK_EQ(s.chops()->chops.size(), 4u);
    CHECK(s.assignRole(s.chops()->chops[0].id, "kick").ok());
    CHECK(s.assignRole(s.chops()->chops[1].id, "snare").ok());
    CHECK(s.generate(cfg()).ok());
  }
  std::shared_ptr<const render::SourceData> run(const StretchPlan& plan) {
    const auto& in = *plan.input;
    std::vector<const float*> ch;
    for (int c = 0; c < in.channels; ++c) ch.push_back(in.channel(c));
    stretch::Input si;
    si.channels = ch.data();
    si.numChannels = in.channels;
    si.frames = in.frames;
    si.sampleRate = in.sampleRate;
    auto out = stretch::stretch(si, plan.plan, plan.request.quality);
    CHECK(out.ok());
    if (!out.ok()) return nullptr;
    auto d = std::make_shared<render::SourceData>();
    d->channels = in.channels;
    d->sampleRate = in.sampleRate;
    d->frames = plan.plan.outFrames;
    d->samples = std::move(out.value());
    return d;
  }
};

}  // namespace

CHOP_TEST(stretch_keeps_roles_pattern_rhythm_and_chop_ids) {
  Rig rig;
  std::vector<ChopId> ids;
  std::vector<std::int64_t> starts;
  for (const auto& c : rig.s.chops()->chops) {
    ids.push_back(c.id);
    starts.push_back(c.range.start);
  }
  auto before = pattern::flatten(*rig.s.pattern(), *rig.s.chops());
  CHECK(before.ok());
  StretchRequest req;
  req.sourceBpm = 96.0;
  req.targetBpm = 120.0;
  req.quality = stretch::Quality::Draft;
  auto plan = rig.s.planStretch(req);
  CHECK(plan.ok());
  CHECK_EQ(plan.value().plan.outFrames, 96000);
  auto audio = rig.run(plan.value());
  CHECK(audio != nullptr);
  CHECK(rig.s.applyStretched(plan.value(), audio).ok());
  CHECK_EQ(rig.s.chopMap()->info().frames, 96000);
  CHECK(rig.s.chopMap()->info().name.find("@120.0 BPM") != std::string::npos);
  CHECK(rig.s.chopMap()->info().path.empty() && rig.s.sourceNeedsEmbed());
  for (std::size_t i = 0; i < ids.size(); ++i) {
    CHECK(rig.s.chops()->chops[i].id == ids[i]);  // ids survive, so roles and pattern references stay valid
    CHECK_EQ(rig.s.chops()->chops[i].range.start, stretch::mapPosition(starts[i], 120000, 96000));
  }
  CHECK(rig.s.roleMap().roleOf(ids[0]) == "kick" && rig.s.roleMap().roleOf(ids[1]) == "snare");
  auto after = pattern::flatten(*rig.s.pattern(), *rig.s.chops());
  CHECK(after.ok());
  CHECK_EQ(after.value().size(), before.value().size());
  for (std::size_t i = 0; i < after.value().size() && i < before.value().size(); ++i) {
    CHECK_EQ(after.value()[i].start, before.value()[i].start);  // musical time is unchanged
    CHECK(after.value()[i].chop == before.value()[i].chop);
  }
  CHECK_EQ(rig.s.history().size(), 1u);
  CHECK(rig.s.history().listAll().back().label == "Stretched to 120.0 BPM");
  // the stretched project round-trips with its audio embedded
  auto saved = rig.s.saveState(true);
  CHECK(saved.ok());
  ProjectSession t;
  CHECK(t.loadState(saved.value().data(), saved.value().size(), nullptr).ok());
  CHECK(!t.sourceMissing() && t.chopMap()->info().frames == 96000);
}

CHOP_TEST(stretch_plan_refuses_out_of_range_oversize_and_missing_sources) {
  Rig rig;
  StretchRequest req;
  req.sourceBpm = 96.0;
  req.targetBpm = 20.0;  // ratio 4.8
  auto far = rig.s.planStretch(req);
  CHECK(!far.ok() && far.error().message == "Out of range: the tempo change must stay between 0.5x and 2x.");
  req.targetBpm = 96.02;
  auto same = rig.s.planStretch(req);
  CHECK(!same.ok() && same.error().message == "Already at the target tempo.");
  req.targetBpm = 5000.0;
  CHECK(!rig.s.planStretch(req).ok());
  ProjectSession empty;
  req.targetBpm = 120.0;
  CHECK(!empty.planStretch(req).ok());
  // an almost-60 s loop that would exceed 60 s when slowed down
  ProjectSession big;
  auto d = std::make_shared<render::SourceData>();
  d->channels = 1;
  d->sampleRate = 48000;
  d->frames = 48000 * 50;
  d->samples.assign(static_cast<std::size_t>(d->frames), 0.f);
  d->samples[0] = 1.f;
  CHECK(big.loadSource(d, "long", "/x.wav").ok());
  req.sourceBpm = 120.0;
  req.targetBpm = 80.0;  // 50 s * 1.5 = 75 s
  auto over = big.planStretch(req);
  CHECK(!over.ok() && over.error().code == ErrorCode::LimitExceeded);
  CHECK(over.error().message == "The stretched audio would be 75.0 s; the maximum is 60 s.");
}

CHOP_TEST(stretch_apply_conflicts_if_the_source_changed_and_is_all_or_nothing) {
  Rig rig;
  StretchRequest req;
  req.sourceBpm = 96.0;
  req.targetBpm = 120.0;
  req.quality = stretch::Quality::Draft;
  auto plan = rig.s.planStretch(req);
  CHECK(plan.ok());
  auto audio = rig.run(plan.value());
  const auto before = rig.s.saveState(true);
  CHECK(before.ok());
  // the user loads something else while the worker runs
  auto other = std::make_shared<render::SourceData>(*clickLoop());
  other->samples[5] = 0.5f;
  Rig second;
  CHECK(second.s.loadSource(other, "other", "/other.wav").ok());
  auto conflict = second.s.applyStretched(plan.value(), audio);
  CHECK(!conflict.ok() && conflict.error().code == ErrorCode::Conflict);
  CHECK(conflict.error().message == "The source changed while stretching; nothing was applied.");
  // markers that would collapse refuse the whole operation and leave the session untouched
  CHECK(rig.s.editMarkers([](source::ChopMap& m) {
    auto a = m.addMarker(31000, true);
    auto b = m.addMarker(31100, true);
    return a.ok() && b.ok() ? Status{} : Status{makeError(ErrorCode::Conflict, "setup")};
  }).ok());
  const auto marked = rig.s.saveState(true);
  StretchRequest half;
  half.sourceBpm = 120.0;
  half.targetBpm = 240.0;  // length x0.5
  half.quality = stretch::Quality::Draft;
  auto p2 = rig.s.planStretch(half);
  CHECK(p2.ok());
  auto a2 = rig.run(p2.value());
  auto refused = rig.s.applyStretched(p2.value(), a2);
  CHECK(!refused.ok() && refused.error().code == ErrorCode::Conflict);
  CHECK(refused.error().message.find("chops shorter than 64 frames") != std::string::npos);
  CHECK(rig.s.saveState(true).value() == marked.value());
  CHECK(!rig.s.applyStretched(p2.value(), nullptr).ok());
}
