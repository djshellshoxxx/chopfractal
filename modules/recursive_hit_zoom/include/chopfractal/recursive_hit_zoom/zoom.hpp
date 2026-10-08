#pragma once
// Recursive Hit Zoom: creates a nested child pattern inside one event's exact time window and source
// bounds. Consumes and emits chop_contracts data only; it does not schedule audio, draw UI, or own
// sample memory. Threading: pure functions, safe from any non-real-time thread.
#include <chopfractal/chop_contracts/chop.hpp>
#include <chopfractal/chop_contracts/event.hpp>
#include <chopfractal/chop_contracts/limits.hpp>
#include <chopfractal/chop_contracts/result.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace chopfractal::zoom {

// Part of the determinism contract: bump when output for identical input would change.
constexpr std::uint32_t kModuleVersion = 1;
constexpr std::uint32_t kSchemaVersion = 1;
constexpr Ticks kMinChildTicks = 15;

enum class Density : std::uint8_t { Sparse, Balanced, Dense };
enum class Operation : std::uint8_t { RepeatParent, SelectSubregion, AlternateChops, CreateRests };
enum class Inherit : std::uint8_t { Inherit, Override, Reset };

enum OverrideField : std::uint8_t { kLevel = 1, kPan = 2, kPitch = 4, kReverse = 8, kRetrigger = 16 };

struct Settings {
  int subdivisions = 4;  // 2, 3, 4, 5, 6 or 8
  Density density = Density::Balanced;
  double densityValue = -1.0;  // advanced numeric control in [0, 1]; negative = use the preset
  Operation operation = Operation::RepeatParent;
  Ticks shuffleTicks = 0;  // bounded local timing movement; never leaves the parent window
  Inherit inherit = Inherit::Inherit;
  EventTransform overrideTx;      // used with Inherit::Override
  std::uint8_t overrideMask = 0;  // OverrideField bits taken from overrideTx
  int maxDepth = limits::kDefaultNestedDepth;  // 1..kMaxNestedDepth
};

struct Context {
  SampleRange sourceBounds;          // the parent's resolved source range (children never read outside it)
  std::vector<ChopInfo> compatible;  // chops allowed for AlternateChops (explicit source replacement)
  int parentDepth = 0;               // 0 for a top-level event
  // How many events this child tree may add: project cap minus the flattened events elsewhere,
  // counting the parent itself as replaced. Exceeding it is an error, never a truncation.
  std::size_t eventBudget = limits::kMaxEventsPerPattern;
};

// Child timing is relative to the parent window and always spans exactly parent.duration.
Result<std::shared_ptr<const NestedPattern>> createChildPattern(const Event& parent, const Context& ctx,
                                                                const Settings& settings, std::uint64_t seed);

// Regenerates children with a new seed. Locked child events are kept in place; a locked child pattern
// refuses with Blocked.
Result<std::shared_ptr<const NestedPattern>> mutateChildren(const Event& parentWithChild, const Context& ctx,
                                                            const Settings& settings, std::uint64_t seed);

// The original single event, with the child tree removed. Never alters the input.
Event collapse(const Event& parent);

// Flattens one event subtree to absolute-position events (windowStart = the container's start tick).
// For projects that have no nested pattern model of their own.
Result<FlatEventList> flatten(const Event& parent, Ticks windowStart, const ChopSnapshot& chops,
                              const FlattenLimits& limits = {}, std::uint64_t probabilitySeed = 0);

// Module-owned versioned state for a child tree.
std::vector<std::uint8_t> serialize(const NestedPattern& child);
Result<std::shared_ptr<const NestedPattern>> deserialize(const std::uint8_t* data, std::size_t size);

// Nearest permitted subdivision not exceeding `budget`, or 0 if even the smallest does not fit.
int nearestPermittedSubdivision(std::size_t budget);

}  // namespace chopfractal::zoom
