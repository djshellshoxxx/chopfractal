#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/composition/synthetic_kit.hpp>

#include <cmath>

namespace chopfractal::composition::synth {
namespace {
constexpr double kPi = 3.14159265358979323846;

struct Voice {
  DrumRole role;
  double seconds;  // slot length
};

// One one-shot written into `out` starting at sample `at`. Noise comes from the shared Rng so the file is reproducible.
void render(DrumRole role, int variant, std::vector<float>& out, std::int64_t at, std::int64_t len, int sr, Rng& rng, double gain) {
  auto noise = [&] { return rng.uniform01() * 2.0 - 1.0; };
  const double dt = 1.0 / static_cast<double>(sr);
  const std::int64_t n = static_cast<std::int64_t>(out.size());
  double hp = 0.0;  // one-pole difference for the high-passed noise
  double lp1 = 0.0, lp2 = 0.0;
  for (std::int64_t i = 0; at + i < n && i < len; ++i) {  // a one-shot stays inside its own slot
    const double t = static_cast<double>(i) * dt;
    double v = 0.0;
    switch (role) {
      case DrumRole::Kick: {
        const double f0 = variant == 0 ? 62.0 : 55.0;
        const double f = f0 * (1.0 + 1.6 * std::exp(-t / 0.018));  // pitch drop
        const double tail = variant == 0 ? 0.20 : 0.13;
        v = std::sin(2.0 * kPi * f * t) * std::exp(-t / tail) + 0.5 * noise() * std::exp(-t / 0.002);
        break;
      }
      case DrumRole::Snare:
      case DrumRole::Ghost: {
        const double tone = variant == 0 ? 190.0 : 225.0;
        const double d = variant == 0 ? 0.11 : 0.14;
        // Wires: noise low-passed twice (about 3 kHz) so the body is broadband but not "bright".
        lp1 += 0.45 * (noise() - lp1);
        lp2 += 0.45 * (lp1 - lp2);
        v = 0.55 * std::sin(2.0 * kPi * tone * t) * std::exp(-t / 0.07) + 1.4 * lp2 * std::exp(-t / d);
        break;
      }
      case DrumRole::Hat:
      case DrumRole::OpenHat:
      case DrumRole::Cymbal: {
        const double d = role == DrumRole::Cymbal ? 0.45 : (role == DrumRole::OpenHat ? 0.20 : (variant == 0 ? 0.030 : (variant == 1 ? 0.040 : 0.022)));
        const double x = noise();
        hp = x - 0.2 * hp;  // emphasises the top end
        v = 0.7 * hp * std::exp(-t / d);
        break;
      }
      default:  // a tom
        v = std::sin(2.0 * kPi * (118.0 + 40.0 * std::exp(-t / 0.05)) * t) * std::exp(-t / 0.15);
        break;
    }
    if (at + i < n) out[static_cast<std::size_t>(at + i)] += static_cast<float>(v * gain);
  }
}
}  // namespace

KitLoop makeKitLoop(int sampleRate) {
  KitLoop k;
  const int sr = sampleRate < 8000 ? 8000 : (sampleRate > 192000 ? 192000 : sampleRate);
  const Voice voices[12] = {{DrumRole::Kick, 0.30}, {DrumRole::Kick, 0.30}, {DrumRole::Snare, 0.30}, {DrumRole::Snare, 0.30},
                            {DrumRole::Ghost, 0.30}, {DrumRole::Hat, 0.12}, {DrumRole::Hat, 0.12}, {DrumRole::Hat, 0.12},
                            {DrumRole::OpenHat, 0.40}, {DrumRole::Cymbal, 0.90}, {DrumRole::Perc, 0.30}, {DrumRole::Hat, 0.12}};
  std::int64_t total = 0;
  for (const Voice& v : voices) total += static_cast<std::int64_t>(v.seconds * sr);
  auto data = std::make_shared<render::SourceData>();
  data->channels = 1;
  data->sampleRate = sr;
  data->frames = total;
  data->samples.assign(static_cast<std::size_t>(total), 0.f);
  Rng rng(0xD12AB);
  std::int64_t at = 0;
  int kickN = 0, snareN = 0, hatN = 0;
  for (const Voice& v : voices) {
    k.starts.push_back(at);
    k.roles.push_back(v.role);
    int variant = 0;
    double gain = 0.8;
    if (v.role == DrumRole::Kick) variant = kickN++;
    if (v.role == DrumRole::Snare) variant = snareN++;
    if (v.role == DrumRole::Ghost) { variant = 0; gain = 0.8 * 0.2; }  // a ghost is the snare 14 dB down
    if (v.role == DrumRole::Hat) { variant = hatN++ % 3; gain = 0.5; }
    if (v.role == DrumRole::OpenHat) gain = 0.5;
    if (v.role == DrumRole::Cymbal) gain = 0.5;
    const std::int64_t len = static_cast<std::int64_t>(v.seconds * sr);
    render(v.role, variant, data->samples, at, len, sr, rng, gain);
    // The slot ends with a 5 ms fade so the cut is clean.
    const std::int64_t fade = sr / 200;
    for (std::int64_t i = 0; i < fade; ++i) {
      const std::int64_t idx = at + len - fade + i;
      if (idx >= 0 && idx < total) data->samples[static_cast<std::size_t>(idx)] *= static_cast<float>(1.0 - static_cast<double>(i) / static_cast<double>(fade));
    }
    at += len;
  }
  k.data = data;
  return k;
}

bool loadKitInto(ProjectSession& session, const KitLoop& loop, bool assignRoles, const std::string& name) {
  if (!loop.data || loop.starts.empty()) return false;
  if (!session.loadSource(loop.data, name).ok()) return false;
  const bool placed = session
                          .editMarkers([&](source::ChopMap& m) -> Status {
                            for (std::int64_t s : loop.starts) {
                              auto r = m.addMarker(s, true);
                              if (!r.ok() && s != 0) return r.error();
                            }
                            return {};
                          })
                          .ok();
  if (!placed || !assignRoles) return placed;
  const ChopSnapshotPtr chops = session.chops();
  if (!chops || chops->chops.size() != loop.roles.size()) return false;
  for (std::size_t i = 0; i < loop.roles.size(); ++i) {
    const char* id = nullptr;
    switch (loop.roles[i]) {
      case DrumRole::Kick: id = "kick"; break;
      case DrumRole::Snare: id = "snare"; break;
      case DrumRole::Hat: id = "hat"; break;
      case DrumRole::Cymbal: id = "cymbal"; break;
      case DrumRole::Perc: id = "percussion"; break;
      default: break;  // ghost and open hat are detected from the audio
    }
    if (id && !session.assignRole(chops->chops[i].id, id).ok()) return false;
  }
  return true;
}

}  // namespace chopfractal::composition::synth
