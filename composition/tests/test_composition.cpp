#include <cstdlib>
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <algorithm>
#include <cmath>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {

// Eight drum-like hits over two seconds: low sine "kicks", noise "snares", short "hats".
std::shared_ptr<const render::SourceData> drumLoop() {
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

bool ready(ProjectSession& s) {
  CHECK(s.loadSource(drumLoop(), "loop", "/music/loop.wav").ok());
  DetectOptions d;
  d.analysis.sensitivity = 0.8;  // the quietest synthetic hat sits just under the 0.5 default's threshold
  CHECK(s.applyChops(d).ok());
  return s.hasSource();
}

std::vector<std::uint8_t> barBytes(const pattern::Pattern& p, std::size_t bar) {
  std::vector<std::uint8_t> out;
  bytes::Writer w(out);
  for (const pattern::Beat& bt : p.bars[bar].beats)
    for (const Event& e : bt.events) writeEvent(w, e);
  return out;
}

std::vector<std::vector<float>> audioOf(const ProjectSession& s) {
  auto r = s.renderOffline(render::OfflineSettings{});
  CHECK(r.ok());
  return r.ok() ? r.value() : std::vector<std::vector<float>>(2);
}

bool audible(const std::vector<std::vector<float>>& a) {
  float peak = 0.f;
  for (float x : a[0]) {
    if (!std::isfinite(x)) return false;
    peak = std::max(peak, std::fabs(x));
  }
  return peak > 0.05f;
}

}  // namespace

CHOP_TEST(load_then_hear_input_pass_through_before_generating) {
  render::Renderer r;
  ProjectSession s;
  r.prepare(render::Config{});
  s.attachMailbox(&r.mailbox());
  CHECK(s.loadSource(drumLoop(), "loop").ok());
  CHECK(s.playback() && s.playback()->passThrough);
  std::vector<float> x(512), oL(512), oR(512);
  for (std::size_t i = 0; i < x.size(); ++i) x[i] = 0.3f * std::sin(static_cast<float>(i) * 0.1f);
  const float* in[2] = {x.data(), x.data()};
  float* out[2] = {oL.data(), oR.data()};
  render::TransportBlock tb;
  tb.playing = true;
  tb.positionValid = true;
  r.process(tb, render::RenderParams{}, in, out, 2, 512);
  CHECK(oL == x);  // a loaded source, but nothing generated yet: input is heard unchanged
  ProjectSession empty;
  CHECK(!empty.generate(settings()).ok());   // nothing loaded
  CHECK(!empty.zoomIn(EventId{1}, zoom::Settings{}, 1).ok());
  CHECK(!empty.mutate(pattern::MutateOptions{}).ok());
}

