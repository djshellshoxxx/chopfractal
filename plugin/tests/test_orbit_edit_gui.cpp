// F02: hits are edited from the Orbit drawer too. A drag moves a top-level hit along time and to another ring with a ghost,
// one undo step on release; a click only selects; a locked hit refuses the drag.
#include <map>

#include "OrbitPanel.h"
#include "perform_test_support.hpp"
#include "workflow_test_support.hpp"

using namespace shelltest;
namespace cf = chopfractal;

namespace {
struct Geo {
  juce::Point<int> mid;
  double R = 0;
  juce::Point<int> centre;
};

juce::Point<int> pointAt(const OrbitPanel& panel, const cf::ui::OrbitView& v, double angle, int ring) {
  const double R = std::min(panel.getWidth(), panel.getHeight()) / 2.0 - 4.0;
  cf::ui::OrbitArc probe;
  probe.ring = ring;
  const auto rr = cf::ui::arcRadii(v, probe, R);
  const double r = (rr.inner + rr.outer) / 2.0;
  const auto c = panel.getLocalBounds().getCentre();
  return {c.x + static_cast<int>(std::lround(r * std::sin(angle))), c.y - static_cast<int>(std::lround(r * std::cos(angle)))};
}

void drag(OrbitPanel& panel, juce::Point<int> from, juce::Point<int> to, juce::ModifierKeys mods = {}) {
  auto src = juce::Desktop::getInstance().getMainMouseSource();
  const auto now = juce::Time::getCurrentTime();
  auto ev = [&](juce::Point<int> pt) { return juce::MouseEvent(src, pt.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &panel, &panel, now, from.toFloat(), now, 1, true); };
  panel.mouseDown(ev(from));
  panel.mouseDrag(ev(to));
  panel.mouseUp(ev(to));
}

std::map<int, int> perRing(EditorModel& m) {
  std::map<int, int> n;
  for (const auto& r : m.view().pattern.rects) ++n[r.chopIndex];
  return n;
}
}  // namespace

CHOP_TEST(dragging_an_orbit_arc_moves_the_hit_to_another_ring_and_one_undo_puts_it_back) {
  if (!haveDisplay()) return;
  EditorRig rig(true);
  if (!rig.ed) return;
  CHECK(rig.ed->openDrawer("orbit"));
  pump(150);
  auto& panel = rig.ed->orbitPanel();
  auto& model = rig.ed->model();
  const auto& orbit = model.view().orbit;
  const cf::ui::OrbitArc* pick = nullptr;
  for (const auto& a : orbit.arcs)
    if (a.depth == 0 && !a.locked && a.ring == 0 && a.sweep > 0.05) {
      pick = &a;
      break;
    }
  CHECK(pick != nullptr);
  if (!pick || orbit.rings < 2) return;
  const cf::ui::OrbitArc arc = *pick;
  const auto before = perRing(model);
  const auto from = pointAt(panel, orbit, arc.startAngle + arc.sweep / 2, arc.ring);
  const auto to = pointAt(panel, orbit, arc.startAngle + arc.sweep / 2, 1);

  // A click only selects.
  drag(panel, from, from);
  pump(100);
  CHECK(perRing(model) == before);
  CHECK(model.selected() == arc.id);

  // A drag to the next ring moves the hit there.
  drag(panel, from, to);
  pump(200);
  const auto after = perRing(model);
  CHECK_EQ(after.count(1) ? after.at(1) : 0, (before.count(1) ? before.at(1) : 0) + 1);
  CHECK_EQ(after.count(0) ? after.at(0) : 0, before.at(0) - 1);

  // One undo restores the pattern.
  rig.ed->commands().undo();
  pump(200);
  CHECK(perRing(model) == before);
}

CHOP_TEST(the_orbit_drag_shows_a_ghost_and_shift_keeps_the_ring) {
  if (!haveDisplay()) return;
  EditorRig rig(true);
  if (!rig.ed) return;
  CHECK(rig.ed->openDrawer("orbit"));
  pump(150);
  auto& panel = rig.ed->orbitPanel();
  auto& model = rig.ed->model();
  const auto& orbit = model.view().orbit;
  const cf::ui::OrbitArc* pick = nullptr;
  for (const auto& a : orbit.arcs)
    if (a.depth == 0 && !a.locked && a.ring == 0 && a.sweep > 0.05) {
      pick = &a;
      break;
    }
  CHECK(pick != nullptr);
  if (!pick || orbit.rings < 2) return;
  const cf::ui::OrbitArc arc = *pick;
  const auto from = pointAt(panel, orbit, arc.startAngle + arc.sweep / 2, arc.ring);
  const auto to = pointAt(panel, orbit, arc.startAngle + arc.sweep / 2, 1);
  auto src = juce::Desktop::getInstance().getMainMouseSource();
  const auto now = juce::Time::getCurrentTime();
  auto ev = [&](juce::Point<int> pt, juce::ModifierKeys m) { return juce::MouseEvent(src, pt.toFloat(), m, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &panel, &panel, now, from.toFloat(), now, 1, true); };
  panel.mouseDown(ev(from, {}));
  CHECK(!panel.plan().active);
  panel.mouseDrag(ev(to, {}));
  CHECK(panel.plan().active && panel.plan().ring == 1);
  panel.mouseDrag(ev(to, juce::ModifierKeys(juce::ModifierKeys::shiftModifier)));
  CHECK(panel.plan().ring == arc.ring);  // Shift keeps the ring
  panel.mouseUp(ev(from, {}));            // back where it started: nothing is committed
  CHECK(!panel.plan().active);
  pump(100);
}

CHOP_TEST(a_locked_hit_refuses_the_orbit_drag) {
  if (!haveDisplay()) return;
  EditorRig rig(true);
  if (!rig.ed) return;
  CHECK(rig.ed->openDrawer("orbit"));
  pump(150);
  auto& panel = rig.ed->orbitPanel();
  auto& model = rig.ed->model();
  const auto& orbit = model.view().orbit;
  const cf::ui::OrbitArc* pick = nullptr;
  for (const auto& a : orbit.arcs)
    if (a.depth == 0 && a.ring == 0 && a.sweep > 0.05) {
      pick = &a;
      break;
    }
  CHECK(pick != nullptr);
  if (!pick || orbit.rings < 2) return;
  const cf::ui::OrbitArc arc = *pick;
  rig.ed->commands().setHitLocked(arc.id, true);
  pump(200);
  const auto before = perRing(model);
  const auto& o2 = model.view().orbit;
  const auto from = pointAt(panel, o2, arc.startAngle + arc.sweep / 2, arc.ring);
  const auto to = pointAt(panel, o2, arc.startAngle + arc.sweep / 2, 1);
  drag(panel, from, to);
  pump(150);
  CHECK(perRing(model) == before);
  CHECK(!panel.plan().active);
}
