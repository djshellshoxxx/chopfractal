// Effect audition tool: renders every renderer effect over a synthetic drum loop and writes one WAV per effect
// (32-bit float, stereo) so each can be listened to and compared with the plain loop.
//   fx_audition [output-directory]      default: ./fx_audition_out
// Output is deterministic: the same build always writes byte-identical files.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/chop_contracts/rng.hpp>

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace chopfractal;
using namespace chopfractal::render;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kRate = 48000;
constexpr int kSlice = 12000;  // 4 slices: kick, closed hat, snare, open hat

SourcePtr drumSource() {
  auto s = std::make_shared<SourceData>();
  s->channels = 2;
  s->sampleRate = kRate;
  s->frames = 4 * kSlice;
  s->samples.assign(static_cast<std::size_t>(2 * s->frames), 0.f);
  Rng rng(7);
  float* l = s->samples.data();
  float* r = l + s->frames;
  for (int i = 0; i < kSlice; ++i) {  // kick: decaying sine sweep
    const double t = i / static_cast<double>(kRate);
    const double ph = 2 * kPi * (50.0 * t + 90.0 * (1 - std::exp(-t * 40)) / 40.0);
    const float v = static_cast<float>(0.9 * std::sin(ph) * std::exp(-t * 11.0));
    l[i] = r[i] = v;
  }
  float lp = 0.f;
  for (int i = 0; i < kSlice; ++i) {  // closed hat: high-passed noise, very short
    const double t = i / static_cast<double>(kRate);
    const float n = static_cast<float>(rng.uniform01() * 2 - 1);
    const float hp = n - lp;
    lp += 0.3f * hp;
    l[kSlice + i] = r[kSlice + i] = 0.5f * hp * static_cast<float>(std::exp(-t * 90.0));
  }
  for (int i = 0; i < kSlice; ++i) {  // snare: noise burst and a 190 Hz body
    const double t = i / static_cast<double>(kRate);
    const float n = static_cast<float>(rng.uniform01() * 2 - 1);
    const float v = static_cast<float>(0.55 * n * std::exp(-t * 22.0) + 0.45 * std::sin(2 * kPi * 190.0 * t) * std::exp(-t * 28.0));
    l[2 * kSlice + i] = r[2 * kSlice + i] = v;
  }
  lp = 0.f;
  for (int i = 0; i < kSlice; ++i) {  // open hat
    const double t = i / static_cast<double>(kRate);
    const float n = static_cast<float>(rng.uniform01() * 2 - 1);
    const float hp = n - lp;
    lp += 0.3f * hp;
    l[3 * kSlice + i] = r[3 * kSlice + i] = 0.45f * hp * static_cast<float>(std::exp(-t * 14.0));
  }
  return s;
}

enum Which : unsigned { kEcho = 1, kTrem = 2, kStutter = 4, kRoll = 8 };

// Two bars at 120 BPM: kick on 1 and the "and" of 2, snare on 2 and 4, hats on every eighth, an open hat before the bar.
FlatEventList pattern(unsigned fx) {
  FlatEventList list;
  std::uint64_t id = 1;
  auto add = [&](int chop, Ticks start, Ticks dur) -> std::size_t {
    FlatEvent e;
    e.id = EventId{id++};
    e.chop = ChopId{static_cast<std::uint64_t>(chop + 1)};
    e.region = {static_cast<std::int64_t>(chop) * kSlice, static_cast<std::int64_t>(chop + 1) * kSlice};
    e.start = start;
    e.duration = dur;
    list.push_back(e);
    return list.size() - 1;  // an index: the vector grows, references would dangle
  };
  for (int bar = 0; bar < 2; ++bar) {
    const Ticks b = bar * 3840;
    add(0, b, 960);
    add(0, b + 1920 + 480, 480);
    const std::size_t sn1 = add(2, b + 960, 960);
    const std::size_t sn2 = add(2, b + 2880, 960);
    for (int h = 0; h < 8; ++h) {
      const std::size_t hat = add((h == 7 && bar == 1) ? 3 : 1, b + h * 480, 480);
      if ((fx & kTrem) && (h % 2) == 1) list[hat].tremPan = TremPanFx{2, 1, 70};
    }
    if (fx & kEcho) {
      list[sn1].echo = EchoFx{3, 255, 255};
      list[sn2].echo = EchoFx{4, 1, 60};
    }
    if (fx & kStutter) {
      StutterFx f;
      f.rate = 4;
      f.rateEnd = 6;
      f.pitch = -30;
      list[sn2].stutter = f;
      list[sn2].echo.reset();
    }
    if (fx & kRoll) {
      RollFx f;
      f.rate = 3;
      f.rateEnd = 8;
      f.flags = kRollSmooth;
      list[sn1].roll = f;
      list[sn1].echo.reset();
    }
  }
  sortFlat(list);
  return list;
}

