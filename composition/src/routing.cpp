// Multiple outputs (F11): which output each chop plays on. The routing is project state (section tag 9) that is baked
// into every published Playback, so a change reaches the audio thread through the same mailbox as the pattern.
#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace chopfractal::composition {

namespace {
constexpr std::uint16_t kRoutingSchema = 1;
constexpr std::uint32_t kMaxRoleRoutes = 256;
constexpr std::size_t kMaxRoleBytes = 64;
constexpr std::uint32_t kMaxChopRoutes = static_cast<std::uint32_t>(limits::kMaxChops);

Status validateShape(const OutputRouting& r) {
  if (static_cast<std::uint8_t>(r.mode) > static_cast<std::uint8_t>(RoutingMode::Custom)) return makeError(ErrorCode::OutOfRange, "unknown output routing mode");
  if (r.roleBus.size() > kMaxRoleRoutes) return makeError(ErrorCode::LimitExceeded, "too many role routes", kMaxRoleRoutes);
  if (r.chopBus.size() > kMaxChopRoutes) return makeError(ErrorCode::LimitExceeded, "too many chop routes", kMaxChopRoutes);
  for (const auto& kv : r.roleBus) {
    if (kv.first.empty() || kv.first.size() > kMaxRoleBytes) return makeError(ErrorCode::OutOfRange, "a role name must be 1 to 64 bytes");
    if (kv.second > render::kMaxAuxBuses) return makeError(ErrorCode::OutOfRange, "an output is 0 (Main) to 8");
  }
  for (const auto& kv : r.chopBus)
    if (kv.second > render::kMaxAuxBuses) return makeError(ErrorCode::OutOfRange, "an output is 0 (Main) to 8");
  return {};
}

bool isDefault(const OutputRouting& r) { return r.mode == RoutingMode::MainOnly && r.roleBus.empty() && r.chopBus.empty(); }

std::vector<std::uint8_t> encode(const OutputRouting& r) {
  std::vector<std::uint8_t> out;
  bytes::Writer w(out);
  w.u8(static_cast<std::uint8_t>(r.mode));
  w.u32(static_cast<std::uint32_t>(r.roleBus.size()));
  for (const auto& kv : r.roleBus) {
    w.str(kv.first);
    w.u8(kv.second);
  }
  w.u32(static_cast<std::uint32_t>(r.chopBus.size()));
  for (const auto& kv : r.chopBus) {
    w.u64(kv.first);
    w.u8(kv.second);
  }
  return out;
}

Result<OutputRouting> decode(const std::vector<std::uint8_t>& in) {
  const Error bad = makeError(ErrorCode::Corrupt, "the output routing section is not valid");
  bytes::Reader r(in.data(), in.size());
  OutputRouting out;
  out.mode = static_cast<RoutingMode>(r.u8());
  const std::uint32_t nRole = r.u32();
  if (!r.ok() || nRole > kMaxRoleRoutes) return bad;
  for (std::uint32_t i = 0; i < nRole; ++i) {
    std::string role = r.str(kMaxRoleBytes);
    const std::uint8_t bus = r.u8();
    if (!r.ok()) return bad;
    out.roleBus[std::move(role)] = bus;
  }
  const std::uint32_t nChop = r.u32();
  if (!r.ok() || nChop > kMaxChopRoutes) return bad;
  for (std::uint32_t i = 0; i < nChop; ++i) {
    const std::uint64_t chop = r.u64();
    const std::uint8_t bus = r.u8();
    if (!r.ok()) return bad;
    out.chopBus[chop] = bus;
  }
  if (!r.ok() || r.remaining() != 0 || out.roleBus.size() != nRole || out.chopBus.size() != nChop) return bad;
  if (!validateShape(out).ok()) return bad;
  return out;
}

Status storeSection(ProjectSession& session, const OutputRouting& r) {
  StateSection sec;
  sec.tag = kTagRouting;
  sec.schema = kRoutingSchema;
  if (!isDefault(r)) sec.bytes = encode(r);
  return session.setSection(std::move(sec));
}

std::string busName(int bus) { return "Out " + std::to_string(bus + 1); }  // bus 1 is "Out 2"
}  // namespace

render::BusRouting ProjectSession::busRouting() const {
  render::BusRouting out;
  if (!chops_ || (routing_.roleBus.empty() && routing_.chopBus.empty())) return out;
  for (const ChopInfo& c : chops_->chops) {
    std::uint8_t bus = 0;
    auto own = routing_.chopBus.find(c.id.value);
    if (own != routing_.chopBus.end()) {
      bus = own->second;
    } else {
      const std::string role = roles_.roleOf(c.id);
      if (!role.empty()) {
        auto it = routing_.roleBus.find(role);
        if (it != routing_.roleBus.end()) bus = it->second;
      }
    }
    if (bus != 0) out.chopBus.push_back({c.id, bus});
  }
  std::sort(out.chopBus.begin(), out.chopBus.end(), [](const std::pair<ChopId, std::uint8_t>& a, const std::pair<ChopId, std::uint8_t>& b) { return a.first < b.first; });
  return out;
}

int ProjectSession::outputBusOf(ChopId chop) const {
  if (!chops_ || !chops_->find(chop)) return 0;
  const render::BusRouting r = busRouting();
  return r.busFor(chop);
}

