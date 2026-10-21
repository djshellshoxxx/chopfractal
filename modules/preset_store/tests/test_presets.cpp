#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/preset_store/presets.hpp>
#include <cmath>
#include <set>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::presets;

CHOP_TEST(factory_presets_are_valid_distinct_and_round_trip) {
  const auto& list = factoryPresets();
  CHECK_EQ(list.size(), 17u);
  std::set<std::string> names, files;
  for (const Preset& p : list) {
    CHECK(validate(p).ok());
    CHECK(names.insert(p.name).second && files.insert(fileNameFor(p.name)).second);
    const auto bytes = serialize(p);
    auto back = deserialize(bytes.data(), bytes.size());
    CHECK(back.ok());
    if (back.ok()) CHECK(serialize(back.value()) == bytes);  // exact round trip
    CHECK(bytes.size() < 2048);
  }
}

CHOP_TEST(file_names_avoid_windows_reserved_device_names) {
  CHECK(fileNameFor("CON") == "preset-con.cfpreset");
  CHECK(fileNameFor("lpt1") == "preset-lpt1.cfpreset");
  CHECK(fileNameFor("console") == "console.cfpreset");
  CHECK(fileNameFor("") == "untitled.cfpreset");
}

CHOP_TEST(validation_rejects_every_out_of_range_field) {
  auto bad = [](auto mutate) {
    Preset p;
    mutate(p);
    return !validate(p).ok();
  };
  CHECK(!bad([](Preset&) {}));
  CHECK(bad([](Preset& p) { p.name = ""; }));
  CHECK(bad([](Preset& p) { p.name = std::string(49, 'a'); }));
  CHECK(bad([](Preset& p) { p.name = "tab\there"; }));
  CHECK(bad([](Preset& p) { p.bars = 3; }));
  CHECK(bad([](Preset& p) { p.density = 1.01; }));
  CHECK(bad([](Preset& p) { p.swing = -0.1; }));
  CHECK(bad([](Preset& p) { p.variation = std::nan(""); }));
  CHECK(bad([](Preset& p) { p.chaos = 2; }));
  CHECK(bad([](Preset& p) { p.motif = "x"; }));
  CHECK(bad([](Preset& p) { p.motif = "...."; }));
  CHECK(bad([](Preset& p) { p.motif = "xxxxxxxxx"; }));
  CHECK(bad([](Preset& p) { p.motif = "xyz."; }));
  CHECK(bad([](Preset& p) { p.depth = 0; }));
  CHECK(bad([](Preset& p) { p.depth = 4; }));
  CHECK(bad([](Preset& p) { p.accentDecay = 0.0; }));
  CHECK(bad([](Preset& p) { p.evolveEvery = 3; }));
}

CHOP_TEST(decoder_survives_truncation_and_corruption) {
  const auto good = serialize(factoryPresets()[2]);
  for (std::size_t cut = 0; cut < good.size(); ++cut) CHECK(!deserialize(good.data(), cut).ok());
  for (std::size_t i = 0; i < good.size(); ++i) {
    auto bad = good;
    bad[i] ^= 0xFF;
    CHECK(!deserialize(bad.data(), bad.size()).ok());  // the checksum (or a field check) catches every single-byte change
  }
  auto longer = good;
  longer.push_back(0);
  CHECK(!deserialize(longer.data(), longer.size()).ok());
  auto future = good;
  future[4] = 9;  // version 9: unsupported, reported as such (checksum is checked after the version)
  auto r = deserialize(future.data(), future.size());
  CHECK(!r.ok() && r.error().code == ErrorCode::UnsupportedVersion);
  CHECK(!deserialize(nullptr, 0).ok());
  std::vector<std::uint8_t> huge(5000, 1);
  CHECK(!deserialize(huge.data(), huge.size()).ok());
}

CHOP_TEST(file_names_are_safe_and_stable) {
  CHECK(fileNameFor("My Preset #1") == "my_preset_1.cfpreset");
  CHECK(fileNameFor("../../etc/passwd") == "etc_passwd.cfpreset");
  CHECK(fileNameFor("") == "untitled.cfpreset");
  CHECK(fileNameFor("!!!").rfind("untitled-", 0) == 0);                       // lossy names keep a hash so they do not collide
  CHECK(fileNameFor("!!!") != fileNameFor("???"));
  CHECK(fileNameFor("\xE3\x82\xAD\xE3\x83\x83\xE3\x82\xAF") != fileNameFor("\xE3\x82\xB9\xE3\x83\x8D\xE3\x82\xA2"));
  CHECK(fileNameFor(std::string(100, 'Z')).size() == 48 + 9);
  CHECK(fileNameFor("Kick-01") == "kick-01.cfpreset");
}

namespace {
std::uint64_t fnv64(const std::vector<std::uint8_t>& d, std::uint64_t h = 1469598103934665603ull) {
  for (std::uint8_t b : d) h = (h ^ b) * 1099511628211ull;
  return h;
}
}  // namespace

