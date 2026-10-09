#include <chopfractal/variation_history/history.hpp>

#include <algorithm>

#include "bytes_detail.hpp"

namespace chopfractal::history {
namespace {
constexpr char kMagic[4] = {'C', 'F', 'V', 'H'};
constexpr std::size_t kMaxLabel = 256;
constexpr std::size_t kMaxTag = 64;
constexpr std::uint32_t kMaxTags = 16;
constexpr std::size_t kAbsolutePayloadCap = 16u << 20;

std::string idText(NodeId id) { return std::to_string(id); }
}  // namespace

std::uint64_t hashPayload(const std::vector<std::uint8_t>& payload) {
  std::uint64_t h = 0xCBF29CE484222325ull;
  for (std::uint8_t b : payload) {
    h ^= b;
    h *= 0x100000001B3ull;
  }
  return h;
}

VariationTree::VariationTree(Config config) : config_(config) {
  if (config_.maxNodes < 1) config_.maxNodes = 1;
  if (config_.maxNodes > kHardMaxNodes) config_.maxNodes = kHardMaxNodes;
}

std::size_t VariationTree::childCount(NodeId id) const {
  std::size_t n = 0;
  for (const auto& kv : nodes_)
    if (kv.second.parent == id) ++n;
  return n;
}

bool VariationTree::isProtected(NodeId id) const {
  if (id == active_) return true;
  auto it = nodes_.find(id);
  if (it != nodes_.end() && it->second.meta.favorite) return true;
  // Required ancestor chain of the active node.
  for (NodeId a = active_; a != kNoNode;) {
    auto n = nodes_.find(a);
    if (n == nodes_.end()) break;
    a = n->second.parent;
    if (a == id) return true;
  }
  return false;
}

Result<AddResult> VariationTree::addSnapshot(NodeId parent, std::vector<std::uint8_t> payload, Metadata meta) {
  if (parent != kNoNode && !contains(parent)) return makeError(ErrorCode::NotFound, "parent node " + idText(parent) + " does not exist");
  if (payload.size() > config_.maxPayloadBytes)
    return makeError(ErrorCode::LimitExceeded, "snapshot payload is too large", static_cast<std::int64_t>(config_.maxPayloadBytes));
  if (meta.label.size() > kMaxLabel || meta.tags.size() > kMaxTags)
    return makeError(ErrorCode::InvalidArgument, "label or tags exceed limits");
  for (const auto& t : meta.tags)
    if (t.size() > kMaxTag) return makeError(ErrorCode::InvalidArgument, "tag too long");

  AddResult result;
  if (nodes_.size() >= config_.maxNodes) {
    // Prune the oldest unprotected leaf; the parent of the new node is never a candidate.
    NodeId victim = kNoNode;
    std::uint64_t victimOrder = ~0ull;
    for (const auto& [id, node] : nodes_) {
      if (id == parent || isProtected(id) || childCount(id) != 0) continue;
      if (node.order < victimOrder) {
        victimOrder = node.order;
        victim = id;
      }
    }
    if (victim == kNoNode)
      return makeError(ErrorCode::LimitExceeded, "history is full and every snapshot is protected", static_cast<std::int64_t>(config_.maxNodes));
    nodes_.erase(victim);
    result.pruned.push_back(victim);
  }

  Node node;
  node.parent = parent;
  node.hash = hashPayload(payload);
  node.payload = std::make_shared<const std::vector<std::uint8_t>>(std::move(payload));
  node.meta = std::move(meta);
  node.order = nextOrder_++;
  result.id = nextId_++;
  nodes_.emplace(result.id, std::move(node));
  return result;
}

Result<Snapshot> VariationTree::getSnapshot(NodeId id) const {
  auto it = nodes_.find(id);
  if (it == nodes_.end()) return makeError(ErrorCode::NotFound, "snapshot " + idText(id) + " does not exist", static_cast<std::int64_t>(id));
  const Node& n = it->second;
  if (hashPayload(*n.payload) != n.hash)
    return makeError(ErrorCode::Corrupt, "snapshot " + idText(id) + " failed its integrity check", static_cast<std::int64_t>(id));
  Snapshot s;
  s.id = id;
  s.parent = n.parent;
  s.payload = n.payload;
  s.payloadHash = n.hash;
  s.meta = n.meta;
  s.order = n.order;
  return s;
}

std::vector<NodeSummary> VariationTree::listChildren(NodeId parent) const {
  std::vector<NodeSummary> out;
  for (const auto& [id, n] : nodes_) {
    if (n.parent != parent) continue;
    out.push_back({id, n.parent, n.meta.label, n.meta.seed, n.meta.favorite, n.order, n.payload->size(), childCount(id)});
  }
  std::sort(out.begin(), out.end(), [](const NodeSummary& a, const NodeSummary& b) { return a.order < b.order; });
  return out;
}

std::vector<NodeSummary> VariationTree::listAll() const {
  std::vector<NodeSummary> out;
  for (const auto& [id, n] : nodes_)
    out.push_back({id, n.parent, n.meta.label, n.meta.seed, n.meta.favorite, n.order, n.payload->size(), childCount(id)});
  std::sort(out.begin(), out.end(), [](const NodeSummary& a, const NodeSummary& b) { return a.order < b.order; });
  return out;
}

std::vector<NodeId> VariationTree::ancestors(NodeId id) const {
  std::vector<NodeId> chain;
  auto it = nodes_.find(id);
  while (it != nodes_.end() && it->second.parent != kNoNode) {
    chain.push_back(it->second.parent);
    it = nodes_.find(it->second.parent);
  }
  std::reverse(chain.begin(), chain.end());
  return chain;
}

Status VariationTree::setActive(NodeId id) {
  if (id != kNoNode && !contains(id)) return makeError(ErrorCode::NotFound, "snapshot " + idText(id) + " does not exist");
  active_ = id;
  return {};
}

Status VariationTree::rename(NodeId id, std::string label) {
  auto it = nodes_.find(id);
  if (it == nodes_.end()) return makeError(ErrorCode::NotFound, "snapshot " + idText(id) + " does not exist");
  if (label.size() > kMaxLabel) return makeError(ErrorCode::InvalidArgument, "label too long");
  it->second.meta.label = std::move(label);
  return {};
}

Status VariationTree::setFavorite(NodeId id, bool favorite) {
  auto it = nodes_.find(id);
  if (it == nodes_.end()) return makeError(ErrorCode::NotFound, "snapshot " + idText(id) + " does not exist");
  it->second.meta.favorite = favorite;
  return {};
}

void VariationTree::collectSubtree(NodeId id, std::vector<NodeId>& out) const {
  out.push_back(id);
  for (const auto& [cid, n] : nodes_)
    if (n.parent == id) collectSubtree(cid, out);
}

Result<std::vector<NodeId>> VariationTree::deleteBranch(NodeId id, DeletePolicy policy) {
  if (!contains(id)) return makeError(ErrorCode::NotFound, "snapshot " + idText(id) + " does not exist");
  std::vector<NodeId> doomed;
  collectSubtree(id, doomed);
  for (NodeId d : doomed) {
    if (d == active_) return makeError(ErrorCode::Blocked, "the branch contains the active snapshot", static_cast<std::int64_t>(d));
    if (policy == DeletePolicy::RefuseProtected && nodes_.at(d).meta.favorite)
      return makeError(ErrorCode::Blocked, "the branch contains a favorite", static_cast<std::int64_t>(d));
  }
  for (NodeId d : doomed) nodes_.erase(d);
  return doomed;
}

Result<std::string> VariationTree::compare(NodeId a, NodeId b, const DiffFn& diff) const {
  if (!diff) return makeError(ErrorCode::InvalidArgument, "compare needs a caller-supplied diff strategy");
  auto sa = getSnapshot(a);
  if (!sa.ok()) return sa.error();
  auto sb = getSnapshot(b);
  if (!sb.ok()) return sb.error();
  return diff(*sa.value().payload, *sb.value().payload);
}

std::vector<std::uint8_t> VariationTree::serialize() const {
  std::vector<std::uint8_t> out;
  detail::Writer w(out);
  w.raw(kMagic, 4);
  w.u32(kSchemaVersion);
  w.u32(static_cast<std::uint32_t>(config_.maxNodes));
  w.u32(static_cast<std::uint32_t>(config_.maxPayloadBytes));
  w.u64(nextId_);
  w.u64(nextOrder_);
  w.u64(active_);
  w.u32(static_cast<std::uint32_t>(nodes_.size()));
  for (const auto& [id, n] : nodes_) {
    w.u64(id);
    w.u64(n.parent);
    w.u64(n.order);
    w.u64(n.hash);
    w.u32(n.meta.payloadSchema);
    w.u64(n.meta.seed);
    w.boolean(n.meta.favorite);
    w.i64(n.meta.wallClock);
    w.str(n.meta.label);
    w.u32(static_cast<std::uint32_t>(n.meta.tags.size()));
    for (const auto& t : n.meta.tags) w.str(t);
    w.u32(static_cast<std::uint32_t>(n.payload->size()));
    w.raw(n.payload->data(), n.payload->size());
  }
  return out;
}

Result<VariationTree> VariationTree::deserialize(const std::uint8_t* data, std::size_t size) {
  if (!data || size < 8 || std::memcmp(data, kMagic, 4) != 0) return makeError(ErrorCode::Corrupt, "not a variation history");
  detail::Reader r(data + 4, size - 4);
  const std::uint32_t schema = r.u32();
  if (schema == 0) return makeError(ErrorCode::Corrupt, "invalid history schema");
  if (schema > kSchemaVersion) return makeError(ErrorCode::UnsupportedVersion, "history was saved by a newer version", schema);
  Config cfg;
  cfg.maxNodes = r.u32();
  cfg.maxPayloadBytes = r.u32();
  if (!r.ok() || cfg.maxNodes < 1 || cfg.maxNodes > kHardMaxNodes || cfg.maxPayloadBytes > kAbsolutePayloadCap)
    return makeError(ErrorCode::Corrupt, "invalid history configuration");
  VariationTree tree(cfg);
  tree.nextId_ = r.u64();
  tree.nextOrder_ = r.u64();
  tree.active_ = r.u64();
  const std::uint32_t count = r.count(static_cast<std::uint32_t>(cfg.maxNodes), 60);
  if (!r.ok()) return makeError(ErrorCode::Corrupt, "invalid node count");
  for (std::uint32_t i = 0; i < count; ++i) {
    const NodeId id = r.u64();
    Node n;
    n.parent = r.u64();
    n.order = r.u64();
    n.hash = r.u64();
    n.meta.payloadSchema = r.u32();
    n.meta.seed = r.u64();
    n.meta.favorite = r.boolean();
    n.meta.wallClock = r.i64();
    n.meta.label = r.str(kMaxLabel);
    const std::uint32_t tagCount = r.count(kMaxTags, 4);
    for (std::uint32_t t = 0; t < tagCount; ++t) n.meta.tags.push_back(r.str(kMaxTag));
    const std::uint32_t len = r.u32();
    if (!r.ok() || len > cfg.maxPayloadBytes || len > r.remaining()) return makeError(ErrorCode::Corrupt, "malformed history node");
    const std::uint8_t* p = r.take(len);
    if (len != 0 && !p) return makeError(ErrorCode::Corrupt, "malformed history payload");
    n.payload = std::make_shared<const std::vector<std::uint8_t>>(len ? std::vector<std::uint8_t>(p, p + len) : std::vector<std::uint8_t>());
    if (id == kNoNode || id >= tree.nextId_ || n.order == 0 || n.order >= tree.nextOrder_)
      return makeError(ErrorCode::Corrupt, "invalid node identity");
    if (!tree.nodes_.emplace(id, std::move(n)).second) return makeError(ErrorCode::Corrupt, "duplicate node id");
  }
  if (!r.ok() || r.remaining() != 0) return makeError(ErrorCode::Corrupt, "malformed history stream");
  // Structural validation: parents exist and precede their children (guarantees acyclic links).
  for (const auto& [id, n] : tree.nodes_) {
    if (n.parent == kNoNode) continue;
    auto p = tree.nodes_.find(n.parent);
    if (p == tree.nodes_.end() || p->second.order >= n.order) return makeError(ErrorCode::Corrupt, "node " + idText(id) + " has an invalid parent");
  }
  if (tree.active_ != kNoNode && !tree.contains(tree.active_)) return makeError(ErrorCode::Corrupt, "active node is missing");
  return tree;
}

}  // namespace chopfractal::history
