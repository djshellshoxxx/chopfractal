// F11: output routing model, playback bake, auto assignment, state section and pruning.
#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/composition/project_session.hpp>

#include <iterator>
#include <string>
#include <vector>

#include "chop_test.hpp"
#include "state_fixture.hpp"
#include "v1_fixture.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {
// Detected chops, topped up with extra markers to at least `minChops`, then a one-bar pattern.
bool routedSession(ProjectSession& s, int minChops = 4) {
  if (!statefix::loadAndDetect(s)) return false;
  for (std::int64_t pos = 3000; static_cast<int>(s.chops()->chops.size()) < minChops && pos < 90000; pos += 2500) {
    if (!s.editMarkers([&](source::ChopMap& m) { return m.addMarker(pos).ok() ? Status{} : Status{makeError(ErrorCode::Conflict, "marker")}; }).ok()) continue;
  }
  if (static_cast<int>(s.chops()->chops.size()) < minChops) return false;
  return s.generate(statefix::patternSettings(42, 1)).ok();
}

ChopId chopAt(const ProjectSession& s, std::size_t i) { return s.chops()->chops[i].id; }

int countWarnings(ProjectSession& s, const std::string& text) {
  int n = 0;
  for (const Notice& no : s.takeNotices())
    if (no.text == text) ++n;
  return n;
}

auto resolver() { return [](const source::SourceInfo&) { return statefix::drumLoop(); }; }
}  // namespace

CHOP_TEST(routing_roundtrips_and_a_project_that_never_routed_is_unchanged) {
  ProjectSession a;
  CHECK(routedSession(a));
  CHECK(a.assignRole(chopAt(a, 2), "kick").ok());
  const auto plain = a.saveState(false).value();
  CHECK(a.section(kTagRouting) == nullptr);
  CHECK(a.outputRouting().mode == RoutingMode::MainOnly);

  OutputRouting r;
  r.mode = RoutingMode::Custom;
  r.chopBus[chopAt(a, 0).value] = 3;
  r.chopBus[chopAt(a, 1).value] = 0;
  r.roleBus["kick"] = 5;
  CHECK(a.setOutputRouting(r).ok());
  CHECK(a.section(kTagRouting) != nullptr);
  const auto saved = a.saveState(false).value();
  CHECK(saved != plain);

  ProjectSession b;
  CHECK(b.loadState(saved.data(), saved.size(), resolver()).ok());
  CHECK(b.outputRouting() == r);
  CHECK(b.outputBusOf(chopAt(b, 0)) == 3 && b.outputBusOf(chopAt(b, 2)) == 5 && b.outputBusOf(chopAt(b, 1)) == 0);
  CHECK(b.playback()->eventBus.size() == b.playback()->events.size());
  CHECK(b.saveState(false).value() == saved);

  // Back to the default: the section disappears and the bytes equal the pre-routing save.
  CHECK(b.resetOutputRouting().ok());
  CHECK(b.section(kTagRouting) == nullptr);
  CHECK(b.saveState(false).value() == plain);

  // A pre-F11 project loads as Main only.
  ProjectSession c;
  const std::vector<std::uint8_t> v1(std::begin(v1fixture::kV1FixtureBytes), std::end(v1fixture::kV1FixtureBytes));
  CHECK(c.loadState(v1.data(), v1.size(), {}).ok());
  CHECK(c.outputRouting() == OutputRouting{});
}