CHOP_TEST(end_to_end_load_chop_generate_mutate_zoom_branch_save_reload_play) {
  render::Renderer r;
  ProjectSession s;
  r.prepare(render::Config{});
  s.attachMailbox(&r.mailbox());
  CHECK(ready(s));

  // Chops: transient detection finds the eight hits.
  CHECK_EQ(s.chops()->chops.size(), 8u);
  for (std::size_t i = 0; i < 8; ++i) CHECK(std::llabs(s.chops()->chops[i].range.start - static_cast<std::int64_t>(i) * 12000) < 400);

  // Roles and grammar are injected as a candidate policy.
  CHECK(s.assignRole(s.chops()->chops[0].id, "kick").ok());
  CHECK(s.assignRole(s.chops()->chops[2].id, "snare").ok());
  roles::RuleSet rules;
  rules.rules = {roles::templates::preserveFirst(1, "kick"), roles::templates::avoidAdjacentRepeats(2)};
  CHECK(s.setRules(rules).ok());

  // Generate.
  CHECK(s.generate(settings(42)).ok());
  CHECK(s.pattern() != nullptr);
  CHECK(s.playback() && !s.playback()->passThrough && !s.playback()->events.empty());
  CHECK_EQ(s.history().size(), 1u);
  const history::NodeId first = s.history().active();
  CHECK(audible(audioOf(s)));
  const auto flat = pattern::flatten(*s.pattern(), *s.chops()).value();
  for (std::size_t i = 1; i < flat.size(); ++i) CHECK(flat[i].chop != flat[i - 1].chop);  // grammar held

  // The renderer receives the same validated snapshot through the mailbox.
  render::TransportBlock tb;
  tb.playing = true;
  tb.positionValid = true;
  std::vector<float> zeros(2048, 0.f), oL(2048), oR(2048);
  const float* in[2] = {zeros.data(), zeros.data()};
  float* out[2] = {oL.data(), oR.data()};
  float peak = 0.f;
  for (int block = 0; block < 40; ++block) {
    tb.ppq = block * 2048 * 120.0 / (60.0 * 48000.0);
    r.process(tb, render::RenderParams{}, in, out, 2, 2048);
    for (float v : oL) peak = std::max(peak, std::fabs(v));
  }
  CHECK(peak > 0.05f);

  // Lock bar 0, mutate: bar 0 is untouched, history branches.
  const auto bar0 = barBytes(*s.pattern(), 0);
  CHECK(s.edit([](const pattern::Pattern& p, const ChopSnapshot&) { return pattern::setLock(p, {ScopeLevel::Bar, 0, 0, {}}, true); }).ok());
  pattern::MutateOptions mo;
  mo.seed = 99;
  mo.amount = 1.0;
  CHECK(s.mutate(mo).ok());
  CHECK(barBytes(*s.pattern(), 0) == bar0);
  CHECK_EQ(s.history().size(), 2u);
  const history::NodeId second = s.history().active();
  CHECK(s.history().getSnapshot(second).value().parent == first);

  // Recursive zoom, twice (depth 2), then the depth limit.
  const Event* hit = nullptr;  // the longest hit of bar 1, so it can be subdivided twice
  for (const auto& bt : s.pattern()->bars[1].beats)
    for (const Event& e : bt.events)
      if (!hit || e.duration > hit->duration) hit = &e;
  CHECK(hit != nullptr);
  if (!hit) return;
  const EventId top = hit->id;
  const std::size_t before = pattern::flatten(*s.pattern(), *s.chops()).value().size();
  zoom::Settings zs;
  zs.subdivisions = 3;
  zs.density = zoom::Density::Dense;
  CHECK(s.zoomIn(top, zs, 5).ok());
  CHECK_EQ(pattern::flatten(*s.pattern(), *s.chops()).value().size(), before + 2);
  const Event* zoomed = pattern::findEvent(*s.pattern(), top);
  CHECK(zoomed && zoomed->child && zoomed->child->events.size() == 3);
  const EventId nested = zoomed->child->events[1].id;
  CHECK(s.zoomIn(nested, zs, 6).ok());                       // second level
  const Event* again = pattern::findEvent(*s.pattern(), top);
  const EventId deepest = again->child->events[1].child->events[0].id;
  auto tooDeep = s.zoomIn(deepest, zs, 7);                    // a third level exceeds the default maximum
  CHECK(!tooDeep.ok() && tooDeep.error().code == ErrorCode::LimitExceeded);
  CHECK(audible(audioOf(s)));
  CHECK(s.mutateChildren(top, zs, 8).ok());
  CHECK(s.collapse(top).ok());
  CHECK_EQ(pattern::flatten(*s.pattern(), *s.chops()).value().size(), before);

  // Quantized switch back to the first variation: deferred until the loop boundary.
  const auto beforeSwitch = pattern::serialize(*s.pattern());
  CHECK(s.activate(first, Quantize::LoopBoundary).ok());
  CHECK(s.hasPendingActivation() && pattern::serialize(*s.pattern()) == beforeSwitch);
  s.onLoopBoundary();
  CHECK(!s.hasPendingActivation() && s.history().active() == first);
  CHECK_EQ(s.pattern()->settings.seed, 42u);
  auto cmp = s.compareNodes(first, second);
  CHECK(cmp.ok() && cmp.value().find("events") != std::string::npos);

  // Save / reload (audio embedded): identical audio, same history.
  CHECK(s.activate(second, Quantize::Immediate).ok());
  const auto audio = audioOf(s);
  auto saved = s.saveState(true);
  CHECK(saved.ok());
  if (!saved.ok()) return;
  ProjectSession reopened;
  CHECK(reopened.loadState(saved.value().data(), saved.value().size(), nullptr).ok());
  CHECK(!reopened.sourceMissing());
  CHECK(audioOf(reopened) == audio);
  CHECK_EQ(reopened.history().size(), s.history().size());
  CHECK_EQ(reopened.history().active(), s.history().active());
  CHECK(pattern::serialize(*reopened.pattern()) == pattern::serialize(*s.pattern()));
  CHECK(reopened.roleMap().roleOf(s.chops()->chops[0].id) == "kick" && reopened.rules().rules.size() == 2);
  CHECK(reopened.chopMap()->markers().size() == s.chopMap()->markers().size());
}

