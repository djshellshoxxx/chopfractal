#pragma once
// Wire format of the extras section of the "composition" module payload (F01 spec section 4.3). Private to the
// composition library: project_session.cpp and state_extras.cpp are the only users.
//
//   section := u8 0xE2, u16 blockCount (<= 32), blockCount x { u8 tag, u32 length, bytes[length] }
//
// Tags 1..8 and 13 carry their raw layout; tags >= 9 other than 13 carry `u16 schema` followed by the section
// bytes (see StateSection). Blocks are written in ascending tag order and only when they differ from the default.
#include <chopfractal/composition/project_session.hpp>

#include <array>
#include <map>
#include <vector>

namespace chopfractal::composition::statewire {

constexpr std::uint8_t kMarker = 0xE2;
constexpr std::uint8_t kTagSeed = 1, kTagFractal = 2, kTagRole = 3, kTagExport = 4, kTagDetect = 5, kTagGrid = 6, kTagUi = 7, kTagAB = 8;
constexpr std::size_t kMaxSlotBytes = 1u * 1024u * 1024u;  // one serialized A/B snapshot

using Blocks = std::map<std::uint8_t, std::vector<std::uint8_t>>;  // tag -> block payload

// True for the tags whose block starts with a u16 schema (feature sections).
inline bool isFeatureTag(std::uint8_t tag) { return tag >= kFirstFeatureTag && tag != kTagFractalTune; }
// Largest permitted block payload for the tag (the section cap plus the schema for feature tags).
std::size_t blockCap(std::uint8_t tag);

// Validates the framing only: marker, count, lengths inside the input, no duplicate tag, per-tag caps, 40 MiB total,
// no trailing bytes. `size` may be zero (no section).
Result<Blocks> parse(const std::uint8_t* data, std::size_t size);
void write(const Blocks& blocks, std::vector<std::uint8_t>& out);

// The blocks tags 1..7 and 13 for the extras that differ from the defaults.
void encodeExtras(const ProjectExtras& e, Blocks& out);
// Decodes tags 1..7 and 13 (absent = default). Individual out-of-range values fall back to the field default;
// a block with the wrong shape is Corrupt.
Result<ProjectExtras> decodeExtras(const Blocks& blocks);

// Tag 8: A/B snapshots as serialized patterns (empty vector = slot unused).
void encodeAB(const std::array<std::vector<std::uint8_t>, 2>& slots, Blocks& out);
Result<std::array<std::vector<std::uint8_t>, 2>> decodeAB(const std::vector<std::uint8_t>& block);

}  // namespace chopfractal::composition::statewire
