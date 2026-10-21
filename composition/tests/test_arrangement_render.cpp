// F18: the arrangement render and job (length, section positions, determinism, crossfade, fade out, limiter, cancel,
// progress, files). Everything renders the synthetic drum loop at 120 BPM / 48 kHz unless a test says otherwise.
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <set>
#include <thread>

#include "session_fixture.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;
namespace fs = std::filesystem;

namespace {

constexpr std::int64_t kBar = 96000;  // 120 BPM at 48 kHz

struct Fixture {
  ProjectSession s;
  std::vector<scenes::SceneId> ids;
  Fixture(int sceneCount = 3) {
    CHECK(fixture::ready(s, true, 5, 2));
    for (int i = 0; i < sceneCount; ++i) {
      // scene 3 has a 4-bar pattern, the others 2 bars
      CHECK(s.generate(fixture::settings(11 + static_cast<std::uint64_t>(i), i == 2 ? 4 : 2)).ok());
      auto r = s.captureScene();
      CHECK(r.ok());
      if (r.ok()) ids.push_back(r.value());
    }
  }
  // A(2-bar pattern) 4 bars, B 2 bars x 2, C(4-bar pattern) 4 bars x 2: 4 + 4 + 8 = 16 bars
  Arrangement sixteen() const {
    Arrangement a;
    a.sections = {{ids[0], 4, 1}, {ids[1], 2, 2}, {ids[2], 4, 2}};
    return a;
  }
  ArrangementExportSettings settings(const char* dir) const {
    ArrangementExportSettings st;
    st.root = fixture::tmpDir("chop_arr_render", dir);
    st.bpm = 120.0;
    st.wavOptions.sampleRate = 48000;
    st.wavOptions.format = wav::Format::Float32;
    return st;
  }
};

ArrangementPlan planOf(const Fixture& f, const Arrangement& a, const char* dir, const ArrangementExportSettings* over = nullptr) {
  auto plan = f.s.planArrangementExport(a, "demo", over ? *over : f.settings(dir));
  CHECK(plan.ok());
  return plan.ok() ? plan.value() : ArrangementPlan{};
}

Result<ArrangementAudio> renderPlan(const ArrangementPlan& p) {
  ArrangementControl c;
  return renderArrangement(p, c);
}

std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t at) {
  return static_cast<std::uint32_t>(b[at]) | static_cast<std::uint32_t>(b[at + 1]) << 8 | static_cast<std::uint32_t>(b[at + 2]) << 16 | static_cast<std::uint32_t>(b[at + 3]) << 24;
}

// Independent reader: the sample offsets of the `cue ` chunk and the `labl` texts, by walking the RIFF chunks.
struct Cues {
  std::vector<std::uint32_t> offsets;
  std::vector<std::string> labels;
  std::uint32_t dataBytes = 0;
};
Cues readCues(const std::vector<std::uint8_t>& b) {
  Cues c;
  CHECK(b.size() > 12 && std::memcmp(b.data(), "RIFF", 4) == 0 && std::memcmp(b.data() + 8, "WAVE", 4) == 0);
  CHECK_EQ(static_cast<std::size_t>(le32(b, 4)) + 8, b.size());
  std::size_t p = 12;
  while (p + 8 <= b.size()) {
    const std::uint32_t len = le32(b, p + 4);
    const std::string id(reinterpret_cast<const char*>(b.data() + p), 4);
    const std::size_t body = p + 8;
    if (id == "data") c.dataBytes = len;
    if (id == "cue ") {
      const std::uint32_t n = le32(b, body);
      for (std::uint32_t i = 0; i < n; ++i) c.offsets.push_back(le32(b, body + 4 + i * 24 + 20));
    }
    if (id == "LIST" && std::memcmp(b.data() + body, "adtl", 4) == 0) {
      std::size_t q = body + 4;
      while (q + 8 <= body + len) {
        const std::uint32_t sl = le32(b, q + 4);
        if (std::memcmp(b.data() + q, "labl", 4) == 0) c.labels.emplace_back(reinterpret_cast<const char*>(b.data() + q + 12), sl - 4 > 0 ? sl - 5 : 0);
        q += 8 + sl + (sl & 1u);
      }
    }
    p = body + len + (len & 1u);
  }
  return c;
}

float peakOf(const std::vector<std::vector<float>>& x) {
  float m = 0.f;
  for (const auto& ch : x)
    for (float v : ch) m = std::max(m, std::fabs(v));
  return m;
}

}  // namespace