bool writeWav(const std::filesystem::path& path, const std::vector<std::vector<float>>& a) {
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  const std::uint32_t frames = static_cast<std::uint32_t>(a[0].size());
  const std::uint32_t dataBytes = frames * 8;
  auto u32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) f.put(static_cast<char>((v >> (8 * i)) & 0xFF)); };
  auto u16 = [&](std::uint16_t v) { f.put(static_cast<char>(v & 0xFF)); f.put(static_cast<char>(v >> 8)); };
  f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVEfmt ", 8);
  u32(16); u16(3); u16(2); u32(kRate); u32(kRate * 8); u16(8); u16(32);
  f.write("data", 4); u32(dataBytes);
  for (std::uint32_t i = 0; i < frames; ++i)
    for (int c = 0; c < 2; ++c) {
      std::uint32_t u;
      std::memcpy(&u, &a[static_cast<std::size_t>(c)][i], 4);
      u32(u);
    }
  return static_cast<bool>(f);
}

struct Case {
  const char* name;
  unsigned fx;
  RenderParams params;
  PlaybackOptions options;
};

}  // namespace

int main(int argc, char** argv) {
  const std::filesystem::path dir = argc > 1 ? argv[1] : "fx_audition_out";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    std::fprintf(stderr, "cannot create %s\n", dir.string().c_str());
    return 1;
  }
  const SourcePtr src = drumSource();

  RenderParams plain;
  RenderParams polish;
  polish.slicePolish = true;
  RenderParams limited = polish;
  limited.limiterEnabled = true;
  limited.limiterCeilingDb = -1.f;
  limited.matchGainDb = 6.f;
  PlaybackOptions dark;
  dark.echoRate = 2;
  dark.echoDecay = 70;
  dark.echoTone = 90;
  PlaybackOptions deep;
  deep.tremMode = 1;
  deep.tremRate = 0;
  deep.tremDepth = 90;

  const Case cases[] = {
      {"01_plain_v1", 0, plain, {}},
      {"02_slice_polish", 0, polish, {}},
      {"03_echo", kEcho, polish, {}},
      {"04_echo_dotted_dark", kEcho, polish, dark},
      {"05_tremolo_pan", kTrem, polish, {}},
      {"06_tremolo_pan_fast_pan", kTrem, polish, deep},
      {"07_stutter", kStutter, polish, {}},
      {"08_roll", kRoll, polish, {}},
      {"09_all_effects", kEcho | kTrem | kStutter | kRoll, polish, {}},
      {"10_all_effects_matched_limited", kEcho | kTrem | kStutter | kRoll, limited, {}},
  };
  for (const Case& c : cases) {
    auto pb = makePlayback(src, pattern(c.fx), 2 * 3840, c.options);
    if (!pb.ok()) {
      std::fprintf(stderr, "%s: %s\n", c.name, pb.error().message.c_str());
      return 1;
    }
    OfflineSettings o;
    o.sampleRate = kRate;
    o.bpm = 120.0;
    o.cycles = 2;
    o.tailSeconds = 1.0;
    o.params = c.params;
    o.params.fxMask = fxbit::kAll;
    auto audio = renderOffline(pb.value(), o);
    if (!audio.ok()) {
      std::fprintf(stderr, "%s: %s\n", c.name, audio.error().message.c_str());
      return 1;
    }
    float peak = 0.f;
    for (float x : audio.value()[0]) peak = std::max(peak, std::fabs(x));
    const std::filesystem::path out = dir / (std::string(c.name) + ".wav");
    if (!writeWav(out, audio.value())) {
      std::fprintf(stderr, "cannot write %s\n", out.string().c_str());
      return 1;
    }
    if (c.params.limiterEnabled && peak > std::pow(10.f, c.params.limiterCeilingDb / 20.f) * 1.0001f) {
      std::fprintf(stderr, "%s: peak %.4f is over the limiter ceiling\n", c.name, static_cast<double>(peak));
      return 1;
    }
    std::printf("%-34s %6zu frames  peak %.3f\n", c.name, audio.value()[0].size(), static_cast<double>(peak));
  }
  return 0;
}