Status ProjectSession::setOutputRouting(const OutputRouting& in) {
  Status st = validateShape(in);
  if (!st.ok()) return st;
  OutputRouting next = in;
  if (next.mode == RoutingMode::MainOnly) {
    next.roleBus.clear();
    next.chopBus.clear();
  } else {
    if (!chopMap_) return makeError(ErrorCode::InvalidArgument, "load a source first");
    std::set<std::string> known;
    for (const auto& kv : roles_.all()) known.insert(kv.second);
    for (const auto& kv : next.roleBus)
      if (!known.count(kv.first)) return makeError(ErrorCode::NotFound, "no chop has the role \"" + kv.first + "\"");
    for (const auto& kv : next.chopBus)
      if (!chopMap_->find(ChopId{kv.first})) return makeError(ErrorCode::NotFound, "unknown chop");
  }
  if (next == routing_) return {};
  const OutputRouting previous = routing_;
  routing_ = std::move(next);
  st = republish();
  if (!st.ok()) {
    routing_ = previous;
    republish();
    return st;
  }
  return storeSection(*this, routing_);
}

int ProjectSession::autoRouteByRole() {
  if (!chops_) return 0;
  std::vector<std::string> order;
  for (const ChopInfo& c : chops_->chops) {
    const std::string role = roles_.roleOf(c.id);
    if (!role.empty() && std::find(order.begin(), order.end(), role) == order.end()) order.push_back(role);
  }
  OutputRouting next;
  next.mode = RoutingMode::ByRole;
  const int routed = static_cast<int>(std::min<std::size_t>(order.size(), render::kMaxAuxBuses));
  for (int i = 0; i < routed; ++i) next.roleBus[order[static_cast<std::size_t>(i)]] = static_cast<std::uint8_t>(i + 1);
  if (!setOutputRouting(next).ok()) return 0;
  if (order.empty()) notice(Notice::Level::Info, "No chop has a role yet, so nothing was routed. Assign roles first.");
  else notice(Notice::Level::Info, "Routed " + std::to_string(routed) + (routed == 1 ? " role to " : " roles to ") + (routed == 1 ? busName(1) : busName(1) + "-" + std::to_string(routed + 1)) + ".");
  if (order.size() > static_cast<std::size_t>(render::kMaxAuxBuses)) notice(Notice::Level::Warning, "More than 8 roles: the extra roles stay on Main.");
  return routed;
}

int ProjectSession::autoRouteByChop() {
  if (!chops_) return 0;
  OutputRouting next;
  next.mode = RoutingMode::ByChop;
  int n = 0;
  for (const ChopInfo& c : chops_->chops) next.chopBus[c.id.value] = static_cast<std::uint8_t>(1 + (n++ % render::kMaxAuxBuses));
  if (!setOutputRouting(next).ok()) return 0;
  if (n > render::kMaxAuxBuses) notice(Notice::Level::Info, "More than 8 chops: outputs are shared.");
  return n;
}

Status ProjectSession::decodeRouting(const StateSection& s) {
  if (s.schema != kRoutingSchema) return makeError(ErrorCode::Corrupt, "unknown output routing section schema");
  auto r = decode(s.bytes);
  if (!r.ok()) return r.error();
  OutputRouting loaded = std::move(r.value());
  // Routes whose chop or role vanished since the project was saved are dropped.
  std::set<std::string> known;
  for (const auto& kv : roles_.all()) known.insert(kv.second);
  bool dropped = false;
  for (auto it = loaded.roleBus.begin(); it != loaded.roleBus.end();) {
    if (!known.count(it->first)) {
      it = loaded.roleBus.erase(it);
      dropped = true;
    } else {
      ++it;
    }
  }
  for (auto it = loaded.chopBus.begin(); it != loaded.chopBus.end();) {
    if (!chopMap_ || !chopMap_->find(ChopId{it->first})) {
      it = loaded.chopBus.erase(it);
      dropped = true;
    } else {
      ++it;
    }
  }
  if (dropped) notice(Notice::Level::Info, "Some output routes were dropped because their chops changed.");
  routing_ = std::move(loaded);
  Status st = republish();
  if (!st.ok()) {
    routing_ = OutputRouting{};
    return st;
  }
  if (dropped) return storeSection(*this, routing_);
  return {};
}

void ProjectSession::pruneRouting() {
  if (routing_.chopBus.empty() && routing_.roleBus.empty() && padSettings_.chokeOverride.empty()) return;
  std::set<std::uint64_t> alive;
  if (chopMap_)
    for (const source::Marker& m : chopMap_->markers()) alive.insert(m.id.value);
  bool routingChanged = false;
  for (auto it = routing_.chopBus.begin(); it != routing_.chopBus.end();) {
    if (!alive.count(it->first)) {
      it = routing_.chopBus.erase(it);
      routingChanged = true;
    } else {
      ++it;
    }
  }
  std::set<std::string> known;
  for (const auto& kv : roles_.all()) known.insert(kv.second);
  for (auto it = routing_.roleBus.begin(); it != routing_.roleBus.end();) {
    if (!known.count(it->first)) {
      it = routing_.roleBus.erase(it);
      routingChanged = true;
    } else {
      ++it;
    }
  }
  if (routingChanged) {
    if (routing_.roleBus.empty() && routing_.chopBus.empty()) routing_.mode = RoutingMode::MainOnly;
    storeSection(*this, routing_);
  }
  PadSettings next = padSettings_;
  for (auto it = next.chokeOverride.begin(); it != next.chokeOverride.end();) it = alive.count(it->first) ? std::next(it) : next.chokeOverride.erase(it);
  if (!(next == padSettings_)) setPadSettings(next);
}

void ProjectSession::resetPerChopRouting() {
  routing_ = OutputRouting{};
  storeSection(*this, routing_);
  if (!padSettings_.chokeOverride.empty()) {
    PadSettings next = padSettings_;
    next.chokeOverride.clear();
    setPadSettings(next);
  }
}

}  // namespace chopfractal::composition