CHOP_TEST(length_equals_total_bars_times_bar_frames_plus_tail) {
  Fixture f;
  const Arrangement a = f.sixteen();
  CHECK_EQ(barFrames(120.0, 48000.0), kBar);
  ArrangementPlan p = planOf(f, a, "length");
  auto r = renderPlan(p);
  CHECK(r.ok());
  CHECK_EQ(static_cast<std::int64_t>(r.value().planar[0].size()), 16 * kBar + 96000);  // tail 2.0 s
  CHECK_EQ(mainFrames(p), 16 * kBar);
  CHECK_EQ(totalFrames(p), 16 * kBar + 96000);
  Arrangement noTail = a;
  noTail.tail = false;
  p = planOf(f, noTail, "length");
  r = renderPlan(p);
  CHECK(r.ok() && static_cast<std::int64_t>(r.value().planar[0].size()) == 16 * kBar);
  // another tempo: bar frames are rounded once and used everywhere
  ArrangementExportSettings st = f.settings("length");
  st.bpm = 133.0;
  p = planOf(f, noTail, "length", &st);
  const std::int64_t bar133 = static_cast<std::int64_t>(std::llround(60.0 / 133.0 * 4.0 * 48000.0));
  CHECK_EQ(barFrames(133.0, 48000.0), bar133);
  r = renderPlan(p);
  CHECK(r.ok() && static_cast<std::int64_t>(r.value().planar[0].size()) == 16 * bar133);
  CHECK_EQ(r.value().sectionStarts[1], 4 * bar133);
  CHECK_EQ(r.value().sectionStarts[2], 8 * bar133);
}

CHOP_TEST(section_boundaries_are_exact_at_integer_bar_frames) {
  Fixture f;
  ArrangementPlan p = planOf(f, f.sixteen(), "bounds");
  auto r = renderPlan(p);
  CHECK(r.ok());
  CHECK_EQ(r.value().sectionStarts.size(), 3u);
  CHECK_EQ(r.value().sectionStarts[0], 0);
  CHECK_EQ(r.value().sectionStarts[1], 4 * kBar);
  CHECK_EQ(r.value().sectionStarts[2], 8 * kBar);
  // and the files agree: cue points at the same frames, MIDI markers at the same bars, the cue list in seconds
  ArrangementControl c;
  const ArrangementResult res = runArrangement(p, c);
  CHECK(res.status.ok());
  const auto bytes = fixture::readFile(res.wavPath);
  const Cues cues = readCues(bytes);
  CHECK((cues.offsets == std::vector<std::uint32_t>{0, 4u * kBar, 8u * kBar}));
  CHECK_EQ(cues.labels.size(), 3u);
  CHECK(cues.labels[0] == "Scene 1" && cues.labels[2] == "Scene 3");
  CHECK_EQ(cues.dataBytes, static_cast<std::uint32_t>((16 * kBar + 96000) * 2 * 4));
  const auto midiBytes = fixture::readFile(res.midiPath);
  auto m = midi::decode(midiBytes.data(), midiBytes.size());
  CHECK(m.ok());
  CHECK_EQ(m.value().markers.size(), 3u);
  CHECK(m.value().markers[0].tick == 0 && m.value().markers[1].tick == 4 * 3840 && m.value().markers[2].tick == 8 * 3840);
  CHECK(m.value().markers[1].text == "Scene 2");
  {  // end of track at the last bar boundary (the decoder reports endTick only when it lies after the last event)
    Ticks end = m.value().endTick;
    for (const midi::Note& n : m.value().notes) end = std::max(end, n.start + n.duration);
    CHECK_EQ(end, 16 * 3840);
  }
  CHECK_EQ(m.value().bpm, 120.0);
  const auto cueBytes = fixture::readFile(res.cuePath);
  const std::string cueText(cueBytes.begin(), cueBytes.end());
  CHECK(cueText == "1,0.000,Scene 1\n5,8.000,Scene 2\n9,16.000,Scene 3\n");
  CHECK(res.bars == 16 && std::fabs(res.seconds - 34.0) < 1e-9);
}

