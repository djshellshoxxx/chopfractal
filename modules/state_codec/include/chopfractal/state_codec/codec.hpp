#pragma once
// Versioned project-state container. Each stateful module serializes its own payload; this module only
// composes payloads under module ID + schema version, protects them with a checksum, validates every
// size before allocating, and chains registered migrations. It has no dependencies.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <chopfractal/state_codec/result.hpp>

namespace chopfractal::codec {

constexpr std::uint32_t kContainerVersion = 1;

struct ModulePayload {
  std::uint32_t schemaVersion = 0;
  std::vector<std::uint8_t> bytes;
};

struct ProjectState {
  std::uint32_t projectVersion = kContainerVersion;
  std::map<std::string, ModulePayload> modules;  // keyed by module id
};

// Defaults are implementation constants pending spec review (maximum serialized source size is open).
struct CodecLimits {
  std::size_t maxTotalBytes = 64u * 1024u * 1024u;
  std::size_t maxPayloadBytes = 48u * 1024u * 1024u;
  std::uint32_t maxModules = 32;
  std::size_t maxModuleIdLength = 64;
};

Result<std::vector<std::uint8_t>> encode(const ProjectState& state, const CodecLimits& limits = {});
Result<ProjectState> decode(const std::uint8_t* data, std::size_t size, const CodecLimits& limits = {});

std::uint32_t crc32(const std::uint8_t* data, std::size_t size);

// Migration steps upgrade a payload by exactly one schema version.
using MigrationStep = std::function<Result<std::vector<std::uint8_t>>(const std::vector<std::uint8_t>&)>;

class MigrationRegistry {
 public:
  void add(const std::string& moduleId, std::uint32_t fromVersion, MigrationStep step);
  // Newer-than-target payloads are rejected (UnsupportedVersion); a missing step is NotFound.
  Result<ModulePayload> migrate(const std::string& moduleId, ModulePayload payload, std::uint32_t targetVersion) const;

 private:
  std::map<std::pair<std::string, std::uint32_t>, MigrationStep> steps_;
};

}  // namespace chopfractal::codec
