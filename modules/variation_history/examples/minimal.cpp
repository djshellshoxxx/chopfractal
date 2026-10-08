// Minimal consumer: record two snapshots of any serialized state and branch from the first.
#include <chopfractal/variation_history/history.hpp>
#include <cstdio>

int main() {
  using namespace chopfractal::history;
  VariationTree tree;
  Metadata m;
  m.label = "first";
  auto a = tree.addSnapshot(kNoNode, {1, 2, 3}, m);
  if (!a.ok()) return 1;
  auto b = tree.addSnapshot(a.value().id, {4, 5, 6}, m);
  auto c = tree.addSnapshot(a.value().id, {7, 8, 9}, m);  // a sibling branch
  if (!b.ok() || !c.ok() || tree.listChildren(a.value().id).size() != 2) return 1;
  std::printf("%zu snapshots\n", tree.size());
  return 0;
}