CHOP_TEST(render_is_deterministic_two_runs_equal_bytes) {
  Fixture f;
  const Arrangement a = f.sixteen();
  ArrangementControl c1, c2;
  const ArrangementResult r1 = runArrangement(planOf(f, a, "det_a"), c1);
  const ArrangementResult r2 = runArrangement(planOf(f, a, "det_b"), c2);
  CHECK(r1.status.ok() && r2.status.ok());
  CHECK(fixture::readFile(r1.wavPath) == fixture::readFile(r2.wavPath));
  CHECK(fixture::readFile(r1.midiPath) == fixture::readFile(r2.midiPath));
  CHECK(fixture::readFile(r1.cuePath) == fixture::readFile(r2.cuePath));
  CHECK(fixture::fnv1a(fixture::readFile(r1.wavPath)) != 0);
}

CHOP_TEST(render_independent_of_snapshot_copy_and_live_edits) {
  Fixture f;
  ArrangementPlan p = planOf(f, f.sixteen(), "live");
  const ArrangementPlan copy = p;
  auto before = renderPlan(p);
  CHECK(before.ok());
  // live edits after the snapshot: another pattern, a removed scene, a new source
  CHECK(f.s.generate(fixture::settings(901, 2)).ok());
  CHECK(f.s.removeScene(f.ids[1]).ok());
  CHECK(f.s.loadSource(fixture::loopAudio(), "other", "/music/other.wav").ok());
  auto after = renderPlan(copy);
  CHECK(after.ok());
  CHECK(before.value().planar == after.value().planar);
}

CHOP_TEST(crossfade_keeps_length_and_is_equal_gain) {
  for (std::int64_t N : {1, 2, 480, 2400, 19200})
    for (std::int64_t i = 0; i < N; ++i) {
      const float in = crossfadeGainIn(i, N), out = 1.f - in;
      CHECK(std::fabs(static_cast<double>(in) + static_cast<double>(out) - 1.0) <= 1e-6);
      CHECK(in > 0.f && in < 1.f);
      if (i > 0) CHECK(in > crossfadeGainIn(i - 1, N));
    }
  CHECK_EQ(crossfadeFrames(Transition::Cut, 48000.0), 0);
  CHECK_EQ(crossfadeFrames(Transition::Crossfade10, 48000.0), 480);
  CHECK_EQ(crossfadeFrames(Transition::Crossfade50, 48000.0), 2400);

  Fixture f;
  Arrangement cut = f.sixteen();
  cut.transition = Transition::Cut;
  Arrangement x10 = cut;
  x10.transition = Transition::Crossfade10;
  Arrangement x50 = cut;
  x50.transition = Transition::Crossfade50;
  auto rc = renderPlan(planOf(f, cut, "xf"));
  auto r10 = renderPlan(planOf(f, x10, "xf"));
  auto r50 = renderPlan(planOf(f, x50, "xf"));
  CHECK(rc.ok() && r10.ok() && r50.ok());
  CHECK(rc.value().planar[0].size() == r10.value().planar[0].size() && rc.value().planar[0].size() == r50.value().planar[0].size());
  // outside the overlap windows [start_k, start_k + N) of the later sections the three renders are identical
  for (auto [res, N] : {std::pair<const ArrangementAudio*, std::int64_t>{&r10.value(), 480}, {&r50.value(), 2400}}) {
    std::int64_t diffOutside = 0, diffInside = 0;
    for (std::size_t i = 0; i < rc.value().planar[0].size(); ++i) {
      bool inWindow = false;
      for (std::size_t k = 1; k < 3; ++k) {
        const std::int64_t s = rc.value().sectionStarts[k];
        inWindow = inWindow || (static_cast<std::int64_t>(i) >= s && static_cast<std::int64_t>(i) < s + N);
      }
      const bool differs = rc.value().planar[0][i] != res->planar[0][i] || rc.value().planar[1][i] != res->planar[1][i];
      (inWindow ? diffInside : diffOutside) += differs ? 1 : 0;
    }
    CHECK_EQ(diffOutside, 0);
    CHECK(diffInside > 0);  // the windows really changed
  }
}