CHOP_TEST(playback_carries_the_event_bus_and_the_effective_bus_prefers_the_chop_override) {
  ProjectSession s;
  CHECK(routedSession(s));
  CHECK(s.playback()->eventBus.empty());
  CHECK(s.assignRole(chopAt(s, 0), "kick").ok());
  CHECK(s.assignRole(chopAt(s, 1), "kick").ok());
  OutputRouting r;
  r.mode = RoutingMode::Custom;
  r.roleBus["kick"] = 2;
  r.chopBus[chopAt(s, 1).value] = 7;
  CHECK(s.setOutputRouting(r).ok());
  CHECK(s.outputBusOf(chopAt(s, 0)) == 2 && s.outputBusOf(chopAt(s, 1)) == 7 && s.outputBusOf(chopAt(s, 2)) == 0);
  const auto pb = s.playback();
  CHECK(pb->eventBus.size() == pb->events.size());
  for (std::size_t i = 0; i < pb->events.size(); ++i) CHECK_EQ(static_cast<int>(pb->eventBus[i]), s.outputBusOf(pb->events[i].chop));

  // A role assigned later moves that chop at once, because the role has a route.
  CHECK(s.assignRole(chopAt(s, 3), "kick").ok());
  CHECK(s.outputBusOf(chopAt(s, 3)) == 2);
  CHECK(s.resetOutputRouting().ok());
  CHECK(s.playback()->eventBus.empty());
}

CHOP_TEST(set_output_routing_validates_and_is_all_or_nothing) {
  ProjectSession s;
  CHECK(!s.setOutputRouting(OutputRouting{RoutingMode::Custom, {}, {{1, 1}}}).ok());  // no source yet
  CHECK(routedSession(s));
  OutputRouting good;
  good.mode = RoutingMode::Custom;
  good.chopBus[chopAt(s, 0).value] = 4;
  CHECK(s.setOutputRouting(good).ok());

  OutputRouting bad = good;
  bad.chopBus[chopAt(s, 1).value] = 9;
  CHECK(s.setOutputRouting(bad).error().code == ErrorCode::OutOfRange);
  bad = good;
  bad.chopBus[999999] = 1;
  CHECK(s.setOutputRouting(bad).error().code == ErrorCode::NotFound);
  bad = good;
  bad.roleBus["nobody"] = 1;
  CHECK(s.setOutputRouting(bad).error().code == ErrorCode::NotFound);
  bad = good;
  bad.mode = static_cast<RoutingMode>(9);
  CHECK(!s.setOutputRouting(bad).ok());
  bad = good;
  bad.roleBus[std::string(65, 'x')] = 1;
  CHECK(!s.setOutputRouting(bad).ok());
  CHECK(s.outputRouting() == good);  // every refusal left the session as it was

  OutputRouting main = good;
  main.mode = RoutingMode::MainOnly;  // Main only clears the routes
  CHECK(s.setOutputRouting(main).ok());
  CHECK(s.outputRouting() == OutputRouting{});
}

CHOP_TEST(auto_route_by_role_is_deterministic) {
  ProjectSession a, b;
  CHECK(routedSession(a, 12) && routedSession(b, 12));
  for (ProjectSession* s : {&a, &b})
    for (std::size_t i = 0; i < 10; ++i) CHECK(s->assignRole(chopAt(*s, i), "r" + std::to_string(i % 10)).ok());
  const int ra = a.autoRouteByRole();
  const int rb = b.autoRouteByRole();
  CHECK_EQ(ra, 8);
  CHECK_EQ(rb, 8);
  CHECK(a.outputRouting() == b.outputRouting());
  CHECK(a.outputRouting().mode == RoutingMode::ByRole);
  CHECK(a.outputRouting().roleBus.size() == 8);
  CHECK_EQ(static_cast<int>(a.outputRouting().roleBus.at("r0")), 1);  // first appearance in snapshot order gets Out 2
  CHECK_EQ(static_cast<int>(a.outputRouting().roleBus.at("r7")), 8);
  CHECK(a.outputRouting().roleBus.count("r8") == 0 && a.outputRouting().roleBus.count("r9") == 0);  // beyond 8 stay on Main
  int warnings = 0, info = 0;
  for (const Notice& n : a.takeNotices()) {
    if (n.level == Notice::Level::Warning && n.text == "More than 8 roles: the extra roles stay on Main.") ++warnings;
    if (n.level == Notice::Level::Info && n.text == "Routed 8 roles to Out 2-9.") ++info;
  }
  CHECK_EQ(warnings, 1);
  CHECK_EQ(info, 1);

  // Fewer roles: the notice is exact and singular where needed.
  ProjectSession c;
  CHECK(routedSession(c));
  CHECK(c.autoRouteByRole() == 0);  // no roles yet
  CHECK(c.assignRole(chopAt(c, 0), "kick").ok());
  CHECK(c.autoRouteByRole() == 1);
  CHECK(countWarnings(c, "Routed 1 role to Out 2.") == 1);
}

