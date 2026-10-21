#include "HistoryPanel.h"

#include <algorithm>
#include <cstdlib>

#include "CdlTheme.h"
#include "EditorTheme.h"

namespace cf = chopfractal;

HistoryPanel::HistoryPanel(EditorModel& model, EditorCommands& commands) : EditorPanelBase(model, commands, "HistoryPanel") {
  setOpaque(true);
  setWantsKeyboardFocus(true);
}

void HistoryPanel::modelChanged(Change c) {
  if (c == Change::View || c == Change::Flags) repaint();
}

juce::Point<int> HistoryPanel::treePos(const cf::ui::TreeNodeView& n) const {
  const auto& v = model_.view();
  const int stepX = v.treeCols > 0 ? std::min(64, std::max(6, (getWidth() - 60) / v.treeCols)) : 64;
  const int stepY = v.treeRows > 0 ? std::min(30, std::max(6, (getHeight() - 40) / v.treeRows)) : 30;
  return {24 + n.column * stepX, 22 + n.depth * stepY};
}

void HistoryPanel::paint(juce::Graphics& g) {
  const auto& tree = model_.view().tree;
  g.setFont(13.0f);
  g.setColour(editor_theme::canvas());
  g.fillRect(getLocalBounds());
  for (const auto& n : tree) {
    const auto np = treePos(n);
    const int x = np.x, y = np.y;
    for (const auto& p : tree)
      if (p.id == n.parent) {
        g.setColour(cdl::pal().inkMuted);
        const auto pp = treePos(p);
        g.drawLine(static_cast<float>(pp.x), static_cast<float>(pp.y), static_cast<float>(x), static_cast<float>(y));
      }
    g.setColour(n.active ? editor_theme::accentGreen() : (n.favorite ? editor_theme::accentAmber() : editor_theme::neutralGrey()));
    g.fillEllipse(static_cast<float>(x - 8), static_cast<float>(y - 8), 16.0f, 16.0f);
    if (n.id == model_.pendingNode()) {  // queued for the next loop: a static dashed ring, no animation
      juce::Path ring;
      ring.addEllipse(static_cast<float>(x - 11), static_cast<float>(y - 11), 22.0f, 22.0f);
      juce::Path dashed;
      const float dashes[] = {4.0f, 3.0f};
      juce::PathStrokeType(2.0f).createDashedStroke(dashed, ring, dashes, 2);
      g.setColour(cdl::pal().warn);
      g.fillPath(dashed);
    }
    g.setColour(cdl::pal().ink.withAlpha(0.85f));
    g.drawText(n.label.empty() ? juce::String(static_cast<juce::int64>(n.id)) : juce::String(n.label), x + 10, y - 7, 90, 14, juce::Justification::left);
  }
  if (tree.empty()) {
    g.setColour(cdl::pal().inkMuted);
    g.drawText("Each Generate, Mutate and Zoom is saved here as a branch you can return to.", getLocalBounds(), juce::Justification::centred);
  }
}

void HistoryPanel::mouseDown(const juce::MouseEvent& e) {
  grabEditorFocus();
  const auto pt = e.getPosition();
  for (const auto& n : model_.view().tree) {
    const auto np = treePos(n);
    if (std::abs(pt.x - np.x) < 11 && std::abs(pt.y - np.y) < 11) {
      const cf::history::NodeId id = n.id;
      if (e.mods.isPopupMenu()) {
        cmd_.historyMenu(id, this);
        break;
      }
      cmd_.activateNode(id);
      break;
    }
  }
}