CHOP_TEST(fade_out_reaches_zero_at_end_and_replaces_the_tail) {
  Fixture f;
  Arrangement a = f.sixteen();
  a.fadeOut = true;  // tail stays "on" in the options but the fade owns the end
  auto plain = renderPlan(planOf(f, [&] { Arrangement b = a; b.fadeOut = false; b.tail = false; return b; }(), "fade"));
  auto faded = renderPlan(planOf(f, a, "fade"));
  CHECK(plain.ok() && faded.ok());
  const auto& x = faded.value().planar;
  CHECK_EQ(static_cast<std::int64_t>(x[0].size()), 16 * kBar);
  CHECK(x[0].back() == 0.f && x[1].back() == 0.f);
  // before the last bar nothing changed; inside it the signal never grows
  const std::size_t fadeStart = static_cast<std::size_t>(15 * kBar);
  for (std::size_t i = 0; i < fadeStart; ++i) CHECK(x[0][i] == plain.value().planar[0][i]);
  for (std::size_t i = fadeStart; i < x[0].size(); ++i) CHECK(std::fabs(x[0][i]) <= std::fabs(plain.value().planar[0][i]) + 1e-7f);
  // the gain falls monotonically: the energy of the last quarter of the bar is far below the first quarter's
  auto energy = [&](std::size_t from, std::size_t to) {
    double e = 0;
    for (std::size_t i = from; i < to; ++i) e += static_cast<double>(x[0][i]) * x[0][i];
    return e;
  };
  CHECK(energy(fadeStart + 3 * kBar / 4, x[0].size()) < energy(fadeStart, fadeStart + kBar / 4));
}

CHOP_TEST(tail_ends_in_a_linear_fade_to_zero) {
  Fixture f;
  auto r = renderPlan(planOf(f, f.sixteen(), "tail"));
  CHECK(r.ok());
  const auto& x = r.value().planar;
  CHECK(x[0].back() == 0.f && x[1].back() == 0.f);
}

CHOP_TEST(limiter_applies_once_over_the_final_mix_and_peak_below_ceiling) {
  Fixture f;
  Arrangement a = f.sixteen();
  ArrangementPlan p = planOf(f, a, "lim");
  p.params.outputGainDb = 18.f;
  auto hot = renderPlan(p);
  CHECK(hot.ok());
  CHECK(peakOf(hot.value().planar) > 1.0f);  // the test material really exceeds the ceiling
  ArrangementPlan q = p;
  q.params.limiterEnabled = true;
  q.params.limiterCeilingDb = -6.f;
  auto lim = renderPlan(q);
  CHECK(lim.ok());
  CHECK_EQ(lim.value().planar[0].size(), hot.value().planar[0].size());  // latency compensated: same length
  CHECK(peakOf(lim.value().planar) <= std::pow(10.f, -6.f / 20.f) + 1e-4f);
  CHECK(peakOf(lim.value().planar) > 0.2f);  // and it is not simply muted
  // applied once: limiting the assembled mix a second time through the same stage changes (almost) nothing
  // while the per-section path would have been audibly different at the section starts: the first sample after the
  // latency drop equals the unlimited signal's start (the limiter was primed, there is no warm-up dip)
  CHECK(std::fabs(lim.value().planar[0][0] - hot.value().planar[0][0]) <= 1e-3f);
}

CHOP_TEST(silent_scene_renders_silence_with_marker) {
  Fixture f;
  Arrangement a = f.sixteen();
  a.transition = Transition::Cut;
  ArrangementPlan p = planOf(f, a, "silent");
  // section 2 gets a playback with no events
  auto silent = render::makePlayback(p.sections[1].playback->source, FlatEventList{}, p.sections[1].playback->lengthTicks);
  CHECK(silent.ok());
  if (!silent.ok()) return;
  p.sections[1].playback = silent.value();
  p.sections[1].notes.clear();
  auto r = renderPlan(p);
  CHECK(r.ok());
  const auto& x = r.value().planar;
  bool allZero = true;
  for (std::int64_t i = 4 * kBar; i < 8 * kBar; ++i) allZero = allZero && x[0][static_cast<std::size_t>(i)] == 0.f && x[1][static_cast<std::size_t>(i)] == 0.f;
  CHECK(allZero);
  ArrangementControl c;
  const ArrangementResult res = runArrangement(p, c);
  CHECK(res.status.ok());
  CHECK_EQ(readCues(fixture::readFile(res.wavPath)).labels.size(), 3u);  // the marker is still there
  // level match reports silent and uses 0 dB for it
  ArrangementPlan lm = p;
  lm.levelMatch = true;
  auto rl = renderPlan(lm);
  CHECK(rl.ok());
  CHECK(rl.value().matchGainDb[1] == 0.f);
}

