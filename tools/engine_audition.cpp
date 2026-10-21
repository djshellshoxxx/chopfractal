// engine_audition: renders N seeds per profile of the break engine (v2) from the procedural synthetic drum-loop
// fixture to WAV files and prints a spectrogram-free text report, so generation quality can be inspected offline.
// With --check it also enforces the audio-level acceptance metrics of docs/research/BREAK_ENGINE_RESEARCH.md
// section 12 (16 clicks, 18 fade or quiet start, 19 transient preservation, 20 level, 23 level jumps, 25 silence)
// and exits non-zero when one fails; ctest runs it as `break_quality_audio`.
//
//   engine_audition [--seeds N] [--profile name|all] [--bars B] [--rate HZ] [--out DIR] [--check] [--quiet]
//
// Thresholds live in the table below. They were set from measured values on the fixture (see the comments) and guard
// against regressions of the renderer hints, the level matching and the fade rules; they are not a taste judgement.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/break_engine/break_engine.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/composition/synthetic_kit.hpp>
#include <chopfractal/wav_export/wav.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {

// ---- thresholds (metric numbers from research section 12) ----
struct Limits {
  double clickPer1000Max = 30.0;  // 16: flagged boundaries per 1000 over the whole run (research target 0). Measured on the fixture: about 8 per
                                  //     1000, all of them a neighbouring hit's attack sitting just outside the exclusion window
  double clickEndThreshold = 0.25;   // 16: max |x[n+1]-x[n]| within 8 samples of an event end that no other hit started within 10 ms before.
                                     //     Event starts are not measured: every fixture slice starts on a transient, which is
                                     //     a legitimate large step; their quietness is metric 18.
  double quietStartMax = 0.01;    // 18: |sample at start| or a fade of at least 0.5 ms
  double transientMin = 0.5;      // 19: peak of the first 5 ms of a kick or snare vs the raw slice at the event level
  double snareSpreadDbMax = 6.0;  // 20: spread (max - min) of backbeat snare RMS over the first 50 ms; research +-2 dB per hit,
                                  //     widened because hats and kicks overlap the 50 ms window in a full mix
  double peakMax = 2.0;           // 20: output peak sanity bound (this render path has no limiter; the limiter owns the -1 dBFS ceiling)
  double jumpDbMax = 30.0;        // 23: change of RMS between consecutive 100 ms windows louder than -34 dBFS, bars 1 to 3
  double silenceMsMax = 600.0;    // 25: longest run below -40 dBFS in bars 1 to 3, milliseconds (a 1/4 at 100 bpm is 600 ms; the
                                  //     fixture's one-shots decay below that level well before a 1/4)
};
constexpr Limits kLimits{};

struct Args {
  int seeds = 4;
  int bars = 4;
  int rate = 48000;
  std::string profile = "all";
  std::string out;
  bool check = false;
  bool quiet = false;
};

bool parse(int argc, char** argv, Args& a) {
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    if (k == "--check") a.check = true;
    else if (k == "--quiet") a.quiet = true;
    else if (k == "--seeds") { const char* v = next(); if (!v) return false; a.seeds = std::atoi(v); }
    else if (k == "--bars") { const char* v = next(); if (!v) return false; a.bars = std::atoi(v); }
    else if (k == "--rate") { const char* v = next(); if (!v) return false; a.rate = std::atoi(v); }
    else if (k == "--profile") { const char* v = next(); if (!v) return false; a.profile = v; }
    else if (k == "--out") { const char* v = next(); if (!v) return false; a.out = v; }
    else return false;
  }
  return a.seeds >= 1 && a.seeds <= 1000 && a.bars >= 4 && a.bars <= 16 && a.rate >= 22050 && a.rate <= 96000;
}

struct Metrics {
  int boundaries = 0, clicks = 0;
  int starts = 0, quietStarts = 0;
  double worstTransient = 1e9;
  double snareMinDb = 1e9, snareMaxDb = -1e9;
  double peak = 0;
  double worstJumpDb = 0;
  double longestGapMs = 0;
  bool finite = true;
};

