#include <algorithm>
#include <chopfractal/wav_export/wav.hpp>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::wav;

namespace {
std::vector<std::vector<float>> tone(int channels, std::size_t frames, float amp = 0.5f) {
  std::vector<std::vector<float>> a(static_cast<std::size_t>(channels), std::vector<float>(frames));
  for (std::size_t c = 0; c < a.size(); ++c)
    for (std::size_t i = 0; i < frames; ++i) a[c][i] = amp * std::sin(0.03f * static_cast<float>(i) * static_cast<float>(c + 1));
  return a;
}
std::string tmpDir() {
  auto p = std::filesystem::temp_directory_path() / "cf_wav_test";
  std::filesystem::create_directories(p);
  return p.string();
}
}  // namespace

CHOP_TEST(header_and_round_trip_for_every_format) {
  for (Format f : {Format::Pcm16, Format::Pcm24, Format::Float32})
    for (int ch : {1, 2}) {
      Options o;
      o.format = f;
      o.dither = false;
      o.sampleRate = 44100;
      const auto in = tone(ch, 1001);
      auto bytes = encode(in, o);
      CHECK(bytes.ok());
      if (!bytes.ok()) continue;
      const auto& b = bytes.value();
      CHECK(std::string(b.begin(), b.begin() + 4) == "RIFF" && std::string(b.begin() + 8, b.begin() + 12) == "WAVE");
      CHECK_EQ(b.size() % 2, 0u);  // chunks are word aligned
      auto back = decode(b.data(), b.size());
      CHECK(back.ok());
      if (!back.ok()) continue;
      CHECK(back.value().format == f && back.value().sampleRate == 44100 && back.value().planar.size() == static_cast<std::size_t>(ch));
      const double tol = f == Format::Pcm16 ? 1.0 / 32768 : f == Format::Pcm24 ? 1.0 / 8388608 : 0.0;
      for (int c = 0; c < ch; ++c)
        for (std::size_t i = 0; i < in[static_cast<std::size_t>(c)].size(); ++i)
          CHECK_NEAR(back.value().planar[static_cast<std::size_t>(c)][i], in[static_cast<std::size_t>(c)][i], tol);
    }
}

CHOP_TEST(dither_is_deterministic_bounded_and_seed_dependent) {
  std::vector<std::vector<float>> silence(2, std::vector<float>(2000, 0.f));
  Options o;
  o.format = Format::Pcm16;
  o.dither = true;
  o.ditherSeed = 7;
  auto a = encode(silence, o), b = encode(silence, o);
  CHECK(a.ok() && b.ok() && a.value() == b.value());
  o.ditherSeed = 8;
  auto c = encode(silence, o);
  CHECK(c.ok() && c.value() != a.value());
  auto back = decode(a.value().data(), a.value().size());
  CHECK(back.ok());
  bool any = false;
  for (float x : back.value().planar[0]) {
    CHECK(std::fabs(x) <= 1.0 / 32768 + 1e-9);  // TPDF of one LSB peak
    any = any || x != 0.f;
  }
  CHECK(any);
  o.dither = false;
  auto flat = encode(silence, o);
  auto fb = decode(flat.value().data(), flat.value().size());
  for (float x : fb.value().planar[0]) CHECK(x == 0.f);
}