CHOP_TEST(same_inputs_give_identical_audio_in_independent_sessions) {
  ProjectSession a, b;
  CHECK(ready(a) && ready(b));
  CHECK(a.generate(settings(7)).ok() && b.generate(settings(7)).ok());
  CHECK(audioOf(a) == audioOf(b));
  CHECK(b.generate(settings(8)).ok());
  CHECK(audioOf(a) != audioOf(b));
}

CHOP_TEST(undo_redo_and_ab_snapshots_restore_exact_patterns_and_playback) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK(s.generate(settings(1)).ok());
  const auto first = pattern::serialize(*s.pattern());
  const auto firstAudio = audioOf(s);
  CHECK(s.storeSnapshot(0).ok());
  CHECK(s.generate(settings(2)).ok());
  CHECK(s.storeSnapshot(1).ok());
  CHECK(s.undo());
  CHECK(pattern::serialize(*s.pattern()) == first && audioOf(s) == firstAudio);
  CHECK(s.redo() && s.pattern()->settings.seed == 2);
  CHECK(s.recallSnapshot(0).ok() && pattern::serialize(*s.pattern()) == first);
  CHECK(s.recallSnapshot(1).ok() && s.pattern()->settings.seed == 2);
  CHECK(!s.recallSnapshot(5).ok());
  while (s.undo()) {}
  CHECK(s.pattern() == nullptr && s.playback()->passThrough);  // back before the first Generate
}

CHOP_TEST(marker_edits_reconcile_the_pattern_and_stay_valid) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK(s.generate(settings(5, 4)).ok());
  const ChopId victim = s.chops()->chops[3].id;
  CHECK(s.editMarkers([&](source::ChopMap& m) { return m.deleteMarker(victim); }).ok());
  CHECK(s.chops()->find(victim) == nullptr);
  CHECK(pattern::flatten(*s.pattern(), *s.chops()).ok());  // no event refers to the deleted chop
  for (const auto& b : s.pattern()->bars)
    for (const auto& bt : b.beats)
      for (const Event& e : bt.events) CHECK(e.chop != victim);
  CHECK(audible(audioOf(s)));
  CHECK(s.undoMarkers().ok());  // marker undo restores the chop; the pattern stays playable
  CHECK(s.chops()->find(victim) != nullptr);
  CHECK(pattern::flatten(*s.pattern(), *s.chops()).ok());
  CHECK(!s.editMarkers([](source::ChopMap& m) { return m.moveMarker(ChopId{9999}, 5); }).ok());
  CHECK(!s.assignRole(ChopId{9999}, "kick").ok());
  CHECK(!s.assignRole(victim, "NOT valid").ok());
  roles::RuleSet bad;
  bad.rules.push_back(roles::Rule{});  // id 0 is invalid
  CHECK(!s.setRules(bad).ok());
}

CHOP_TEST(history_cap_degrades_gracefully_with_a_visible_notice) {
  ProjectSession s;
  CHECK(ready(s));
  for (std::uint64_t i = 1; i <= 70; ++i) CHECK(s.generate(settings(i, 1)).ok());  // every generate still succeeds
  CHECK(s.history().size() <= 64u);
  bool warned = false;
  for (const Notice& n : s.notices()) warned = warned || n.text.find("family tree") != std::string::npos;
  CHECK(warned);
  CHECK(s.pattern()->settings.seed == 70u);
}

