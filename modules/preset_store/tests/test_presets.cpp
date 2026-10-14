#include <chopfractal/preset_store/presets.hpp>
#include <cmath>
#include <set>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::presets;

CHOP_TEST(factory_presets_are_valid_distinct_and_round_trip) {
  const auto& list = factoryPresets();
  CHECK_EQ(list.size(), 6u);
  std::set<std::string> names, files;
  for (const Preset& p : list) {
    CHECK(validate(p).ok());
    CHECK(names.insert(p.name).second && files.insert(fileNameFor(p.name)).second);
    const auto bytes = serialize(p);
    auto back = deserialize(bytes.data(), bytes.size());
    CHECK(back.ok());
    if (back.ok()) CHECK(serialize(back.value()) == bytes);  // exact round trip
    CHECK(bytes.size() < 512);
  }
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
  CHECK(fileNameFor("!!!") == "untitled.cfpreset");
  CHECK(fileNameFor(std::string(100, 'Z')).size() == 48 + 9);
  CHECK(fileNameFor("Kick-01") == "kick-01.cfpreset");
}