CHOP_TEST(normalization_clipping_and_bad_input) {
  auto in = tone(1, 500, 0.25f);
  Options o;
  o.format = Format::Float32;
  o.normalize = true;
  o.ceilingDb = -6.f;
  EncodeReport rep;
  auto n = encode(in, o, &rep);
  CHECK(n.ok());
  auto back = decode(n.value().data(), n.value().size());
  float peak = 0;
  for (float x : back.value().planar[0]) peak = std::max(peak, std::fabs(x));
  CHECK_NEAR(peak, std::pow(10.0, -6.0 / 20.0), 1e-5);
  CHECK_NEAR(rep.peak, 0.25, 1e-3);

  auto hot = tone(1, 500, 2.0f);
  o.normalize = false;
  o.format = Format::Pcm16;
  o.dither = false;
  auto h = encode(hot, o, &rep);
  CHECK(h.ok() && rep.clipped > 0);
  o.format = Format::Float32;
  CHECK(encode(hot, o, &rep).ok() && rep.clipped == 0);

  std::vector<std::vector<float>> nan(1, std::vector<float>(10, std::nanf("")));
  auto z = encode(nan, Options{});
  CHECK(z.ok());
  auto zb = decode(z.value().data(), z.value().size());
  for (float x : zb.value().planar[0]) CHECK(x == 0.f);

  CHECK(!encode({}, Options{}).ok());
  CHECK(!encode(std::vector<std::vector<float>>(3, std::vector<float>(4)), Options{}).ok());
  CHECK(!encode({std::vector<float>(4), std::vector<float>(5)}, Options{}).ok());
  Options bad;
  bad.sampleRate = 100;
  CHECK(!encode(tone(1, 4), bad).ok());
  bad = Options{};
  bad.ceilingDb = 3.f;
  CHECK(!encode(tone(1, 4), bad).ok());
}

CHOP_TEST(decoder_rejects_malformed_files_without_crashing) {
  auto good = encode(tone(2, 100), Options{}).value();
  CHECK(!decode(nullptr, 0).ok());
  for (std::size_t cut = 0; cut < good.size(); cut += 7) {
    auto r = decode(good.data(), cut);  // every truncation is handled
    if (cut < 44) CHECK(!r.ok());
  }
  auto bad = good;
  bad[0] = 'X';
  CHECK(!decode(bad.data(), bad.size()).ok());
  bad = good;
  bad[20] = 5;  // unknown format tag
  CHECK(!decode(bad.data(), bad.size()).ok());
  bad = good;
  bad[40] = 0xFF;
  bad[41] = 0xFF;
  bad[42] = 0xFF;
  bad[43] = 0x7F;  // data length larger than the file
  CHECK(!decode(bad.data(), bad.size()).ok());
}

CHOP_TEST(atomic_write_refuses_overwrite_and_leaves_no_temp_file) {
  namespace fs = std::filesystem;
  const std::string dir = tmpDir();
  const std::string path = dir + "/a.wav";
  fs::remove(path);
  const std::vector<std::uint8_t> one{1, 2, 3}, two{9, 9};
  CHECK(writeFileAtomic(path, one, false).ok());
  auto again = writeFileAtomic(path, two, false);
  CHECK(!again.ok() && again.error().code == ErrorCode::Conflict);
  {
    std::ifstream f(path, std::ios::binary);
    std::vector<char> got((std::istreambuf_iterator<char>(f)), {});
    CHECK_EQ(got.size(), 3u);  // existing file untouched
  }
  CHECK(writeFileAtomic(path, two, true).ok());
  CHECK_EQ(fs::file_size(path), 2u);
  CHECK(!fs::exists(path + ".cf-tmp"));
  CHECK(!writeFileAtomic(dir + "/missing_dir/x.wav", one, false).ok());
  CHECK(!writeFileAtomic("", one, false).ok());
  fs::remove(path);
}

CHOP_TEST(file_names_are_sanitized) {
  CHECK(sanitizeFileName("Snare Hit #2!") == "snare_hit_2");
  CHECK(sanitizeFileName("../../etc/passwd") == "etc_passwd");
  CHECK(sanitizeFileName("") == "untitled");
  CHECK(sanitizeFileName("***") == "untitled");
  CHECK(sanitizeFileName(std::string(200, 'a')).size() == 48);
  CHECK(sanitizeFileName("kick-01") == "kick-01");
}

CHOP_TEST(normalizing_to_full_scale_is_not_reported_as_clipping) {
  Options o;
  o.format = Format::Pcm16;
  o.dither = false;
  o.normalize = true;
  o.ceilingDb = 0.f;
  EncodeReport rep;
  CHECK(encode(tone(1, 500, 0.3f), o, &rep).ok());
  CHECK_EQ(rep.clipped, 0u);
  o.format = Format::Pcm24;
  CHECK(encode(tone(1, 500, 0.3f), o, &rep).ok() && rep.clipped == 0);
}