CHOP_TEST(factory_presets_are_break_styles_with_engine_v2_profiles) {
  const auto& list = factoryPresets();
  std::set<int> profiles;
  std::set<std::string> categories;
  int stutterStyle = 0;
  for (const Preset& p : list) {
    categories.insert(p.category);
    int profile = -1;
    bool engineBreak = false, stutterOrRoll = false;
    for (const ParamValue& v : p.params) {
      if (v.id == "break_profile") profile = static_cast<int>(v.value);
      if (v.id == "engine_version") engineBreak = v.value == 1.0;
      if ((v.id == "allow_stutter" || v.id == "allow_roll") && v.value == 1.0) stutterOrRoll = true;
    }
    CHECK(profile >= 0 && profile <= 5 && engineBreak);
    profiles.insert(profile);
    CHECK(p.useSeed && p.seed >= 1101 && !p.description.empty() && p.description.size() <= kMaxDescription);
    if (p.category == "Stutter Edit") {
      ++stutterStyle;
      CHECK(stutterOrRoll && p.fxIntensity >= 0.8);
    }
  }
  CHECK_EQ(profiles.size(), 6u);        // every engine v2 profile has at least one preset
  CHECK_EQ(stutterStyle, 4);
  CHECK_EQ(categories.size(), factoryCategories().size());
  for (const std::string& c : factoryCategories()) CHECK(categories.count(c) == 1);
  // Rule templates ride along and survive a round trip.
  for (const Preset& p : list) {
    auto back = deserialize(serialize(p).data(), serialize(p).size());
    CHECK(back.ok() && back.value().rules.rules.size() == p.rules.rules.size());
  }
  CHECK(!list[0].rules.rules.empty() && list[6].rules.rules.empty());
}

CHOP_TEST(factory_list_is_pinned) {
  std::uint64_t h = 1469598103934665603ull;
  for (const Preset& p : factoryPresets()) h = fnv64(serialize(p), h);
  CHECK_EQ(h, 2848073482856316127ull);  // the list is the contract: changing a factory value means changing this constant
}

CHOP_TEST(version_1_files_stay_readable) {
  std::vector<std::uint8_t> out;
  bytes::Writer w(out);
  for (char c : {'C', 'F', 'P', 'R'}) w.u8(static_cast<std::uint8_t>(c));
  w.u32(1);
  w.str("Old One");
  w.u8(5);
  w.i32(2);
  for (double v : {0.4, 0.1, 0.3, 0.5, 0.0}) w.f64(v);
  w.str("x.xx");
  w.i32(2);
  w.f64(0.1);
  w.f64(0.85);
  w.i32(4);
  w.f64(0.2);
  std::uint32_t h = 2166136261u;
  for (std::uint8_t b : out) h = (h ^ b) * 16777619u;
  w.u32(h);
  auto r = deserialize(out.data(), out.size());
  CHECK(r.ok());
  if (!r.ok()) return;
  CHECK(r.value().name == "Old One" && r.value().allowReverse && r.value().allowRetrigger && !r.value().allowPitch);
  CHECK(r.value().category == "User" && r.value().params.empty() && !r.value().useSeed && r.value().gridDivision == 16 && r.value().rules.rules.empty());
}

CHOP_TEST(version_2_validation_and_hostile_files) {
  auto bad = [](auto mutate) {
    Preset p;
    mutate(p);
    return !validate(p).ok();
  };
  CHECK(bad([](Preset& p) { p.category = ""; }));
  CHECK(bad([](Preset& p) { p.description = std::string(121, 'a'); }));
  CHECK(bad([](Preset& p) { p.params.assign(33, ParamValue{"a", 1.0}); }));
  CHECK(bad([](Preset& p) { p.params = {{"density", 1.0}, {"density", 2.0}}; }));
  CHECK(bad([](Preset& p) { p.params = {{"", 1.0}}; }));
  CHECK(bad([](Preset& p) { p.params = {{"density", std::nan("")}}; }));
  CHECK(bad([](Preset& p) { p.seed = 1000000000000ull; }));
  CHECK(bad([](Preset& p) { p.gridDivision = 3; }));
  CHECK(bad([](Preset& p) { p.rules.rules.assign(65, roles::templates::preserveFirst(1, "kick")); }));
  const auto good = serialize(factoryPresets()[16]);
  for (std::size_t cut = 0; cut < good.size(); ++cut) CHECK(!deserialize(good.data(), cut).ok());
  for (std::size_t i = 0; i < good.size(); ++i) {
    auto b = good;
    b[i] ^= 0x5A;
    CHECK(!deserialize(b.data(), b.size()).ok());
  }
  std::vector<std::uint8_t> big(kMaxFileBytes + 1, 0);
  auto r = deserialize(big.data(), big.size());
  CHECK(!r.ok() && r.error().code == ErrorCode::LimitExceeded);
}