CHOP_TEST(auto_route_by_chop_wraps_after_eight) {
  ProjectSession s;
  CHECK(routedSession(s, 10));
  const int n = static_cast<int>(s.chops()->chops.size());
  CHECK_EQ(s.autoRouteByChop(), n);
  CHECK(s.outputRouting().mode == RoutingMode::ByChop);
  for (int i = 0; i < n; ++i) CHECK_EQ(s.outputBusOf(chopAt(s, static_cast<std::size_t>(i))), 1 + i % 8);
  CHECK(countWarnings(s, "More than 8 chops: outputs are shared.") == 1);
  // Choosing a single route afterwards is a Custom edit that keeps the others.
  OutputRouting r = s.outputRouting();
  r.mode = RoutingMode::Custom;
  r.chopBus[chopAt(s, 0).value] = 0;
  CHECK(s.setOutputRouting(r).ok());
  CHECK(s.outputBusOf(chopAt(s, 0)) == 0 && s.outputBusOf(chopAt(s, 1)) == 2);
}

CHOP_TEST(routes_of_deleted_chops_are_pruned_and_the_section_goes_away) {
  ProjectSession s;
  CHECK(routedSession(s));
  const auto plain = [&] {
    ProjectSession t;
    CHECK(routedSession(t));
    return t.saveState(false).value();
  }();
  OutputRouting r;
  r.mode = RoutingMode::Custom;
  const ChopId victim = chopAt(s, 1);
  r.chopBus[victim.value] = 2;
  CHECK(s.setOutputRouting(r).ok());
  PadSettings ps;
  ps.chokeOverride[victim.value] = 3;
  CHECK(s.setPadSettings(ps).ok());
  CHECK(s.section(kTagRouting) != nullptr && s.section(kTagPads) != nullptr);

  CHECK(s.editMarkers([&](source::ChopMap& m) { return m.deleteMarker(victim); }).ok());
  CHECK(s.outputRouting().chopBus.empty());
  CHECK(s.outputRouting().mode == RoutingMode::MainOnly);
  CHECK(s.padSettings().chokeOverride.empty());
  CHECK(s.section(kTagRouting) == nullptr && s.section(kTagPads) == nullptr);
  CHECK(s.playback()->eventBus.empty());
  (void)plain;
}

CHOP_TEST(a_new_source_resets_routes_but_relinking_keeps_them) {
  ProjectSession s;
  CHECK(routedSession(s));
  CHECK(s.autoRouteByChop() > 0);
  PadSettings ps;
  ps.baseNote = 40;
  ps.chokeOverride[chopAt(s, 0).value] = 2;
  CHECK(s.setPadSettings(ps).ok());
  const OutputRouting before = s.outputRouting();
  CHECK(s.relinkSource(s.sourceData()).ok());
  CHECK(s.outputRouting() == before);

  CHECK(s.loadSource(statefix::drumLoop(), "other", "/music/other.wav").ok());
  CHECK(s.outputRouting() == OutputRouting{});
  CHECK(s.section(kTagRouting) == nullptr);
  CHECK(s.padSettings().baseNote == 40 && s.padSettings().chokeOverride.empty());  // project-level choices stay, chop-bound ones go

  s.reset();
  CHECK(s.outputRouting() == OutputRouting{} && s.section(kTagRouting) == nullptr);
}

CHOP_TEST(an_export_is_the_main_fold_down_whatever_the_routing) {
  ProjectSession a, b;
  CHECK(routedSession(a) && routedSession(b));
  CHECK(b.autoRouteByChop() > 0);
  ExportWavOptions o;
  auto wa = a.renderWavBytes(o);
  auto wb = b.renderWavBytes(o);
  CHECK(wa.ok() && wb.ok());
  if (wa.ok() && wb.ok()) CHECK(wa.value() == wb.value());
  auto ma = a.renderMidi(KitOptions{});
  auto mb = b.renderMidi(KitOptions{});
  CHECK(ma.ok() && mb.ok());
  if (ma.ok() && mb.ok()) CHECK(ma.value() == mb.value());
}

