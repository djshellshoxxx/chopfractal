#pragma once
// Generic immutable snapshot branch graph. Stores opaque payload bytes plus metadata; knows nothing
// about audio, UI, hosts, or ChopFractal types. Not thread-safe: own it from one thread.
#include <chopfractal/variation_history/result.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace chopfractal::history {

using NodeId = std::uint64_t;
constexpr NodeId kNoNode = 0;
constexpr std::uint32_t kSchemaVersion = 1;
constexpr std::size_t kHardMaxNodes = 128;

struct Config {
  std::size_t maxNodes = 64;               // default cap; configurable up to kHardMaxNodes
  std::size_t maxPayloadBytes = 1u << 20;  // per snapshot
};

struct Metadata {
  std::uint64_t seed = 0;
  std::string label;
  std::uint32_t payloadSchema = 0;  // schema version of the payload as written by its serializer
  bool favorite = false;
  std::vector<std::string> tags;
  std::int64_t wallClock = 0;  // display only; never used for ordering
};

struct Snapshot {
  NodeId id = kNoNode;
  NodeId parent = kNoNode;
  std::shared_ptr<const std::vector<std::uint8_t>> payload;
  std::uint64_t payloadHash = 0;
  Metadata meta;
  std::uint64_t order = 0;  // creation ordering value
};

struct NodeSummary {
  NodeId id = kNoNode;
  NodeId parent = kNoNode;
  std::string label;
  std::uint64_t seed = 0;
  bool favorite = false;
  std::uint64_t order = 0;
  std::size_t payloadBytes = 0;
  std::size_t childCount = 0;
};

struct AddResult {
  NodeId id = kNoNode;
  std::vector<NodeId> pruned;  // nodes removed to make room; the caller must tell the user
};

enum class DeletePolicy { RefuseProtected, IncludeFavorites };

class VariationTree {
 public:
  explicit VariationTree(Config config = {});

  // parent == kNoNode creates a root. Prunes the oldest unprotected leaf when full; refuses with
  // LimitExceeded when no safe prune candidate exists.
  Result<AddResult> addSnapshot(NodeId parent, std::vector<std::uint8_t> payload, Metadata meta);
  // Verifies the payload hash; a mismatch returns Corrupt with `hint` = the affected node id.
  Result<Snapshot> getSnapshot(NodeId id) const;
  std::vector<NodeSummary> listChildren(NodeId parent) const;  // kNoNode lists roots
  std::vector<NodeSummary> listAll() const;                    // ordered by creation
  std::vector<NodeId> ancestors(NodeId id) const;              // root first, excluding `id`
  bool contains(NodeId id) const { return nodes_.count(id) != 0; }
  std::size_t size() const { return nodes_.size(); }

  Status setActive(NodeId id);
  NodeId active() const { return active_; }
  Status rename(NodeId id, std::string label);
  Status setFavorite(NodeId id, bool favorite);

  // Removes a node and all descendants. Never removes the active node or (with RefuseProtected)
  // favorites; returns the removed ids.
  Result<std::vector<NodeId>> deleteBranch(NodeId id, DeletePolicy policy = DeletePolicy::RefuseProtected);

  using DiffFn = std::function<std::string(const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&)>;
  Result<std::string> compare(NodeId a, NodeId b, const DiffFn& diff) const;

  std::vector<std::uint8_t> serialize() const;
  static Result<VariationTree> deserialize(const std::uint8_t* data, std::size_t size);

  const Config& config() const { return config_; }

 private:
  struct Node {
    NodeId parent = kNoNode;
    std::shared_ptr<const std::vector<std::uint8_t>> payload;
    std::uint64_t hash = 0;
    Metadata meta;
    std::uint64_t order = 0;
  };
  bool isProtected(NodeId id) const;
  std::size_t childCount(NodeId id) const;
  void collectSubtree(NodeId id, std::vector<NodeId>& out) const;

  Config config_;
  std::map<NodeId, Node> nodes_;
  NodeId nextId_ = 1;
  std::uint64_t nextOrder_ = 1;
  NodeId active_ = kNoNode;
};

std::uint64_t hashPayload(const std::vector<std::uint8_t>& payload);  // FNV-1a 64

}  // namespace chopfractal::history
