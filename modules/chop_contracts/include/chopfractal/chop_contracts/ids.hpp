#pragma once
// Stable identifier types. Rules: IDs are allocated by the module that owns the entity
// (monotonic counter or content hash), are never reused, 0 means "invalid", and UI element
// identifiers are never serialized as domain IDs.
#include <cstddef>
#include <cstdint>
#include <functional>

namespace chopfractal {

template <class Tag>
struct Id {
  std::uint64_t value = 0;
  constexpr Id() = default;
  constexpr explicit Id(std::uint64_t v) : value(v) {}
  constexpr bool valid() const { return value != 0; }
  friend constexpr bool operator==(Id a, Id b) { return a.value == b.value; }
  friend constexpr bool operator!=(Id a, Id b) { return a.value != b.value; }
  friend constexpr bool operator<(Id a, Id b) { return a.value < b.value; }
};

struct SourceTag {};
struct ChopTag {};
struct EventTag {};
struct ScopeTag {};

using SourceId = Id<SourceTag>;  // content hash of the source audio
using ChopId = Id<ChopTag>;      // owned by source_chop
using EventId = Id<EventTag>;    // owned by pattern_engine (top level) or derived for children
using ScopeId = Id<ScopeTag>;    // phrase / bar / beat scope nodes, owned by pattern_engine

// Derived IDs: nested child events get IDs computed from (parent id, child index) so they stay stable
// across reseeds. They always have the top bit set, so they can never collide with IDs handed out by the
// owning module's sequential counter, and validators must not compare them against that counter.
constexpr std::uint64_t kDerivedIdBit = 1ull << 63;
constexpr bool isDerivedId(std::uint64_t value) { return (value & kDerivedIdBit) != 0; }

}  // namespace chopfractal

namespace std {
template <class Tag>
struct hash<chopfractal::Id<Tag>> {
  size_t operator()(chopfractal::Id<Tag> id) const noexcept { return std::hash<std::uint64_t>()(id.value); }
};
}  // namespace std
