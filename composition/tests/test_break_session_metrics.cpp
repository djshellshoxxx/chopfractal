// Break engine v2 through the real session API: objective musical and audio checks over every profile, several
// seeds and varied density / chaos / glitch / stutter (docs/specs/break-engine-v2.md, "Tests"). Settings come from
// host parameters (settingsFromParams) so the defaults of a NEW project are the ones under test.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/composition/synthetic_kit.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {
struct Acc {
  int bars = 0, kick1 = 0, snare5 = 0, snare13 = 0, kickSnareSame = 0, fillBars = 0, fillOk = 0;
  int maxPerBar = 0, events = 0, maxRun = 0, dupStarts = 0;
};

host::ParamValues paramsFor(int profile, double density, double chaos, double glitch) {
  host::ParamValues v = host::ParamValues::defaults();
  v.set(host::kBreakProfile, profile);
  v.set(host::kDensity, density);
  v.set(host::kChaos, chaos);
  v.set(host::kBreakGlitch, glitch);
  return v;
}

void scan(ProjectSession& s, Acc& a) {
  const pattern::Pattern& p = *s.pattern();
  const ChopSnapshotPtr chops = s.annotatedChops();
  ChopId prev{};
  int run = 0;
  for (std::size_t b = 0; b < p.bars.size(); ++b) {
    std::map<int, int> roleAtStep;
    std::vector<Ticks> kickAt, snareAt;
    int n = 0, lastQuarter = 0;
    std::vector<std::pair<Ticks, const Event*>> evs;
    for (const pattern::Beat& bt : p.bars[b].beats)
      for (const Event& e : bt.events) evs.push_back({e.start, &e});
    std::sort(evs.begin(), evs.end(), [](auto& x, auto& y) { return x.first < y.first; });
    Ticks lastStart = -1;
    for (auto& [t, e] : evs) {
      ++n;
      if (t == lastStart) ++a.dupStarts;
      lastStart = t;
      run = (n > 1 || b > 0) && e->chop == prev ? run + 1 : 1;
      prev = e->chop;
      a.maxRun = std::max(a.maxRun, run);
      if (auto d = pattern::describeEvent(p, e->id, *chops)) {
        if (d->role == DrumRole::Kick) kickAt.push_back(t);
        if (d->role == DrumRole::Snare) snareAt.push_back(t);
        roleAtStep[d->step] |= 1 << static_cast<int>(d->role);
        if (d->step >= 12) ++lastQuarter;
      }
    }
    ++a.bars;
    a.events += n;
    a.maxPerBar = std::max(a.maxPerBar, n);
    auto has = [&](int step, DrumRole r) { return (roleAtStep[step] & (1 << static_cast<int>(r))) != 0; };
    a.kick1 += has(0, DrumRole::Kick);
    a.snare5 += has(4, DrumRole::Snare);
    a.snare13 += has(12, DrumRole::Snare);
    for (Ticks k : kickAt) a.kickSnareSame += std::count(snareAt.begin(), snareAt.end(), k);
    if (b % 4 == 3) {
      ++a.fillBars;
      a.fillOk += lastQuarter >= 2;
    }
  }
}
}  // namespace

