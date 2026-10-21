#include <chopfractal/plugin_ui_adapter/fx_rack_view.hpp>
#include <chopfractal/plugin_ui_adapter/inspector_view.hpp>
#include <chopfractal/plugin_ui_adapter/views.hpp>
#include <cmath>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::ui;

namespace {
ChopSnapshot makeChops(int n = 4) {
  ChopSnapshot s;
  s.sourceFrames = 12000 * n;
  s.sampleRate = 48000;
  for (int i = 0; i < n; ++i) s.chops.push_back({ChopId{static_cast<std::uint64_t>(i + 1)}, {i * 12000ll, (i + 1) * 12000ll}, 0, 0, 0});
  return s;
}
pattern::Pattern makePattern(const ChopSnapshot& c) {
  pattern::Settings s;
  s.bars = 2;
  s.density = 0.8;
  return pattern::generate(c, s).value();
}
std::vector<EventId> topHits(const pattern::Pattern& p, const ChopSnapshot& c, std::size_t n) {
  std::vector<EventId> ids;
  for (const auto& r : buildPatternView(p, c).rects)
    if (r.topLevel && ids.size() < n) ids.push_back(r.id);
  return ids;
}
}  // namespace

CHOP_TEST(level_conversions_round_trip_and_format) {
  CHECK_NEAR(levelToDb(1.f), 0.0, 1e-9);
  CHECK_NEAR(levelToDb(2.f), 6.0206, 1e-3);
  CHECK(levelToDb(0.f) == kInspectorMinDb);
  CHECK(dbToLevel(kInspectorMinDb) == 0.f);
  CHECK(dbToLevel(100.0) <= 4.f);  // clamped to the engine maximum
  CHECK_NEAR(dbToLevel(levelToDb(0.5f)), 0.5, 1e-5);
  CHECK(formatLevelDb(0.f) == "-inf dB");
  CHECK(formatLevelDb(1.f) == "0.0 dB");
  CHECK(formatPan(0.f) == "C" && formatPan(-0.4f) == "L40" && formatPan(0.25f) == "R25");
  CHECK(formatPitch(0.f) == "0 st" && formatPitch(3.f) == "+3 st" && formatPitch(-12.f) == "-12 st" && formatPitch(0.5f) == "+0.5 st");
  CHECK(formatPercent(0.8) == "80 %");
}

CHOP_TEST(inspector_view_reports_one_hit_and_marks_mixed_values) {
  const auto chops = makeChops();
  auto p = makePattern(chops);
  const auto ids = topHits(p, chops, 2);
  CHECK(ids.size() == 2);
  if (ids.size() < 2) return;
  EventTransform tx;
  tx.level = 0.5f;
  tx.pan = -0.4f;
  tx.pitchSemitones = 3.f;
  tx.reverse = true;
  tx.retrigger = 2;
  auto r = pattern::setEventTransform(p, ids[0], tx);
  CHECK(r.ok());
  p = r.value();
  const auto one = buildInspectorView(p, {ids[0]}, chops);
  CHECK(one.hitCount == 1 && one.editable && one.refusal.empty());
  CHECK(one.levelDb.text == "-6.0 dB" && one.pan.text == "L40" && one.pitch.text == "+3 st" && one.retrigger.text == "2");
  CHECK(one.reverse && !one.reverseMixed && !one.levelDb.mixed);
  CHECK(one.title.rfind("Hit ", 0) == 0);
  const auto two = buildInspectorView(p, ids, chops);
  CHECK(two.hitCount == 2 && two.title == "2 hits");
  CHECK(two.levelDb.mixed && two.levelDb.text == "mixed");
  CHECK(two.reverseMixed && two.pan.mixed && two.pitch.mixed);
  const auto none = buildInspectorView(p, {}, chops);
  CHECK(none.hitCount == 0 && !none.editable && none.title == "No hit selected" && !none.refusal.empty());
}