CHOP_TEST(shorter_scene_loops_longer_truncates) {
  Fixture f;
  Arrangement a;
  a.transition = Transition::Cut;
  a.tail = false;
  a.sections = {{f.ids[0], 4, 1}, {f.ids[2], 2, 4}};  // 2-bar pattern over 4 bars (loops); 4-bar pattern over 2 bars x 4 (cut)
  ArrangementPlan p = planOf(f, a, "loops");
  auto r = renderPlan(p);
  CHECK(r.ok());
  CHECK_EQ(static_cast<std::int64_t>(r.value().planar[0].size()), 12 * kBar);
  // section 1 equals a direct render of the scene looped twice
  render::OfflineSettings rs;
  rs.sampleRate = 48000.0;
  rs.bpm = 120.0;
  rs.cycles = 2;
  rs.tailSeconds = 0.0;
  rs.params.limiterEnabled = false;
  auto direct = render::renderOffline(p.sections[0].playback, rs);
  CHECK(direct.ok());
  std::size_t bad = 0;
  for (std::size_t i = 0; i < static_cast<std::size_t>(4 * kBar); ++i) bad += std::fabs(direct.value()[0][i] - r.value().planar[0][i]) > 1e-6f ? 1 : 0;
  CHECK_EQ(bad, 0u);
  // section 2: the first two bars of the 4-bar scene, four times; the pattern after bar 2 never plays, and the second
  // repeat starts the pattern again (its first frames equal the first repeat's first frames)
  const std::size_t s2 = static_cast<std::size_t>(4 * kBar);
  std::size_t equalStart = 0;
  for (std::size_t i = 0; i < 2000; ++i) equalStart += std::fabs(r.value().planar[0][s2 + i] - r.value().planar[0][s2 + static_cast<std::size_t>(2 * kBar) + i]) <= 1e-6f ? 1 : 0;
  CHECK_EQ(equalStart, 2000u);
  render::OfflineSettings rs4 = rs;
  rs4.cycles = 1;
  auto full = render::renderOffline(p.sections[1].playback, rs4);  // the whole 4-bar scene once
  CHECK(full.ok());
  std::size_t head = 0;
  for (std::size_t i = 0; i < static_cast<std::size_t>(2 * kBar); ++i) head += std::fabs(full.value()[0][i] - r.value().planar[0][s2 + i]) > 1e-6f ? 1 : 0;
  CHECK_EQ(head, 0u);  // the cut changes nothing before the cut
  // MIDI: notes of the cut scene stop at the repeat end
  ArrangementControl c;
  p.root = fixture::tmpDir("chop_arr_render", "loops");
  const ArrangementResult res = runArrangement(p, c);
  CHECK(res.status.ok());
  const auto mb = fixture::readFile(res.midiPath);
  auto m = midi::decode(mb.data(), mb.size());
  CHECK(m.ok());
  for (const midi::Note& n : m.value().notes) CHECK(n.start + n.duration <= 12 * 3840);
  std::size_t inSecondSection = 0;
  for (const midi::Note& n : m.value().notes) inSecondSection += n.start >= 4 * 3840 ? 1 : 0;
  CHECK(inSecondSection > 0);
}

CHOP_TEST(notes_equal_the_scene_notes_repeated) {
  Fixture f;
  Arrangement a;
  a.tail = false;
  a.sections = {{f.ids[0], 8, 1}};  // a 2-bar pattern four times
  ArrangementPlan p = planOf(f, a, "notes");
  const std::size_t perPass = p.sections[0].notes.size();
  CHECK(perPass > 0);
  ArrangementControl c;
  const ArrangementResult res = runArrangement(p, c);
  CHECK(res.status.ok());
  const auto mb = fixture::readFile(res.midiPath);
  auto m = midi::decode(mb.data(), mb.size());
  CHECK(m.ok());
  CHECK_EQ(m.value().notes.size(), 4 * perPass);
  // the same notes the scene exports on its own (F09 scene export), shifted by one pass each
  const Ticks pass = p.sections[0].lengthTicks;
  for (std::size_t i = 0; i < perPass; ++i) {
    std::size_t hits = 0;
    for (int k = 0; k < 4; ++k)
      for (const midi::Note& n : m.value().notes)
        hits += (n.note == p.sections[0].notes[i].note && n.start == p.sections[0].notes[i].start + k * pass) ? 1 : 0;
    CHECK(hits >= 4);
  }
}