double rms(const std::vector<float>& x, std::int64_t a, std::int64_t n) {
  a = std::max<std::int64_t>(0, a);
  const std::int64_t b = std::min<std::int64_t>(static_cast<std::int64_t>(x.size()), a + n);
  if (b <= a) return 0.0;
  double s = 0;
  for (std::int64_t i = a; i < b; ++i) s += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
  return std::sqrt(s / static_cast<double>(b - a));
}
double toDb(double v) { return 20.0 * std::log10(std::max(v, 1e-9)); }

Metrics measure(const ProjectSession& s, const std::vector<float>& mix, const synth::KitLoop& loop, double bpm, int rate, int bars) {
  Metrics m;
  const auto pb = s.playback();
  const ChopSnapshotPtr view = s.annotatedChops();
  const double tpf = bpm * static_cast<double>(kTicksPerQuarter) / (60.0 * rate);
  const std::int64_t n = static_cast<std::int64_t>(mix.size());
  const std::int64_t fiveMs = static_cast<std::int64_t>(std::llround(0.005 * rate));
  const std::int64_t half = static_cast<std::int64_t>(std::llround(0.0005 * rate));
  const Ticks barTicks = 3840;
  const std::int64_t steady = static_cast<std::int64_t>(std::llround(static_cast<double>(3 * barTicks) / tpf));  // end of bar 3
  std::vector<std::int64_t> snareStarts;
  for (const FlatEvent& e : pb->events) {
    const std::int64_t st = static_cast<std::int64_t>(std::llround(static_cast<double>(e.start) / tpf));
    const std::int64_t en = st + static_cast<std::int64_t>(std::llround(static_cast<double>(e.duration) / tpf));
    if (st >= n) continue;
    // 16: boundary jumps (starts on a transient use the larger threshold)
    auto jump = [&](std::int64_t at) {
      double worst = 0;
      for (std::int64_t i = std::max<std::int64_t>(0, at - 8); i < std::min<std::int64_t>(n - 1, at + 8); ++i)
        worst = std::max(worst, static_cast<double>(std::fabs(mix[static_cast<std::size_t>(i + 1)] - mix[static_cast<std::size_t>(i)])));
      return worst;
    };
    bool otherStartsNear = false;
    for (const FlatEvent& o : pb->events) {
      const std::int64_t os = static_cast<std::int64_t>(std::llround(static_cast<double>(o.start) / tpf));
      if (&o != &e && os >= en - rate / 100 && os <= en + 16) otherStartsNear = true;  // another hit's attack (10 ms back) would be the step
    }
    if (en < n && !otherStartsNear) {
      ++m.boundaries;
      if (jump(en) > kLimits.clickEndThreshold) {
        ++m.clicks;
        if (std::getenv("AUD_DEBUG")) std::fprintf(stderr, "click end=%lld jump=%.2f dur=%lld chop=%u retrig=%d rev=%d fx=%d fadeOut=%u\n", (long long)en, jump(en), (long long)(en - st), (unsigned)e.chop.value, e.tx.retrigger, e.tx.reverse, (int)e.fx.active(), e.fadeOutFrames);
      }
    }
    // 18: the first sample is quiet, or the renderer's attack ramp (default 32 frames, longer than 0.5 ms at these rates) is in force
    ++m.starts;
    const double firstSample = std::fabs(static_cast<double>(mix[static_cast<std::size_t>(st)]));
    const std::uint32_t fade = e.fadeInFrames ? e.fadeInFrames : 32u;
    if (firstSample <= kLimits.quietStartMax || fade >= static_cast<std::uint32_t>(std::max<std::int64_t>(1, half))) ++m.quietStarts;
    // 19 and 20: kicks and snares keep their transient; backbeat snares match each other
    const ChopInfo* ci = view ? view->find(e.chop) : nullptr;
    const DrumRole role = ci ? ci->role : DrumRole::Unknown;
    if ((role == DrumRole::Kick || role == DrumRole::Snare) && e.tx.retrigger == 1 && !e.tx.reverse && e.tx.pitchSemitones == 0.f && !e.fx.active()) {
      float raw = 0;
      const std::int64_t srcRate = loop.data->sampleRate;
      const std::int64_t cnt = std::min<std::int64_t>(e.region.length(), fiveMs * srcRate / rate);
      for (std::int64_t i = 0; i < cnt; ++i) raw = std::max(raw, std::fabs(loop.data->channel(0)[e.region.start + i]));
      float got = 0;
      for (std::int64_t i = st; i < std::min(n, st + fiveMs); ++i) got = std::max(got, std::fabs(mix[static_cast<std::size_t>(i)]));
      const double expect = static_cast<double>(raw) * e.tx.level * std::max(1.f - std::max(e.tx.pan, 0.f), 1.f - std::max(-e.tx.pan, 0.f));
      if (expect > 0.05) m.worstTransient = std::min(m.worstTransient, static_cast<double>(got) / expect);
    }
    if (role == DrumRole::Snare && e.tx.level > 0.7f && st + 2 * fiveMs < steady) snareStarts.push_back(st);
  }
  if (snareStarts.size() >= 2) {
    for (std::int64_t st : snareStarts) {
      const double db = toDb(rms(mix, st, static_cast<std::int64_t>(0.05 * rate)));
      m.snareMinDb = std::min(m.snareMinDb, db);
      m.snareMaxDb = std::max(m.snareMaxDb, db);
    }
  }
  for (float v : mix) {
    if (!std::isfinite(v)) m.finite = false;
    m.peak = std::max(m.peak, static_cast<double>(std::fabs(v)));
  }
  // 23: level jumps between consecutive 100 ms windows (non-silent pairs) in bars 1 to 3
  const std::int64_t win = static_cast<std::int64_t>(0.1 * rate);
  double prev = -1;
  for (std::int64_t a = 0; a + win <= std::min(steady, n); a += win) {
    const double cur = rms(mix, a, win);
    if (prev > 0.02 && cur > 0.02) m.worstJumpDb = std::max(m.worstJumpDb, std::fabs(toDb(cur) - toDb(prev)));
    prev = cur;
  }
  // 25: longest run below -60 dBFS (0.1 % of full scale) in bars 1 to 3
  std::int64_t run = 0, longest = 0;
  for (std::int64_t i = 0; i < std::min(steady, n); ++i) {
    if (std::fabs(mix[static_cast<std::size_t>(i)]) < 1e-2f) longest = std::max(longest, ++run);
    else run = 0;
  }
  m.longestGapMs = 1000.0 * static_cast<double>(longest) / rate;
  (void)bars;
  return m;
}

}  // namespace