CHOP_TEST(aux_availability_is_clamped_and_never_changes_the_playback) {
  ProjectSession s;
  CHECK(routedSession(s));
  CHECK(s.autoRouteByChop() > 0);
  const auto pb = s.playback();
  s.setAuxBusesAvailable(3);
  CHECK(s.auxBusesAvailable() == 3 && s.playback() == pb);
  s.setAuxBusesAvailable(99);
  CHECK(s.auxBusesAvailable() == 8);
  s.setAuxBusesAvailable(-4);
  CHECK(s.auxBusesAvailable() == 0);
}

CHOP_TEST(routes_to_vanished_chops_in_a_saved_project_are_dropped_with_one_notice) {
  ProjectSession a;
  CHECK(routedSession(a));
  std::vector<std::uint8_t> payload;
  bytes::Writer w(payload);
  w.u8(static_cast<std::uint8_t>(RoutingMode::Custom));
  w.u32(0);
  w.u32(2);
  w.u64(999999);  // no such chop
  w.u8(2);
  w.u64(chopAt(a, 0).value);
  w.u8(4);
  StateSection sec;
  sec.tag = kTagRouting;
  sec.schema = 1;
  sec.bytes = payload;
  CHECK(a.setSection(sec).ok());
  const auto saved = a.saveState(false).value();
  ProjectSession b;
  CHECK(b.loadState(saved.data(), saved.size(), resolver()).ok());
  CHECK(b.outputRouting().chopBus.size() == 1 && b.outputRouting().chopBus.begin()->second == 4);
  CHECK(countWarnings(b, "Some output routes were dropped because their chops changed.") == 1);
  const StateSection* kept = b.section(kTagRouting);
  CHECK(kept != nullptr && kept->bytes != payload);  // rewritten without the vanished route
}

CHOP_TEST(hostile_routing_sections_are_dropped_with_a_warning_and_leave_main_only) {
  ProjectSession a;
  CHECK(routedSession(a));
  OutputRouting r;
  r.mode = RoutingMode::Custom;
  CHECK(a.assignRole(chopAt(a, 0), "kick").ok());
  r.roleBus["kick"] = 3;
  r.chopBus[chopAt(a, 1).value] = 6;
  CHECK(a.setOutputRouting(r).ok());
  const StateSection good = *a.section(kTagRouting);

  auto tryLoad = [&](const StateSection& sec) {
    ProjectSession w;
    CHECK(routedSession(w));
    CHECK(w.setSection(sec).ok());
    const auto bytes = w.saveState(false).value();
    ProjectSession l;
    CHECK(l.loadState(bytes.data(), bytes.size(), resolver()).ok());
    return std::make_pair(l.section(kTagRouting) != nullptr, l.outputRouting());
  };
  for (std::size_t n = 1; n < good.bytes.size(); ++n) {
    StateSection t = good;
    t.bytes.resize(n);
    auto res = tryLoad(t);
    CHECK(!res.first && res.second == OutputRouting{});
  }
  StateSection extra = good;
  extra.bytes.push_back(7);
  CHECK(!tryLoad(extra).first);
  StateSection schema = good;
  schema.schema = 4;
  CHECK(!tryLoad(schema).first);
  for (std::size_t i = 0; i < good.bytes.size(); ++i) {
    StateSection t = good;
    t.bytes[i] = 0xFF;
    auto res = tryLoad(t);
    CHECK(res.first || res.second == OutputRouting{});  // valid or dropped, never half applied
  }
  // Out-of-range buses are corrupt, not clamped.
  StateSection hot = good;
  hot.bytes.back() = 9;
  CHECK(!tryLoad(hot).first);
  CHECK(tryLoad(good).first);
}
