#include <chopfractal/variation_history/history.hpp>

#include "chop_test.hpp"

using namespace chopfractal::history;

namespace {
std::vector<std::uint8_t> bytesOf(std::uint8_t v) { return std::vector<std::uint8_t>(8, v); }
Metadata meta(std::uint64_t seed, const char* label = "") {
  Metadata m;
  m.seed = seed;
  m.label = label;
  m.payloadSchema = 3;
  return m;
}
}  // namespace

CHOP_TEST(nodes_restore_exact_payload_seed_and_schema) {
  VariationTree t;
  auto a = t.addSnapshot(kNoNode, bytesOf(1), meta(11, "root"));
  CHECK(a.ok());
  auto s = t.getSnapshot(a.value().id);
  CHECK(s.ok());
  CHECK(*s.value().payload == bytesOf(1));
  CHECK_EQ(s.value().meta.seed, 11u);
  CHECK_EQ(s.value().meta.payloadSchema, 3u);
  CHECK(s.value().meta.label == "root");
}

CHOP_TEST(branching_leaves_siblings_unchanged) {
  VariationTree t;
  NodeId root = t.addSnapshot(kNoNode, bytesOf(1), meta(1)).value().id;
  NodeId a = t.addSnapshot(root, bytesOf(2), meta(2)).value().id;
  NodeId b = t.addSnapshot(root, bytesOf(3), meta(3)).value().id;
  NodeId c = t.addSnapshot(a, bytesOf(4), meta(4)).value().id;
  CHECK_EQ(t.listChildren(root).size(), 2u);
  CHECK_EQ(t.listChildren(a).size(), 1u);
  CHECK(*t.getSnapshot(b).value().payload == bytesOf(3));
  CHECK(t.ancestors(c) == (std::vector<NodeId>{root, a}));
  CHECK(!t.addSnapshot(999, bytesOf(1), meta(0)).ok());
}

CHOP_TEST(prune_removes_oldest_unprotected_leaf_and_reports_it) {
  Config cfg;
  cfg.maxNodes = 4;
  VariationTree t(cfg);
  NodeId root = t.addSnapshot(kNoNode, bytesOf(0), meta(0)).value().id;
  NodeId a = t.addSnapshot(root, bytesOf(1), meta(1)).value().id;
  NodeId b = t.addSnapshot(root, bytesOf(2), meta(2)).value().id;
  NodeId c = t.addSnapshot(a, bytesOf(3), meta(3)).value().id;
  CHECK(t.setActive(c).ok());
  CHECK(t.setFavorite(b, true).ok());
  // Leaves: b (favorite), c (active). Nothing is safe to prune.
  auto full = t.addSnapshot(c, bytesOf(9), meta(9));
  CHECK(!full.ok() && full.error().code == ErrorCode::LimitExceeded);
  CHECK(t.setFavorite(b, false).ok());
  auto ok = t.addSnapshot(c, bytesOf(9), meta(9));
  CHECK(ok.ok());
  CHECK(ok.value().pruned == (std::vector<NodeId>{b}));
  CHECK(t.contains(root) && t.contains(a) && t.contains(c) && !t.contains(b));
  CHECK_EQ(t.size(), 4u);
}

CHOP_TEST(delete_branch_refuses_active_and_favorites) {
  VariationTree t;
  NodeId root = t.addSnapshot(kNoNode, bytesOf(0), meta(0)).value().id;
  NodeId a = t.addSnapshot(root, bytesOf(1), meta(1)).value().id;
  NodeId a2 = t.addSnapshot(a, bytesOf(2), meta(2)).value().id;
  NodeId b = t.addSnapshot(root, bytesOf(3), meta(3)).value().id;
  CHECK(t.setActive(a2).ok());
  auto blocked = t.deleteBranch(a);
  CHECK(!blocked.ok() && blocked.error().code == ErrorCode::Blocked);
  CHECK(t.setFavorite(b, true).ok());
  CHECK(!t.deleteBranch(b).ok());
  auto forced = t.deleteBranch(b, DeletePolicy::IncludeFavorites);
  CHECK(forced.ok() && forced.value() == (std::vector<NodeId>{b}));
  CHECK(t.setActive(root).ok());
  auto gone = t.deleteBranch(a);
  CHECK(gone.ok() && gone.value().size() == 2);
  CHECK_EQ(t.size(), 1u);
}

CHOP_TEST(serialization_roundtrips_tree_links_and_metadata) {
  VariationTree t;
  NodeId root = t.addSnapshot(kNoNode, bytesOf(1), meta(1, "root")).value().id;
  Metadata m = meta(2, "child");
  m.favorite = true;
  m.tags = {"fav", "drums"};
  m.wallClock = 1234;
  NodeId a = t.addSnapshot(root, bytesOf(2), m).value().id;
  CHECK(t.setActive(a).ok());
  const auto blob = t.serialize();
  auto back = VariationTree::deserialize(blob.data(), blob.size());
  CHECK(back.ok());
  CHECK_EQ(back.value().size(), 2u);
  CHECK_EQ(back.value().active(), a);
  auto s = back.value().getSnapshot(a);
  CHECK(s.ok() && s.value().parent == root && s.value().meta.favorite && s.value().meta.tags.size() == 2);
  CHECK_EQ(s.value().meta.wallClock, 1234);
  // New ids continue after the restored ones.
  auto next = back.value().addSnapshot(a, bytesOf(5), meta(5));
  CHECK(next.ok() && next.value().id > a);
}

CHOP_TEST(corrupt_payload_is_isolated_to_its_node_and_garbage_is_rejected) {
  VariationTree t;
  NodeId a = t.addSnapshot(kNoNode, bytesOf(0x11), meta(1)).value().id;
  NodeId b = t.addSnapshot(a, bytesOf(0x22), meta(2)).value().id;
  auto blob = t.serialize();
  // Flip one byte inside node b's payload (the last 8 bytes of the stream).
  blob[blob.size() - 1] ^= 0xFF;
  auto back = VariationTree::deserialize(blob.data(), blob.size());
  CHECK(back.ok());
  CHECK(back.value().getSnapshot(a).ok());
  auto bad = back.value().getSnapshot(b);
  CHECK(!bad.ok() && bad.error().code == ErrorCode::Corrupt && bad.error().hint == static_cast<std::int64_t>(b));

  const auto good = t.serialize();
  for (std::size_t cut = 0; cut < good.size(); ++cut) CHECK(!VariationTree::deserialize(good.data(), cut).ok());
  CHECK(!VariationTree::deserialize(nullptr, 0).ok());
}

CHOP_TEST(compare_requires_a_diff_strategy_and_limits_are_enforced) {
  VariationTree t;
  NodeId a = t.addSnapshot(kNoNode, bytesOf(1), meta(1)).value().id;
  NodeId b = t.addSnapshot(a, bytesOf(2), meta(2)).value().id;
  CHECK(!t.compare(a, b, nullptr).ok());
  auto d = t.compare(a, b, [](const auto& x, const auto& y) { return std::to_string(x[0]) + "->" + std::to_string(y[0]); });
  CHECK(d.ok() && d.value() == "1->2");
  Config cfg;
  cfg.maxPayloadBytes = 4;
  VariationTree small(cfg);
  CHECK(!small.addSnapshot(kNoNode, bytesOf(1), meta(1)).ok());
  Config huge;
  huge.maxNodes = 100000;
  CHECK_EQ(VariationTree(huge).config().maxNodes, kHardMaxNodes);
}