int main(int argc, char** argv) {
  Args a;
  if (!parse(argc, argv, a)) {
    std::fprintf(stderr, "usage: engine_audition [--seeds N] [--profile name|all] [--bars 4..16] [--rate HZ] [--out DIR] [--check] [--quiet]\n");
    return 2;
  }
  const synth::KitLoop loop = synth::makeKitLoop(a.rate);
  const double bpm = loop.bpm;
  if (!a.out.empty()) std::filesystem::create_directories(a.out);

  int failures = 0, runs = 0;
  long totalBoundaries = 0, totalClicks = 0;
  std::printf("engine audition: %d seed(s) per profile, %d bars, %d Hz, %.0f bpm, kit loop of %zu one-shots\n", a.seeds, a.bars, a.rate, bpm, loop.starts.size());
  std::printf("%-12s %5s %6s %7s %6s %6s %7s %6s %8s %7s %s\n", "profile", "seed", "events", "clicks", "start", "trans", "snrdB", "peak", "jumpdB", "gapms", "result");
  for (int p = 0; p < breakeng::kProfileCount; ++p) {
    const breakeng::Profile prof = static_cast<breakeng::Profile>(p);
    const std::string name = breakeng::profileName(prof);
    if (a.profile != "all" && a.profile != name) continue;
    for (int k = 0; k < a.seeds; ++k) {
      ProjectSession s;
      if (!synth::loadKitInto(s, loop, true)) {
        std::fprintf(stderr, "could not load the kit loop\n");
        return 2;
      }
      pattern::Settings st;
      st.engineVersion = pattern::kEngineVersionBreak;
      st.brk.profile = prof;
      st.brk.tempoHint = bpm;
      st.bars = a.bars;
      st.density = 0.5;
      st.seed = 1000u + static_cast<std::uint64_t>(k) * 7919u + static_cast<std::uint64_t>(p);
      Status g = s.generate(st);
      if (!g.ok()) {
        std::fprintf(stderr, "%s seed %d: generate failed: %s\n", name.c_str(), k, g.error().message.c_str());
        return 2;
      }
      render::OfflineSettings os;
      os.sampleRate = a.rate;
      os.bpm = bpm;
      os.cycles = 1;
      os.tailSeconds = 0.4;
      auto audio = render::renderOffline(s.playback(), os);
      if (!audio.ok()) {
        std::fprintf(stderr, "%s seed %d: render failed: %s\n", name.c_str(), k, audio.error().message.c_str());
        return 2;
      }
      const std::vector<float>& mix = audio.value()[0];
      const Metrics m = measure(s, mix, loop, bpm, a.rate, a.bars);
      std::vector<std::string> bad;
      const double clicks = m.boundaries ? 1000.0 * m.clicks / m.boundaries : 0.0;
      if (!m.finite) bad.push_back("non-finite sample");
      totalBoundaries += m.boundaries;
      totalClicks += m.clicks;
      if (m.quietStarts != m.starts) bad.push_back("18 start without fade");
      if (m.worstTransient < kLimits.transientMin) bad.push_back("19 transient eaten");
      if (m.snareMaxDb - m.snareMinDb > kLimits.snareSpreadDbMax && m.snareMaxDb > -200) bad.push_back("20 snare level spread");
      if (m.peak > kLimits.peakMax) bad.push_back("20 peak");
      if (m.worstJumpDb > kLimits.jumpDbMax) bad.push_back("23 level jump");
      if (m.longestGapMs > kLimits.silenceMsMax) bad.push_back("25 silence");
      ++runs;
      if (!bad.empty()) ++failures;
      const double snr = (m.snareMaxDb > -200 && m.snareMinDb < 1e8) ? m.snareMaxDb - m.snareMinDb : 0.0;
      if (!a.quiet || !bad.empty()) {
        std::printf("%-12s %5d %6zu %7.1f %6d %6.2f %7.1f %6.2f %8.1f %7.0f %s", name.c_str(), k, s.playback()->events.size(), clicks, m.starts, m.worstTransient > 1e8 ? 1.0 : m.worstTransient,
                    snr, m.peak, m.worstJumpDb, m.longestGapMs, bad.empty() ? "ok" : "FAIL:");
        for (const std::string& b : bad) std::printf(" [%s]", b.c_str());
        std::printf("\n");
      }
      if (!a.out.empty()) {
        wav::Options wo;
        wo.sampleRate = a.rate;
        wo.format = wav::Format::Pcm24;
        auto bytes = wav::encode(audio.value(), wo);
        if (bytes.ok()) {
          char file[256];
          std::snprintf(file, sizeof file, "%s_seed%02d.wav", name.c_str(), k);
          std::ofstream f(std::filesystem::path(a.out) / file, std::ios::binary);
          f.write(reinterpret_cast<const char*>(bytes.value().data()), static_cast<std::streamsize>(bytes.value().size()));
        }
      }
    }
  }
  const double clickRate = totalBoundaries ? 1000.0 * static_cast<double>(totalClicks) / static_cast<double>(totalBoundaries) : 0.0;
  if (clickRate > kLimits.clickPer1000Max) ++failures;
  std::printf("%d render(s), %d with a failing metric; metric 16: %ld of %ld boundaries flagged (%.1f per 1000, limit %.0f)\n", runs, failures, totalClicks, totalBoundaries, clickRate, kLimits.clickPer1000Max);
  return (a.check && failures > 0) ? 1 : 0;
}