CHOP_TEST(session_break_metrics_hold_across_profiles_seeds_and_macros) {
  const bool verbose = std::getenv("BREAK_METRICS_VERBOSE") != nullptr;
  for (int prof = 0; prof < breakeng::kProfileCount; ++prof) {
    Acc a;
    for (int seed = 1; seed <= 6; ++seed)
      for (double density : {0.3, 0.5, 0.8})
        for (double chaos : {0.0, 1.0})
          for (double glitch : {0.0, 1.0}) {
            ProjectSession s;
            CHECK(synth::loadKitInto(s, synth::makeKitLoop(), true));
            pattern::Settings st = settingsFromParams(paramsFor(prof, density, chaos, glitch), pattern::Settings{});
            CHECK_EQ(st.engineVersion, pattern::kEngineVersionBreak);
            st.seed = static_cast<std::uint64_t>(seed) * 977u + static_cast<std::uint64_t>(prof);
            CHECK(s.generate(st).ok());
            CHECK_EQ(s.pattern()->engineVersion, pattern::kEngineVersionBreak);
            scan(s, a);
          }
    // Floors sit below the measured values (strict: kick1 .81-.98, snare5 .94-1, snare13 .77-1; soft: kick1 .38-.65, snare5 .65-.83,
    // snare13 .46-.69; sweep includes chaos 1 and glitch 1) so only a real regression trips them.
    struct Band { double k1, s5, s13, lo, hi; int maxRun; };
    static const Band kBands[breakeng::kProfileCount] = {
        {0.75, 0.88, 0.70, 14, 24, 4},  // Amen / Funk
        {0.75, 0.88, 0.70, 14, 24, 4},  // Jungle
        {0.72, 0.88, 0.70, 16, 31, 4},  // DnB rolling
        {0.55, 0.70, 0.55, 16, 30, 8},  // Breakcore
        {0.30, 0.50, 0.35, 14, 26, 8},  // IDM glitch
        {0.85, 0.88, 0.88, 12, 21, 4},  // Hip-hop
    };
    const Band& bd = kBands[prof];
    const double nb = a.bars, avg = double(a.events) / nb;
    CHECK(a.kick1 / nb >= bd.k1);
    CHECK(a.snare5 / nb >= bd.s5);
    CHECK(a.snare13 / nb >= bd.s13);
    CHECK(avg >= bd.lo && avg <= bd.hi);       // hit density per bar, against docs/research/break_stats.json (17-19 funk, 15 hip-hop, plus hat layers)
    CHECK(a.maxPerBar <= 36);
    CHECK(a.maxRun <= bd.maxRun);               // identical-slice runs
    CHECK_EQ(a.kickSnareSame, 0);               // kick and snare never share a start
    CHECK(a.fillOk >= a.fillBars * 8 / 10);     // bar 4 of each phrase carries a fill in the last quarter
    if (verbose)
      std::fprintf(stderr, "prof %d bars %d k1 %.2f s5 %.2f s13 %.2f ks %d fill %.2f perBar avg %.1f max %d run %d dup %d\n", prof, a.bars,
                   double(a.kick1) / a.bars, double(a.snare5) / a.bars, double(a.snare13) / a.bars, a.kickSnareSame,
                   a.fillBars ? double(a.fillOk) / a.fillBars : 0.0, double(a.events) / a.bars, a.maxPerBar, a.maxRun, a.dupStarts);
  }
}

CHOP_TEST(glitch_and_stutter_macros_change_the_output_on_their_own) {
  // Glitch: audible without touching Allow Reverse / Retrigger / Tape Glide, in the glitch profiles and the strict ones.
  for (int prof : {1, 3, 4}) {
    int differs = 0;
    for (int seed = 1; seed <= 6; ++seed) {
      std::vector<std::uint8_t> bytes[2];
      for (int g = 0; g < 2; ++g) {
        ProjectSession s;
        CHECK(synth::loadKitInto(s, synth::makeKitLoop(), true));
        pattern::Settings st = settingsFromParams(paramsFor(prof, 0.5, 0.0, g ? 1.0 : 0.0), pattern::Settings{});
        st.seed = static_cast<std::uint64_t>(seed) * 31u;
        CHECK(s.generate(st).ok());
        bytes[g] = pattern::serialize(*s.pattern());
      }
      differs += bytes[0] != bytes[1];
    }
    CHECK(differs >= 5);
  }
  // Stutter: the macro value alone reaches the pattern once the host switches Allow Stutter on (the macro strip does
  // that when the knob is turned up); the Stutter blocks then appear on playback events.
  ProjectSession s;
  CHECK(synth::loadKitInto(s, synth::makeKitLoop(), true));
  CHECK(s.generate(settingsFromParams(paramsFor(1, 0.5, 0.0, 0.5), pattern::Settings{})).ok());
  host::ParamValues v = host::ParamValues::defaults();
  v.set(host::kAllowStutter, 1.0);
  v.set(host::kStutterAmount, 100.0);
  CHECK(s.setFx(fxSettingsFromParams(v, 7), render::PlaybackOptions{}).ok());
  int stutters = 0;
  for (const FlatEvent& e : s.playback()->events) stutters += e.stutter.has_value();
  CHECK(stutters >= 1);
}