CHOP_TEST(missing_source_keeps_markers_and_pattern_and_relinking_restores_audio) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK(s.generate(settings(11)).ok());
  const auto audio = audioOf(s);
  auto saved = s.saveState(false);  // reference only
  CHECK(saved.ok());
  if (!saved.ok()) return;
  CHECK(saved.value().size() < 40000u);  // no audio in the project

  ProjectSession lost;
  CHECK(lost.loadState(saved.value().data(), saved.value().size(), [](const source::SourceInfo&) { return std::shared_ptr<const render::SourceData>(); }).ok());
  CHECK(lost.sourceMissing() && lost.pattern() != nullptr);
  CHECK_EQ(lost.chopMap()->markers().size(), s.chopMap()->markers().size());
  CHECK(!lost.playback()->source);                            // renderer passes input through
  CHECK(!lost.renderOffline(render::OfflineSettings{}).ok());
  CHECK(!lost.takeNotices().empty());
  CHECK(lost.mutate(pattern::MutateOptions{}).ok());           // pattern authoring continues without audio
  CHECK(lost.undo());

  // The wrong audio is refused; the right audio restores identical playback.
  auto other = std::make_shared<render::SourceData>(*drumLoop());
  other->samples[100] = 0.9f;
  CHECK(!lost.relinkSource(other).ok());
  CHECK(lost.relinkSource(drumLoop()).ok());
  CHECK(!lost.sourceMissing() && audioOf(lost) == audio);

  ProjectSession wrongFile;
  CHECK(wrongFile.loadState(saved.value().data(), saved.value().size(), [&](const source::SourceInfo&) { return std::shared_ptr<const render::SourceData>(other); }).ok());
  CHECK(wrongFile.sourceMissing());  // a different file at the same path is not trusted
  ProjectSession found;
  CHECK(found.loadState(saved.value().data(), saved.value().size(), [](const source::SourceInfo&) { return drumLoop(); }).ok());
  CHECK(!found.sourceMissing() && audioOf(found) == audio);
}

CHOP_TEST(embedding_is_capped_and_reports_its_size) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK_EQ(s.embeddedSourceBytes(), 16u + 96000u * 4u);
  CHECK(s.saveState(true).ok());
  auto big = std::make_shared<render::SourceData>();
  big->channels = 2;
  big->sampleRate = 96000;  // 60 s stereo at 96 kHz is 46 MB, over a 32 MiB cap set for the test
  big->frames = 96000 * 60;
  big->samples.assign(static_cast<std::size_t>(big->frames) * 2, 0.f);
  ProjectSession large;
  CHECK(large.loadSource(big, "long").ok());
  CHECK(large.saveState(true).ok());  // under the generous 128 MiB default
  CHECK_EQ(large.embeddedSourceCap(), kMaxEmbeddedSourceBytes);
  CHECK(!large.setEmbeddedSourceCap(0).ok() && !large.setEmbeddedSourceCap(kCodecCeilingBytes).ok());
  CHECK(large.setEmbeddedSourceCap(32u * 1024u * 1024u).ok());
  auto tooBig = large.saveState(true);
  CHECK(!tooBig.ok() && tooBig.error().code == ErrorCode::LimitExceeded);
  CHECK(large.saveState(false).ok());  // referencing the file is always allowed
}

