#include "OrbitPanel.h"

#include <algorithm>
#include <cmath>

#include "CdlTheme.h"
#include "EditorTheme.h"

namespace cf = chopfractal;
using editor_theme::laneColour;

OrbitPanel::OrbitPanel(EditorModel& model, EditorCommands& commands) : EditorPanelBase(model, commands, "OrbitPanel") {
  setOpaque(true);
  setWantsKeyboardFocus(true);
}

void OrbitPanel::modelChanged(Change c) {
  if (c == Change::View || c == Change::Selection) repaint();
}

void OrbitPanel::paint(juce::Graphics& g) {
  const auto& orbit = model_.view().orbit;
  // The rings are float geometry that the rasteriser snaps to 1/256 pixel. Drawn in the editor's coordinates (the origin
  // shifted back by the panel position) they come out exactly as before the split; drawn panel-locally the
  // anti-aliased edges differ by up to 3/255.
  g.setOrigin(-getPosition());
  const auto area = getBounds();
  g.setFont(13.0f);
  g.setColour(editor_theme::canvas());
  g.fillRect(area);
  if (!orbit.arcs.empty()) {
    const auto c = area.getCentre().toFloat();
    const double R = std::min(area.getWidth(), area.getHeight()) / 2.0 - 4.0;
    const double ph = model_.playheadQuarters();
    const double posTicks = ph >= 0.0 ? ph * cf::kTicksPerQuarter : -1.0;
    auto pt = [&](double r, double a) { return juce::Point<float>(c.x + static_cast<float>(r * std::sin(a)), c.y - static_cast<float>(r * std::cos(a))); };
    g.setColour(cdl::pal().ink.withAlpha(0.18f));
    for (double a : orbit.beatAngles) g.drawLine(juce::Line<float>(pt(R * 0.25, a), pt(R, a)), 0.5f);
    g.setColour(cdl::pal().ink.withAlpha(0.45f));
    for (double a : orbit.barAngles) g.drawLine(juce::Line<float>(pt(R * 0.25, a), pt(R + 3.0, a)), 1.2f);
    for (const auto& a : orbit.arcs) {
      const auto rr = cf::ui::arcRadii(orbit, a, R);
      const double pulse = posTicks >= 0.0 ? cf::ui::hitPulse(a, posTicks, orbit.lengthTicks) : 0.0;
      juce::Path path;
      path.addCentredArc(c.x, c.y, static_cast<float>((rr.inner + rr.outer) / 2.0), static_cast<float>((rr.inner + rr.outer) / 2.0), 0.0f,
                         static_cast<float>(a.startAngle), static_cast<float>(a.startAngle + a.sweep), true);
      g.setColour(laneColour(a.ring).withAlpha(a.childActive ? 0.35f : static_cast<float>(0.45 + 0.4 * std::min(1.0, static_cast<double>(a.level)))).brighter(static_cast<float>(pulse)));
      g.strokePath(path, juce::PathStrokeType(static_cast<float>(std::max(2.0, rr.outer - rr.inner)), juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
      if (a.locked) {
        g.setColour(editor_theme::accentRed());
        g.strokePath(path, juce::PathStrokeType(1.0f));
      }
      if (a.id == model_.selected()) {
        g.setColour(cdl::pal().ink);
        g.strokePath(path, juce::PathStrokeType(2.0f));
      }
    }
    g.setColour(cdl::pal().ink.withAlpha(0.9f));
    g.setFont(10.0f);
    const double ringW = (R - 0.25 * R) / std::max(1, orbit.rings);
    for (int ring = 0; ring < orbit.rings; ++ring) g.drawText(juce::String(ring + 1), static_cast<int>(c.x) - 8, static_cast<int>(c.y - R + ring * ringW), 16, 10, juce::Justification::centred);
    if (plan_.active && dragId_.valid() && model_.view().barTicks > 0) {  // the ghost: where the drop would land
      cf::ui::OrbitArc ghost = dragArc_;
      ghost.ring = plan_.ring;
      ghost.startAngle = cf::ui::playheadAngle(static_cast<double>(plan_.bar * model_.view().barTicks + plan_.startInBar) / cf::kTicksPerQuarter, orbit.lengthTicks);
      const auto rr = cf::ui::arcRadii(orbit, ghost, R);
      juce::Path gp;
      const float mid = static_cast<float>((rr.inner + rr.outer) / 2.0);
      gp.addCentredArc(c.x, c.y, mid, mid, 0.0f, static_cast<float>(ghost.startAngle), static_cast<float>(ghost.startAngle + ghost.sweep), true);
      g.setColour(cdl::pal().ink.withAlpha(0.8f));
      g.strokePath(gp, juce::PathStrokeType(static_cast<float>(std::max(2.0, rr.outer - rr.inner)), juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    }
    if (posTicks >= 0.0) {
      const double a = cf::ui::playheadAngle(ph, orbit.lengthTicks);
      g.setColour(editor_theme::accentGreen());
      g.drawLine(juce::Line<float>(pt(0.0, a), pt(R + 3.0, a)), 1.5f);
    }
  } else {
    g.setColour(cdl::pal().inkMuted);
    g.drawText("Orbit view", area, juce::Justification::centred);
  }
}

double OrbitPanel::radius() const {
  const auto area = getLocalBounds();
  return std::min(area.getWidth(), area.getHeight()) / 2.0 - 4.0;
}

juce::Point<double> OrbitPanel::fromCentre(juce::Point<int> p) const {
  const auto c = getLocalBounds().getCentre();
  return {static_cast<double>(p.x - c.x), static_cast<double>(p.y - c.y)};
}

void OrbitPanel::mouseDown(const juce::MouseEvent& e) {
  const auto rel = fromCentre(e.getPosition());
  grabEditorFocus();
  plan_ = {};
  dragId_ = cf::EventId{};
  const cf::EventId hit = cf::ui::hitTest(model_.view().orbit, rel.x, rel.y, radius());
  model_.select(hit);
  repaint();
  if (!hit.valid()) return;
  if (e.mods.isPopupMenu()) {
    showMenu(hit);
    return;
  }
  for (const auto& a : model_.view().orbit.arcs)
    if (a.id == hit) {
      dragArc_ = a;
      break;
    }
  if (dragArc_.depth != 0) return;  // nested hits are edited from the pattern grid and the Inspector
  dragId_ = hit;
  dragStart_ = e.getPosition();
  dragRefused_ = cmd_.hitGuard(hit).isNotEmpty();
  const auto len = model_.view().orbit.lengthTicks;
  grabTicks_ = cf::ui::angleToTick(cf::ui::pointerAngle(rel.x, rel.y), len) - dragArc_.startTick;
}

void OrbitPanel::updatePlan(const juce::MouseEvent& e) {
  const auto& v = model_.view();
  const auto& orbit = v.orbit;
  if (v.barTicks <= 0 || v.barCount <= 0 || orbit.lengthTicks <= 0) return;
  const auto rel = fromCentre(e.getPosition());
  cf::Ticks tick = cf::ui::angleToTick(cf::ui::pointerAngle(rel.x, rel.y), orbit.lengthTicks) - grabTicks_;
  tick %= orbit.lengthTicks;
  if (tick < 0) tick += orbit.lengthTicks;
  if (model_.extras().ui.gridSnap && !e.mods.isCommandDown()) tick = cf::ui::snapNearest(tick, cmd_.snapGridTicks()) % orbit.lengthTicks;
  const auto pos = cf::ui::splitTick(tick, v.barTicks, v.barCount);
  plan_.bar = pos.bar;
  plan_.startInBar = std::min<cf::Ticks>(pos.startInBar, std::max<cf::Ticks>(0, v.barTicks - static_cast<cf::Ticks>(dragArc_.durationTick)));  // stays inside its bar
  const int ring = e.mods.isShiftDown() ? dragArc_.ring : cf::ui::ringAt(orbit, rel.x, rel.y, radius());
  plan_.ring = ring < 0 ? dragArc_.ring : ring;
}

void OrbitPanel::mouseDrag(const juce::MouseEvent& e) {
  if (!dragId_.valid() || dragRefused_) return;
  if (!plan_.active && e.getPosition().getDistanceFrom(dragStart_) < 4) return;
  plan_.active = true;
  updatePlan(e);
  repaint();
}

void OrbitPanel::mouseUp(const juce::MouseEvent& e) {
  if (!dragId_.valid()) return;
  const bool active = plan_.active && !dragRefused_;
  if (active) updatePlan(e);
  const Plan p = plan_;
  const cf::EventId id = dragId_;
  const auto arc = dragArc_;
  plan_ = {};
  dragId_ = cf::EventId{};
  repaint();
  if (!active) return;
  const auto& v = model_.view();
  const cf::Ticks oldBar = v.barTicks > 0 ? arc.startTick / v.barTicks : 0;
  const cf::Ticks oldStart = v.barTicks > 0 ? arc.startTick % v.barTicks : 0;
  if (p.bar == oldBar && p.startInBar == oldStart && p.ring == arc.ring) return;  // dropped where it was
  cmd_.moveHit(id, p.bar, p.startInBar, p.ring == arc.ring ? -1 : p.ring);
}

void OrbitPanel::showMenu(cf::EventId id) {
  bool enabled = true, locked = false;
  for (const auto& r : model_.view().pattern.rects)
    if (r.id == id) {
      enabled = r.enabled;
      locked = r.locked;
    }
  juce::PopupMenu m;
  m.addItem(1, "Edit hit...");
  m.addItem(2, enabled ? "Mute hit" : "Unmute hit");
  m.addItem(3, locked ? "Unlock hit" : "Lock hit");
  m.addSeparator();
  m.addItem(4, "Reset effects and transform");
  m.addSeparator();
  m.addItem(5, "Delete hit");
  juce::Component::SafePointer<OrbitPanel> alive(this);
  m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [alive, id, enabled, locked](int choice) {
    if (alive == nullptr || choice == 0) return;
    switch (choice) {
      case 1:
        if (alive->onEditHit) alive->onEditHit(id);
        break;
      case 2: alive->cmd_.setHitEnabled(id, !enabled); break;
      case 3: alive->cmd_.setHitLocked(id, !locked); break;
      case 4: alive->cmd_.resetHit(id); break;
      case 5: alive->cmd_.deleteHit(id); break;
      default: break;
    }
  });
}