CHOP_TEST(session_break_audio_is_clean_limited_and_smoothly_faded) {
  const double bpm = 100.0;
  const int rate = 48000;
  int boundaries = 0, clicks = 0, chokeHinted = 0, v2Events = 0, smooth = 0;
  for (int prof = 0; prof < breakeng::kProfileCount; ++prof)
    for (int seed = 1; seed <= 2; ++seed) {
      ProjectSession s;
      CHECK(synth::loadKitInto(s, synth::makeKitLoop(rate), true));
      pattern::Settings st = settingsFromParams(paramsFor(prof, seed == 1 ? 0.8 : 0.4, seed == 1 ? 1.0 : 0.0, 1.0), pattern::Settings{});
      st.seed = static_cast<std::uint64_t>(seed) * 4099u + static_cast<std::uint64_t>(prof);
      st.brk.tempoHint = bpm;
      CHECK(s.generate(st).ok());
      host::ParamValues fxv = host::ParamValues::defaults();
      fxv.set(host::kAllowStutter, 1.0);
      fxv.set(host::kStutterAmount, 60.0);
      CHECK(s.setFx(fxSettingsFromParams(fxv, 3), render::PlaybackOptions{}).ok());
      render::OfflineSettings os;
      os.sampleRate = rate;
      os.bpm = bpm;
      os.tailSeconds = 0.4;
      os.params.slicePolish = true;  // plugin default (slice_polish)
      os.params.limiterEnabled = true;
      os.params.limiterCeilingDb = -1.f;
      auto audio = render::renderOffline(s.playback(), os);
      CHECK(audio.ok());
      float peak = 0.f;
      bool finite = true;
      for (const auto& ch : audio.value())
        for (float x : ch) {
          finite = finite && std::isfinite(x);
          peak = std::max(peak, std::fabs(x));
        }
      CHECK(finite);
      CHECK(peak <= 0.8913f + 0.01f);  // limiter ceiling -1 dBFS
      CHECK(peak > 0.05f);             // and not silent
      // Discontinuity at hit ends that no other attack covers (the audition's metric 16, now with polish and limiter on).
      const std::vector<float>& mix = audio.value()[0];
      const double tpf = bpm * kTicksPerQuarter / (60.0 * rate);
      const auto& evs = s.playback()->events;
      for (const FlatEvent& e : evs) {
        ++v2Events;
        chokeHinted += e.chokeGroup != 0;
        smooth += e.smoothFades;
        const std::int64_t en = std::llround(static_cast<double>(e.start + e.duration) / tpf);
        if (en + 9 >= static_cast<std::int64_t>(mix.size()) || en < 8) continue;
        bool covered = false;
        for (const FlatEvent& o : evs) {
          const std::int64_t os2 = std::llround(static_cast<double>(o.start) / tpf);
          if (&o != &e && os2 >= en - rate / 100 && os2 <= en + 16) covered = true;
        }
        if (covered) continue;
        ++boundaries;
        float worst = 0.f;
        for (std::int64_t i = en - 8; i < en + 8; ++i) worst = std::max(worst, std::fabs(mix[static_cast<std::size_t>(i + 1)] - mix[static_cast<std::size_t>(i)]));
        clicks += worst > 0.25f;
      }
    }
  if (std::getenv("BREAK_METRICS_VERBOSE")) std::fprintf(stderr, "audio boundaries %d clicks %d events %d choke %d smooth %d\n", boundaries, clicks, v2Events, chokeHinted, smooth);
  CHECK(boundaries > 100);
  CHECK(clicks * 1000 <= boundaries * 30);  // at most 3 percent flagged (the audition limit)
  CHECK(smooth == v2Events);                // every v2 event carries the smooth-fade hint
  CHECK(chokeHinted > v2Events / 4);        // kicks, snares and hats carry choke groups
}
