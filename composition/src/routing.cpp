// Output routing: which chop or role plays on which aux output (docs/specs/roadmap/F11-multi-output-stems.md).
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <set>

namespace chopfractal::composition {

std::uint8_t ProjectSession::effectiveBus(ChopId chop) const {
  if (auto it = routing_.chopBus.find(chop.value); it != routing_.chopBus.end()) return it->second;
  const std::string role = roles_.roleOf(chop);
  if (!role.empty())
    if (auto it = routing_.roleBus.find(role); it != routing_.roleBus.end()) return it->second;
  return 0;
}

render::BusRouting ProjectSession::makeBusRouting() const {
  render::BusRouting r;
  if (!chops_) return r;
  for (const ChopInfo& c : chops_->chops) r.chopBus.emplace_back(c.id, effectiveBus(c.id));
  std::sort(r.chopBus.begin(), r.chopBus.end(), [](const auto& a, const auto& b) { return a.first.value < b.first.value; });
  return r;
}

Status ProjectSession::setOutputRouting(const OutputRouting& r) {
  for (const auto& kv : r.roleBus) {
    if (!roles::isValidRoleId(kv.first)) return makeError(ErrorCode::InvalidArgument, "Roles use lowercase letters, digits, '_' and '-' (1-32 chars).");
    if (kv.second > render::kMaxAuxBuses) return makeError(ErrorCode::OutOfRange, "outputs are Main and Out 2 to Out 9", render::kMaxAuxBuses);
  }
  for (const auto& kv : r.chopBus) {
    if (kv.second > render::kMaxAuxBuses) return makeError(ErrorCode::OutOfRange, "outputs are Main and Out 2 to Out 9", render::kMaxAuxBuses);
    if (!chopMap_ || !chopMap_->find(ChopId{kv.first})) return makeError(ErrorCode::NotFound, "unknown chop");
  }
  if (r.mode > RoutingMode::Custom) return makeError(ErrorCode::OutOfRange, "unknown routing mode");
  const OutputRouting previous = routing_;
  routing_ = r;
  Status st = republish();
  if (!st.ok()) {
    routing_ = previous;
    return st;
  }
  return {};
}

int ProjectSession::autoRouteByRole() {
  if (!chops_) return 0;
  OutputRouting r;
  r.mode = RoutingMode::ByRole;
  std::vector<std::string> order;
  for (const ChopInfo& c : chops_->chops) {
    const std::string role = roles_.roleOf(c.id);
    if (!role.empty() && std::find(order.begin(), order.end(), role) == order.end()) order.push_back(role);
  }
  int routed = 0;
  for (std::size_t i = 0; i < order.size() && i < static_cast<std::size_t>(render::kMaxAuxBuses); ++i) {
    r.roleBus[order[i]] = static_cast<std::uint8_t>(i + 1);
    ++routed;
  }
  if (order.size() > static_cast<std::size_t>(render::kMaxAuxBuses)) notice(Notice::Level::Warning, "More than 8 roles: the extra roles stay on Main.");
  if (routed > 0) notice(Notice::Level::Info, "Routed " + std::to_string(routed) + (routed == 1 ? " role to Out 2." : " roles to Out 2-" + std::to_string(routed + 1) + "."));
  if (!setOutputRouting(r).ok()) return 0;
  return routed;
}

int ProjectSession::autoRouteByChop() {
  if (!chops_) return 0;
  OutputRouting r;
  r.mode = RoutingMode::ByChop;
  int n = 0;
  for (const ChopInfo& c : chops_->chops) r.chopBus[c.id.value] = static_cast<std::uint8_t>(1 + (n++ % render::kMaxAuxBuses));
  if (n > render::kMaxAuxBuses) notice(Notice::Level::Info, "More than 8 chops: outputs are shared.");
  if (!setOutputRouting(r).ok()) return 0;
  return n;
}

}  // namespace chopfractal::composition