CHOP_TEST(inspector_view_shows_locks_and_roles) {
  const auto chops = makeChops();
  auto p = makePattern(chops);
  const auto ids = topHits(p, chops, 1);
  CHECK(!ids.empty());
  if (ids.empty()) return;
  auto locked = pattern::setLock(p, {ScopeLevel::Event, 0, 0, ids[0]}, true);
  CHECK(locked.ok());
  if (!locked.ok()) return;
  const auto v = buildInspectorView(locked.value(), ids, chops, [](ChopId id) { return id.value == 1 ? std::string("Kick") : std::string("Snare"); });
  CHECK(v.locked && !v.editable && !v.refusal.empty());
  CHECK(v.role == "Kick" || v.role == "Snare");
  CHECK(v.accessibleText.find("locked") != std::string::npos);
}

CHOP_TEST(fx_rack_round_trips_every_module_and_control) {
  const auto chops = makeChops();
  const auto p = makePattern(chops);
  const auto ids = topHits(p, chops, 1);
  CHECK(!ids.empty());
  if (ids.empty()) return;
  const Event* e = pattern::findEvent(p, ids[0]);
  CHECK(e != nullptr);
  if (!e) return;
  for (int m = 0; m < kFxModuleCount; ++m) {
    const auto mod = static_cast<FxModule>(m);
    HitFx h = hitFxOf(*e);
    fxSetModuleOn(h, mod, false);
    CHECK(!fxModuleOn(h, mod));
    fxSetModuleOn(h, mod, true);
    CHECK(fxModuleOn(h, mod));
    for (const auto& spec : fxControlSpecs(mod)) {
      if (spec.kind == FxControlKind::Slider || spec.kind == FxControlKind::Choice) {
        fxSetControl(h, mod, spec.id, spec.max);
        CHECK_NEAR(fxControlValue(h, mod, spec.id), spec.max, spec.step + 1e-6);
        fxSetControl(h, mod, spec.id, spec.min - 5.0);  // clamped
        CHECK(fxControlValue(h, mod, spec.id) >= spec.min - 1e-6);
      }
      CHECK(!fxControlText(mod, spec, spec.def).empty());
    }
    fxSetModuleOn(h, mod, false);
    CHECK(!fxModuleOn(h, mod));
  }
}

CHOP_TEST(fx_rack_edits_survive_the_pattern_and_report_mixed) {
  const auto chops = makeChops();
  auto p = makePattern(chops);
  const auto ids = topHits(p, chops, 2);
  CHECK(ids.size() == 2);
  if (ids.size() < 2) return;
  HitFx h = hitFxOf(*pattern::findEvent(p, ids[0]));
  fxSetControl(h, FxModule::Echo, "intensity", 0.6);
  fxSetControl(h, FxModule::Filter, "intensity", 0.5);
  auto a = pattern::setEventFx(p, ids[0], h.fx);
  CHECK(a.ok());
  if (!a.ok()) return;
  auto b = pattern::setEventGestureFx(a.value(), ids[0], h.gesture);
  CHECK(b.ok());
  if (!b.ok()) return;
  p = b.value();
  const HitFx back = hitFxOf(*pattern::findEvent(p, ids[0]));
  CHECK(fxModuleOn(back, FxModule::Echo) && fxModuleOn(back, FxModule::Filter));
  const auto view = buildFxRackView(p, ids);
  CHECK(view.modules.size() == static_cast<std::size_t>(kFxModuleCount));
  CHECK(view.hitCount == 2);
  const auto& echo = view.modules[static_cast<std::size_t>(FxModule::Echo)];
  CHECK(echo.mixed && !echo.on && echo.onCount == 1 && echo.total == 2);
  const auto one = buildFxRackView(p, {ids[0]});
  CHECK(one.modules[static_cast<std::size_t>(FxModule::Echo)].on);
  CHECK(buildFxRackView(p, {}).hitCount == 0);
}