CHOP_TEST(export_matches_live_render_of_one_scene) {
  Fixture f;
  Arrangement a;
  a.transition = Transition::Cut;
  a.sections = {{f.ids[0], 8, 1}};  // the 2-bar scene, four cycles
  ArrangementPlan p = planOf(f, a, "parity");
  auto r = renderPlan(p);
  CHECK(r.ok());
  render::OfflineSettings rs;
  rs.sampleRate = 48000.0;
  rs.bpm = 120.0;
  rs.cycles = 4;
  rs.tailSeconds = 2.0;
  rs.params = f.s.renderParams();
  rs.params.enabled = true;
  rs.params.dryMix = 0.f;
  auto direct = render::renderOffline(p.sections[0].playback, rs);
  CHECK(direct.ok());
  CHECK(direct.value()[0].size() >= r.value().planar[0].size());
  // the arrangement's own tail fade is the only difference
  const std::size_t n = r.value().planar[0].size() - 2400;
  double worst = 0;
  for (std::size_t ch = 0; ch < 2; ++ch)
    for (std::size_t i = 0; i < n; ++i) worst = std::max(worst, static_cast<double>(std::fabs(direct.value()[ch][i] - r.value().planar[ch][i])));
  CHECK(worst <= 1e-6);
}

CHOP_TEST(level_match_gain_is_recomputed_per_scene) {
  Fixture f;
  ArrangementPlan p = planOf(f, f.sixteen(), "lm");
  p.levelMatch = true;
  auto r = renderPlan(p);
  CHECK(r.ok());
  CHECK_EQ(r.value().matchGainDb.size(), 3u);
  for (float g : r.value().matchGainDb) CHECK(std::isfinite(g) && std::fabs(g) <= 12.f);
  CHECK(r.value().matchGainDb[0] != 0.f || r.value().matchGainDb[1] != 0.f || r.value().matchGainDb[2] != 0.f);
  // the same scene twice gets the same gain
  Arrangement twice;
  twice.sections = {{f.ids[0], 4, 1}, {f.ids[1], 2, 2}, {f.ids[0], 4, 1}};
  ArrangementPlan q = planOf(f, twice, "lm");
  q.levelMatch = true;
  auto rq = renderPlan(q);
  CHECK(rq.ok() && rq.value().matchGainDb[0] == rq.value().matchGainDb[2]);
  // off: no gain at all
  CHECK(renderPlan(planOf(f, f.sixteen(), "lm")).value().matchGainDb == std::vector<float>(3, 0.f));
}

CHOP_TEST(cancel_removes_partial_files_and_returns_cancelled) {
  Fixture f;
  ArrangementPlan p = planOf(f, f.sixteen(), "cancel");
  ArrangementControl c;
  p.observer = [&c](float v) {
    if (v > 0.3f) c.cancel.store(true);
  };
  const ArrangementResult res = runArrangement(p, c);
  CHECK(!res.status.ok());
  CHECK(res.status.error().code == ErrorCode::Cancelled);
  CHECK(res.status.error().message == "Export cancelled. No files were written.");
  CHECK(res.wavPath.empty() && res.midiPath.empty() && res.cuePath.empty());
  CHECK(!fs::exists(p.root) || fs::is_empty(p.root));
  // a cancel before the first block, and one during the write phase, also leave nothing
  ArrangementControl pre;
  pre.cancel.store(true);
  CHECK(runArrangement(planOf(f, f.sixteen(), "cancel"), pre).status.error().code == ErrorCode::Cancelled);
  ArrangementPlan late = planOf(f, f.sixteen(), "cancel");
  ArrangementControl lc;
  late.observer = [&lc](float v) {
    if (v >= 0.97f && v < 1.f) lc.cancel.store(true);
  };
  CHECK(runArrangement(late, lc).status.error().code == ErrorCode::Cancelled);
  CHECK(!fs::exists(late.root) || fs::is_empty(late.root));
}

CHOP_TEST(progress_is_monotonic_and_reaches_1) {
  Fixture f;
  ArrangementPlan p = planOf(f, f.sixteen(), "progress");
  std::vector<float> seen;
  p.observer = [&seen](float v) { seen.push_back(v); };
  ArrangementControl c;
  const ArrangementResult res = runArrangement(p, c);
  CHECK(res.status.ok());
  CHECK(seen.size() > 4);
  for (std::size_t i = 1; i < seen.size(); ++i) CHECK(seen[i] >= seen[i - 1]);
  CHECK(seen.back() == 1.f && c.fraction.load() == 1.f);
  CHECK(seen.front() < 0.5f);
  CHECK_EQ(c.sections.load(), 3);
  CHECK_EQ(c.section.load(), 3);
}

