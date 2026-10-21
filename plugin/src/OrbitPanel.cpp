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

void OrbitPanel::mouseDown(const juce::MouseEvent& e) {
  const auto area = getLocalBounds();
  const double R = std::min(area.getWidth(), area.getHeight()) / 2.0 - 4.0;
  const auto c = area.getCentre();
  const auto pt = e.getPosition();
  grabEditorFocus();
  model_.select(cf::ui::hitTest(model_.view().orbit, static_cast<double>(pt.x - c.x), static_cast<double>(pt.y - c.y), R));
  repaint();
}
