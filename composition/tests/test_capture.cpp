#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/composition/source_replace.hpp>

#include <cmath>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {
// Four beats of a decaying click over a quiet tone, as a stereo capture would deliver it (planar).
std::shared_ptr<const render::SourceData> clickLoop() {
  auto d = std::make_shared<render::SourceData>();
  d->channels = 2;
  d->sampleRate = 48000;
  d->frames = 96000;
  d->samples.assign(2 * 96000, 0.f);
  for (int c = 0; c < 2; ++c)
    for (std::int64_t i = 0; i < d->frames; ++i) {
      const std::int64_t inBeat = i % 24000;
      float v = 0.01f * std::sin(static_cast<float>(i) * 0.03f);
      if (inBeat < 2000) v += (((i * 7919) % 2000) / 1000.f - 1.f) * std::exp(-static_cast<float>(inBeat) / 500.f) * 0.8f;
      d->samples[static_cast<std::size_t>(c * d->frames + i)] = v;
    }
  return d;
}
}  // namespace

CHOP_TEST(captured_source_loads_chops_and_roundtrips_embedded) {
  ProjectSession s;
  CHECK(!s.sourceNeedsEmbed());
  CHECK(s.loadCapturedSource(clickLoop(), "Capture 2026-10-21 10:30").ok());
  CHECK(s.hasSource() && s.chopMap()->info().path.empty());
  CHECK(s.chopMap()->info().name == "Capture 2026-10-21 10:30");
  CHECK(s.sourceNeedsEmbed());
  auto setup = s.smartSetup(120.0);
  CHECK(setup.ok());
  CHECK(s.chopMap()->markers().size() >= 4);

  auto saved = s.saveState(s.sourceNeedsEmbed());
  CHECK(saved.ok());
  if (!saved.ok()) return;
  ProjectSession t;
  CHECK(t.loadState(saved.value().data(), saved.value().size(), [](const source::SourceInfo&) { return std::shared_ptr<const render::SourceData>(); }).ok());
  CHECK(t.hasSource() && !t.sourceMissing());
  CHECK(t.wasEmbedded());
  CHECK(t.sourceData()->samples == s.sourceData()->samples);
  CHECK(t.chopMap()->serialize() == s.chopMap()->serialize());
  CHECK(t.sourceNeedsEmbed());
}

CHOP_TEST(pathless_source_without_embed_reports_missing) {
  ProjectSession s;
  CHECK(s.loadCapturedSource(clickLoop(), "Capture").ok());
  auto referenceOnly = s.saveState(false);  // what the processor must never do for a capture: there is no file to find
  CHECK(referenceOnly.ok());
  if (!referenceOnly.ok()) return;
  ProjectSession t;
  CHECK(t.loadState(referenceOnly.value().data(), referenceOnly.value().size(), [](const source::SourceInfo&) { return std::shared_ptr<const render::SourceData>(); }).ok());
  CHECK(t.sourceMissing());
}

CHOP_TEST(keeping_a_capture_over_a_project_can_be_undone_with_the_replace_store) {
  ProjectSession s;
  auto first = std::make_shared<render::SourceData>(*clickLoop());
  first->samples[5] += 0.001f;  // a different source id
  CHECK(s.loadSource(first, "file loop", "/tmp/file loop.wav").ok());
  CHECK(s.smartSetup(120.0).ok());
  pattern::Settings ps;
  ps.bars = 2;
  ps.seed = 7;
  CHECK(s.generate(ps).ok());
  const auto before = s.saveState(false).value();

  SourceReplaceUndo undo;
  CHECK(undo.capture(s).ok());
  CHECK(s.loadCapturedSource(clickLoop(), "Capture").ok());
  CHECK(s.pattern() == nullptr);  // the pattern and variations were cleared by the replace
  CHECK(s.sourceNeedsEmbed());
  CHECK(undo.restore(s).ok());
  CHECK(s.saveState(false).value() == before);
  CHECK(!s.sourceNeedsEmbed());
}