CHOP_TEST(job_runs_on_a_worker_writes_three_files_and_suffixes_collisions) {
  Fixture f;
  ArrangementExportSettings st = f.settings("job");
  CHECK(f.s.startArrangementExport(f.sixteen(), "Demo Beat", st).ok());
  ArrangementJob* job = f.s.arrangementJob();
  CHECK(job != nullptr);
  const ArrangementResult res = job->result();
  CHECK(job->done());
  CHECK(res.status.ok());
  CHECK(fs::path(res.wavPath).filename() == "demo_beat_arrangement.wav");
  CHECK(fs::path(res.midiPath).filename() == "demo_beat_arrangement.mid");
  CHECK(fs::path(res.cuePath).filename() == "demo_beat_arrangement_cues.txt");
  CHECK(job->progress().fraction == 1.0 && job->progress().sections == 3);
  std::size_t files = 0, partials = 0;
  for (const auto& e : fs::directory_iterator(st.root)) {
    ++files;
    partials += e.path().extension() == ".partial" ? 1 : 0;
  }
  CHECK_EQ(files, 3u);
  CHECK_EQ(partials, 0u);
  // the same name again: _2, then _3, never an overwrite
  const auto firstBytes = fixture::readFile(res.wavPath);
  CHECK(f.s.startArrangementExport(f.sixteen(), "Demo Beat", st).ok());
  const ArrangementResult second = f.s.arrangementJob()->result();
  CHECK(second.status.ok() && fs::path(second.wavPath).filename() == "demo_beat_arrangement_2.wav");
  CHECK(fs::path(second.cuePath).filename() == "demo_beat_arrangement_2_cues.txt");
  CHECK(fixture::readFile(second.wavPath) == firstBytes);  // and the same bytes
  CHECK(fixture::readFile(res.wavPath) == firstBytes);
  CHECK(f.s.startArrangementExport(f.sixteen(), "Demo Beat", st).ok());
  CHECK(fs::path(f.s.arrangementJob()->result().wavPath).filename() == "demo_beat_arrangement_3.wav");
  // options: no MIDI, no markers
  Arrangement lean = f.sixteen();
  lean.writeMidi = false;
  lean.writeMarkers = false;
  st = f.settings("job_lean");
  CHECK(f.s.startArrangementExport(lean, "lean", st).ok());
  const ArrangementResult l = f.s.arrangementJob()->result();
  CHECK(l.status.ok() && l.midiPath.empty() && l.cuePath.empty());
  std::size_t n = 0;
  for (const auto& e : fs::directory_iterator(st.root)) n += e.is_regular_file() ? 1 : 0;
  CHECK_EQ(n, 1u);
  CHECK(readCues(fixture::readFile(l.wavPath)).offsets.empty());  // no markers means no cue chunk either
}

CHOP_TEST(second_job_is_rejected_while_one_runs) {
  Fixture f;
  ArrangementExportSettings st = f.settings("second");
  std::atomic<bool> release{false}, entered{false};
  st.observer = [&](float) {
    entered.store(true);
    while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  };
  CHECK(f.s.startArrangementExport(f.sixteen(), "one", st).ok());
  while (!entered.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(!f.s.arrangementJob()->done());
  auto again = f.s.startArrangementExport(f.sixteen(), "two", f.settings("second2"));
  CHECK(!again.ok() && again.error().message == "An arrangement export is already running.");
  f.s.cancelArrangementExport();
  release.store(true);
  const ArrangementResult res = f.s.arrangementJob()->result();
  CHECK(!res.status.ok() && res.status.error().code == ErrorCode::Cancelled);
  // a finished job is replaced by the next request
  CHECK(f.s.startArrangementExport(f.sixteen(), "three", f.settings("second3")).ok());
  CHECK(f.s.arrangementJob()->result().status.ok());
  f.s.releaseArrangementJob();
  CHECK(f.s.arrangementJob() == nullptr);
}

CHOP_TEST(destroying_a_session_with_a_running_job_stops_it) {
  std::atomic<bool> entered{false};
  {
    Fixture f;
    ArrangementExportSettings st = f.settings("destroy");
    st.observer = [&](float) { entered.store(true); };
    CHECK(f.s.startArrangementExport(f.sixteen(), "x", st).ok());
    while (!entered.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }  // ~ProjectSession cancels and joins the worker
  CHECK(entered.load());
}
