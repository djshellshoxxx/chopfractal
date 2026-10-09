#include <chopfractal/state_codec/codec.hpp>

#include "chop_test.hpp"

using namespace chopfractal::codec;

namespace {
ProjectState sample() {
  ProjectState s;
  s.modules["source_chop"] = {1, {1, 2, 3}};
  s.modules["pattern_engine"] = {2, {9, 8, 7, 6}};
  s.modules["empty"] = {1, {}};
  return s;
}
}  // namespace

CHOP_TEST(roundtrip_preserves_modules_and_versions) {
  auto enc = encode(sample());
  CHECK(enc.ok());
  auto dec = decode(enc.value().data(), enc.value().size());
  CHECK(dec.ok());
  CHECK_EQ(dec.value().modules.size(), 3u);
  CHECK_EQ(dec.value().modules.at("pattern_engine").schemaVersion, 2u);
  CHECK(dec.value().modules.at("pattern_engine").bytes == (std::vector<std::uint8_t>{9, 8, 7, 6}));
  CHECK(dec.value().modules.at("empty").bytes.empty());
}

CHOP_TEST(any_single_bit_flip_or_truncation_is_rejected) {
  auto enc = encode(sample());
  CHECK(enc.ok());
  const auto good = enc.value();
  for (std::size_t i = 0; i < good.size(); ++i) {
    auto bad = good;
    bad[i] ^= 0x10;
    CHECK(!decode(bad.data(), bad.size()).ok());
  }
  for (std::size_t cut = 0; cut < good.size(); ++cut) CHECK(!decode(good.data(), cut).ok());
  CHECK(!decode(nullptr, 0).ok());
}

CHOP_TEST(newer_project_version_is_rejected_safely) {
  ProjectState s = sample();
  s.projectVersion = kContainerVersion + 1;
  auto enc = encode(s);
  CHECK(enc.ok());
  auto dec = decode(enc.value().data(), enc.value().size());
  CHECK(!dec.ok() && dec.error().code == ErrorCode::UnsupportedVersion);
}

CHOP_TEST(size_limits_apply_to_encode_and_decode) {
  CodecLimits lim;
  lim.maxPayloadBytes = 3;
  auto enc = encode(sample(), lim);
  CHECK(!enc.ok() && enc.error().code == ErrorCode::LimitExceeded);

  auto good = encode(sample());
  CHECK(good.ok());
  CodecLimits tiny;
  tiny.maxTotalBytes = 16;
  CHECK(!decode(good.value().data(), good.value().size(), tiny).ok());

  ProjectState many;
  for (int i = 0; i < 40; ++i) many.modules["m" + std::to_string(i)] = {1, {}};
  CHECK(!encode(many).ok());  // default cap is 32 modules
  ProjectState badId;
  badId.modules[""] = {1, {}};
  CHECK(!encode(badId).ok());
}

CHOP_TEST(migrations_chain_in_order_and_reject_newer_or_missing) {
  MigrationRegistry reg;
  reg.add("m", 1, [](const std::vector<std::uint8_t>& in) -> Result<std::vector<std::uint8_t>> {
    auto out = in;
    out.push_back(0xA1);
    return out;
  });
  reg.add("m", 2, [](const std::vector<std::uint8_t>& in) -> Result<std::vector<std::uint8_t>> {
    auto out = in;
    out.push_back(0xA2);
    return out;
  });
  auto up = reg.migrate("m", {1, {5}}, 3);
  CHECK(up.ok());
  CHECK_EQ(up.value().schemaVersion, 3u);
  CHECK(up.value().bytes == (std::vector<std::uint8_t>{5, 0xA1, 0xA2}));
  CHECK(reg.migrate("m", {3, {}}, 3).ok());  // already current
  auto newer = reg.migrate("m", {4, {}}, 3);
  CHECK(!newer.ok() && newer.error().code == ErrorCode::UnsupportedVersion);
  auto missing = reg.migrate("other", {1, {}}, 2);
  CHECK(!missing.ok() && missing.error().code == ErrorCode::NotFound);

  reg.add("bad", 1, [](const std::vector<std::uint8_t>&) -> Result<std::vector<std::uint8_t>> {
    return makeError(ErrorCode::Corrupt, "nope");
  });
  CHECK(!reg.migrate("bad", {1, {}}, 2).ok());
}
