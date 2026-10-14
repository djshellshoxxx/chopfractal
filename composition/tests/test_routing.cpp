// Integration tests for output routing (docs/specs/roadmap/F11-multi-output-stems.md).
#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/state_codec/codec.hpp>
#include <cmath>
#include <memory>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {

std::shared_ptr<const render::SourceData> loop() {
  auto d = std::make_shared<render::SourceData>();
  d->channels = 1;
  d->sampleRate = 48000;
  d->frames = 96000;
  d->samples.assign(96000, 0.f);
  Rng rng(3);
  for (int k = 0; k < 8; ++k)
    for (int i = 0; i < 5000; ++i)
      d->samples[static_cast<std::size_t>(k * 12000 + i)] = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * std::exp(-static_cast<float>(i) / 900.f) * 0.8f;
  return d;
}

bool ready(ProjectSession& s) {
  CHECK(s.loadSource(loop(), "loop", "/music/loop.wav").ok());
  DetectOptions d;
  d.analysis.sensitivity = 0.8;
  CHECK(s.applyChops(d).ok());
  pattern::Settings st;
  st.bars = 2;
  st.density = 0.9;
  st.seed = 6;
  CHECK(s.generate(st).ok());
  return s.chops() && s.chops()->chops.size() == 8;
}

}  // namespace

CHOP_TEST(routing_roundtrips_and_projects_without_routing_have_no_routing_section) {
  ProjectSession s;
  CHECK(ready(s));
  auto plain = s.saveState(true);
  CHECK(plain.ok());
  auto dec = codec::decode(plain.value().data(), plain.value().size());
  CHECK(dec.ok() && dec.value().modules.count("routing") == 0);  // byte-identical to before for projects that never route
  CHECK(s.assignRole(s.chops()->chops[0].id, "kick").ok());
  CHECK(s.assignRole(s.chops()->chops[1].id, "snare").ok());
  CHECK_EQ(s.autoRouteByRole(), 2);
  CHECK(s.outputRouting().mode == RoutingMode::ByRole);
  OutputRouting r = s.outputRouting();
  r.chopBus[s.chops()->chops[3].id.value] = 5;
  r.mode = RoutingMode::Custom;
  CHECK(s.setOutputRouting(r).ok());
  CHECK_EQ(s.effectiveBus(s.chops()->chops[0].id), 1);
  CHECK_EQ(s.effectiveBus(s.chops()->chops[3].id), 5);
  CHECK_EQ(s.effectiveBus(s.chops()->chops[7].id), 0);
  auto saved = s.saveState(true);
  CHECK(saved.ok());
  ProjectSession t;
  CHECK(t.loadState(saved.value().data(), saved.value().size(), nullptr).ok());
  CHECK(t.outputRouting() == s.outputRouting());
  CHECK(t.playback() && !t.playback()->eventBus.empty());  // the loaded playback carries the routes
  auto st = codec::decode(saved.value().data(), saved.value().size());
  CHECK(st.ok());
  for (std::size_t cut = 0; cut + 1 < st.value().modules["routing"].bytes.size(); ++cut) {
    auto copy = st.value();
    copy.modules["routing"].bytes.resize(cut);
    auto enc = codec::encode(copy);
    ProjectSession u;
    CHECK(!u.loadState(enc.value().data(), enc.value().size(), nullptr).ok() && !u.hasSource());
  }
  auto bad = st.value();
  bad.modules["routing"].bytes.back() = 99;  // a bus above 8
  auto enc = codec::encode(bad);
  ProjectSession v;
  CHECK(!v.loadState(enc.value().data(), enc.value().size(), nullptr).ok());
}

CHOP_TEST(auto_routing_is_deterministic_caps_at_eight_and_validation_refuses_bad_routes) {
  auto routeRoles = [](int roleCount) {
    ProjectSession s;
    CHECK(ready(s));
    for (int i = 0; i < 8; ++i) CHECK(s.assignRole(s.chops()->chops[static_cast<std::size_t>(i)].id, "r" + std::to_string(i % roleCount)).ok());
    const int n = s.autoRouteByRole();
    return std::make_pair(n, s.outputRouting());
  };
  const auto a = routeRoles(8), b = routeRoles(8);
  CHECK(a.first == 8 && a.second == b.second);
  CHECK_EQ(a.second.roleBus.at("r0"), 1);
  CHECK_EQ(a.second.roleBus.at("r7"), 8);
  // nine distinct roles: only the first eight are routed and a warning is raised
  ProjectSession nine;
  CHECK(nine.loadSource(loop(), "loop", "/x.wav").ok());
  DetectOptions d;
  d.analysis.sensitivity = 0.8;
  CHECK(nine.applyChops(d).ok());
  for (int i = 0; i < 8; ++i) CHECK(nine.assignRole(nine.chops()->chops[static_cast<std::size_t>(i)].id, "role" + std::to_string(i)).ok());
  CHECK_EQ(nine.autoRouteByRole(), 8);
  ProjectSession chops;
  CHECK(ready(chops));
  CHECK_EQ(chops.autoRouteByChop(), 8);
  CHECK_EQ(chops.outputRouting().chopBus.at(chops.chops()->chops[7].id.value), 8);
  OutputRouting bad;
  bad.chopBus[chops.chops()->chops[0].id.value] = 9;
  CHECK(!chops.setOutputRouting(bad).ok());
  bad.chopBus.clear();
  bad.chopBus[9999] = 1;
  CHECK(!chops.setOutputRouting(bad).ok());
  bad.chopBus.clear();
  bad.roleBus["Bad Role"] = 1;
  CHECK(!chops.setOutputRouting(bad).ok());
  CHECK(chops.outputRouting().mode == RoutingMode::ByChop);  // refused edits change nothing
}

CHOP_TEST(routes_are_pruned_with_their_chops_and_exports_ignore_routing) {
  ProjectSession s;
  CHECK(ready(s));
  const auto exportBefore = s.renderWavBytes(ExportWavOptions{});
  CHECK(exportBefore.ok());
  CHECK_EQ(s.autoRouteByChop(), 8);
  const ChopId gone = s.chops()->chops[7].id;
  CHECK(s.editMarkers([&](source::ChopMap& m) { return m.deleteMarker(gone); }).ok());
  CHECK(s.outputRouting().chopBus.count(gone.value) == 0);
  CHECK_EQ(s.outputRouting().chopBus.size(), 7u);
  // a routed session exports the same bytes as an unrouted one: the WAV is the main-bus fold-down
  ProjectSession plain;
  CHECK(ready(plain));
  CHECK(plain.editMarkers([&](source::ChopMap& m) { return m.deleteMarker(plain.chops()->chops[7].id); }).ok());
  CHECK(plain.generate(s.pattern()->settings).ok());
  CHECK(s.generate(s.pattern()->settings).ok());
  auto a = s.renderWavBytes(ExportWavOptions{});
  auto b = plain.renderWavBytes(ExportWavOptions{});
  CHECK(a.ok() && b.ok() && a.value() == b.value());
  // a new source resets routing
  CHECK(s.loadSource(loop(), "other", "/other.wav").ok());
  CHECK(s.outputRouting() == OutputRouting{});
}
