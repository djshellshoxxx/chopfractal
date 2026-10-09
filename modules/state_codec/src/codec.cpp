#include <chopfractal/state_codec/codec.hpp>

#include "bytes_detail.hpp"

namespace chopfractal::codec {
namespace {
constexpr char kMagic[4] = {'C', 'F', 'S', 'T'};
constexpr std::size_t kHeaderBytes = 4 + 4 + 4;  // magic + container version + module count
constexpr std::size_t kTrailerBytes = 4;         // crc32
}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

Result<std::vector<std::uint8_t>> encode(const ProjectState& state, const CodecLimits& limits) {
  if (state.modules.size() > limits.maxModules)
    return makeError(ErrorCode::LimitExceeded, "too many modules in project state", limits.maxModules);
  std::vector<std::uint8_t> out;
  detail::Writer w(out);
  w.raw(kMagic, 4);
  w.u32(state.projectVersion);
  w.u32(static_cast<std::uint32_t>(state.modules.size()));
  for (const auto& [id, payload] : state.modules) {
    if (id.empty() || id.size() > limits.maxModuleIdLength)
      return makeError(ErrorCode::InvalidArgument, "invalid module id: " + id);
    if (payload.bytes.size() > limits.maxPayloadBytes)
      return makeError(ErrorCode::LimitExceeded, "payload of module '" + id + "' exceeds the size cap",
                       static_cast<std::int64_t>(limits.maxPayloadBytes));
    w.str(id);
    w.u32(payload.schemaVersion);
    w.u32(static_cast<std::uint32_t>(payload.bytes.size()));
    w.raw(payload.bytes.data(), payload.bytes.size());
    if (out.size() + kTrailerBytes > limits.maxTotalBytes)
      return makeError(ErrorCode::LimitExceeded, "project state exceeds the total size cap",
                       static_cast<std::int64_t>(limits.maxTotalBytes));
  }
  w.u32(crc32(out.data(), out.size()));
  return out;
}

Result<ProjectState> decode(const std::uint8_t* data, std::size_t size, const CodecLimits& limits) {
  if (!data || size < kHeaderBytes + kTrailerBytes) return makeError(ErrorCode::Corrupt, "project state is truncated");
  if (size > limits.maxTotalBytes) return makeError(ErrorCode::LimitExceeded, "project state exceeds the total size cap");
  if (std::memcmp(data, kMagic, 4) != 0) return makeError(ErrorCode::Corrupt, "not a ChopFractal project state");

  detail::Reader trailer(data + size - kTrailerBytes, kTrailerBytes);
  if (trailer.u32() != crc32(data, size - kTrailerBytes)) return makeError(ErrorCode::Corrupt, "project state checksum mismatch");

  detail::Reader r(data + 4, size - 4 - kTrailerBytes);
  ProjectState state;
  state.projectVersion = r.u32();
  if (state.projectVersion == 0) return makeError(ErrorCode::Corrupt, "invalid project version");
  if (state.projectVersion > kContainerVersion)
    return makeError(ErrorCode::UnsupportedVersion, "project was saved by a newer version", state.projectVersion);
  const std::uint32_t count = r.count(limits.maxModules, 12);
  if (!r.ok()) return makeError(ErrorCode::Corrupt, "invalid module count");
  for (std::uint32_t i = 0; i < count; ++i) {
    std::string id = r.str(limits.maxModuleIdLength);
    ModulePayload payload;
    payload.schemaVersion = r.u32();
    const std::uint32_t len = r.u32();
    if (!r.ok() || id.empty() || len > limits.maxPayloadBytes || len > r.remaining())
      return makeError(ErrorCode::Corrupt, "malformed module entry");
    const std::uint8_t* p = r.take(len);
    if (len != 0 && !p) return makeError(ErrorCode::Corrupt, "malformed module payload");
    if (len != 0) payload.bytes.assign(p, p + len);
    if (!state.modules.emplace(std::move(id), std::move(payload)).second)
      return makeError(ErrorCode::Corrupt, "duplicate module entry");
  }
  if (r.remaining() != 0) return makeError(ErrorCode::Corrupt, "trailing bytes in project state");
  return state;
}

void MigrationRegistry::add(const std::string& moduleId, std::uint32_t fromVersion, MigrationStep step) {
  steps_[{moduleId, fromVersion}] = std::move(step);
}

Result<ModulePayload> MigrationRegistry::migrate(const std::string& moduleId, ModulePayload payload,
                                                 std::uint32_t targetVersion) const {
  if (payload.schemaVersion > targetVersion)
    return makeError(ErrorCode::UnsupportedVersion, "module '" + moduleId + "' state is newer than this build supports",
                     payload.schemaVersion);
  while (payload.schemaVersion < targetVersion) {
    auto it = steps_.find({moduleId, payload.schemaVersion});
    if (it == steps_.end())
      return makeError(ErrorCode::NotFound, "no migration for module '" + moduleId + "' from schema " +
                                                std::to_string(payload.schemaVersion));
    auto next = it->second(payload.bytes);
    if (!next.ok()) return next.error();
    payload.bytes = std::move(next.value());
    ++payload.schemaVersion;
  }
  return payload;
}

}  // namespace chopfractal::codec