CHOP_TEST(hostile_project_files_are_rejected_without_touching_the_live_session) {
  ProjectSession s;
  CHECK(ready(s));
  CHECK(s.generate(settings(3)).ok());
  auto saved = s.saveState(true);
  CHECK(saved.ok());
  if (!saved.ok()) return;
  const auto reference = s.saveState(false).value();

  // Raw corruption: the checksum catches every single-byte change.
  const auto& blob = saved.value();
  for (std::size_t i = 0; i < blob.size(); i += 997) {
    auto bad = blob;
    bad[i] ^= 0x40;
    CHECK(!s.loadState(bad.data(), bad.size(), nullptr).ok());
  }
  for (std::size_t cut : {std::size_t{0}, std::size_t{5}, blob.size() / 2, blob.size() - 1}) CHECK(!s.loadState(blob.data(), cut, nullptr).ok());
  CHECK(s.saveState(false).value() == reference);  // untouched after every failure

  // Structural corruption that passes the checksum: tamper with module payloads, then re-encode.
  auto decoded = codec::decode(blob.data(), blob.size());
  CHECK(decoded.ok());
  if (!decoded.ok()) return;
  int rejected = 0, accepted = 0;
  for (const char* module : {"source_chop", "pattern_engine", "variation_history", "chop_roles_grammar", "source_audio"}) {
    const std::size_t n = decoded.value().modules.at(module).bytes.size();
    const std::size_t stride = std::max<std::size_t>(1, n / 120);
    for (std::size_t i = 0; i < n; i += stride) {
      codec::ProjectState tampered = decoded.value();
      tampered.modules[module].bytes[i] ^= 0x5A;
      auto enc = codec::encode(tampered);
      CHECK(enc.ok());
      if (!enc.ok()) continue;
      ProjectSession probe;
      Status st = probe.loadState(enc.value().data(), enc.value().size(), nullptr);
      if (st.ok()) {
        ++accepted;  // a flip that survives must still leave a consistent, playable session
        if (probe.pattern() && probe.chops() && !probe.sourceMissing()) CHECK(pattern::flatten(*probe.pattern(), *probe.chops()).ok());
      } else {
        ++rejected;
      }
    }
  }
  CHECK(rejected > 0);
  (void)accepted;

  // A project written by a newer build is refused cleanly.
  codec::ProjectState newer = decoded.value();
  newer.modules["pattern_engine"].schemaVersion = pattern::kSchemaVersion + 1;
  auto enc = codec::encode(newer);
  CHECK(enc.ok());
  if (enc.ok()) {
    ProjectSession probe;
    Status st = probe.loadState(enc.value().data(), enc.value().size(), nullptr);
    CHECK(!st.ok() && st.error().code == ErrorCode::UnsupportedVersion);
  }
  // A pattern without its source description is inconsistent.
  codec::ProjectState noSource = decoded.value();
  noSource.modules.erase("source_chop");
  noSource.modules.erase("source_audio");
  auto enc2 = codec::encode(noSource);
  CHECK(enc2.ok() && !ProjectSession{}.loadState(enc2.value().data(), enc2.value().size(), nullptr).ok());
}

CHOP_TEST(audition_uses_the_chop_range_and_parameters_bind_to_settings) {
  ProjectSession s;
  CHECK(ready(s));
  auto req = s.auditionRequest(s.chops()->chops[2].id);
  CHECK(req.ok() && req.value().region == s.chops()->chops[2].range);
  CHECK(!s.auditionRequest(ChopId{12345}).ok());

  host::ParamValues v = host::ParamValues::defaults();
  v.set(host::kDensity, 0.9);
  v.set(host::kSwing, 0.5);
  v.set(host::kPatternBars, 3);  // choice index 3 = 8 bars
  v.set(host::kAllowReverse, 1);
  pattern::Settings base;
  base.seed = 77;
  base.variation.event = 0.25;
  const pattern::Settings bound = settingsFromParams(v, base);
  CHECK(bound.density == 0.9 && bound.swing == 0.5 && bound.bars == 8 && bound.allowReverse && !bound.allowPitch);
  CHECK(bound.seed == 77u && bound.variation.event == 0.25);  // non-automatable fields come from the caller
  CHECK(mutateAmountFromParams(v) == 0.5);

  // Live density feeds Mutate, but a different length needs Generate.
  CHECK(s.generate(settings(4, 2)).ok());
  pattern::Settings live = s.pattern()->settings;
  live.density = 0.2;
  CHECK(s.mutate(pattern::MutateOptions{5, 1.0, false}, &live).ok());
  CHECK(s.pattern()->settings.density == 0.2);
  live.bars = 4;
  auto refused = s.mutate(pattern::MutateOptions{6, 1.0, false}, &live);
  CHECK(!refused.ok() && refused.error().code == ErrorCode::Conflict);
}
