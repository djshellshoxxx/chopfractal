#include "ShellChrome.h"

#include "ControlHelp.h"

#include <algorithm>
#include <cmath>
#include <chopfractal/plugin_host_adapter/parameters.hpp>
#include <initializer_list>

#include "CdlLookAndFeel.h"
#include "DetectDialog.h"
#include "EditorTheme.h"

namespace cf = chopfractal;

ShellChrome::ShellChrome(EditorModel& model, EditorCommands& commands, juce::AudioProcessorValueTreeState& apvts)
    : EditorPanelBase(model, commands, "ShellChrome"), apvts_(apvts), tips_(apvts) {
  for (juce::Button* b : std::initializer_list<juce::Button*>{&load_, &detect_, &zoom_, &collapse_, &lock_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  load_.onClick = [this] { cmd_.chooseFile(); };
  detect_.setTooltip("Find chop points in the sample; with markers already placed it shows the proposals and asks how to merge");
  detect_.onClick = [this] {
    if (cmd_.beginDetect(static_cast<DetectChoice>(model_.detectMode()))) {
      if (auto* top = getParentComponent()) DetectDialog::launch(*top, detect_.getBounds(), model_, cmd_);
    }
  };
  for (juce::Button* b : std::initializer_list<juce::Button*>{&fit_, &undoMarker_, &redoMarker_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  fit_.setTooltip("Show the whole sample in the waveform");
  fit_.onClick = [this] { model_.fitWave(); };
  undoMarker_.setTooltip("Undo the last marker edit");
  undoMarker_.onClick = [this] { cmd_.undoMarkers(); };
  redoMarker_.setTooltip("Redo the marker edit you undid");
  redoMarker_.onClick = [this] { cmd_.redoMarkers(); };
  snap_.addItem("Snap off", 1);
  snap_.addItem("Snap transient", 2);
  snap_.addItem("Snap zero crossing", 3);
  snap_.setTitle("Marker snap");
  snap_.setTooltip("Where a dragged or new marker lands: off, on the nearest transient, or on a zero crossing");
  snap_.setSelectedId(model_.extras().ui.markerSnap + 1, juce::dontSendNotification);
  snap_.onChange = [this] { cmd_.setMarkerSnap(snap_.getSelectedId() - 1); };
  addAndMakeVisible(snap_);
  zoom_.setTooltip("Subdivide the selected pattern hits into nested rhythms. Select hits or waveform sections first.");
  zoom_.onClick = [this] { cmd_.zoomInSelected(); };
  collapse_.onClick = [this] { cmd_.collapseSelected(); };
  lock_.onClick = [this] { cmd_.toggleLockSelectedBar(); };
  addAndMakeVisible(editMode_);
  editMode_.setTooltip("Hits are always editable by dragging; this switch is kept for older projects");
  editMode_.setVisible(false);
  gridSnap_.setTitle("Snap to grid");
  gridSnap_.setTooltip("Hits land on the snap step when moved, resized or added");
  gridSnap_.setToggleState(model_.extras().ui.gridSnap, juce::dontSendNotification);
  gridSnap_.onClick = [this] { cmd_.setGridSnap(gridSnap_.getToggleState()); };
  addAndMakeVisible(gridSnap_);
  snapDivision_.addItemList({"Pattern grid", "1/4", "1/8", "1/16", "1/32"}, 1);
  snapDivision_.setTitle("Snap division");
  snapDivision_.setTooltip("The step hits snap to: the pattern grid or a fixed note value");
  {
    static const int kDiv[] = {0, 4, 8, 16, 32};
    for (int i = 0; i < 5; ++i)
      if (kDiv[i] == model_.extras().ui.snapDivision) snapDivision_.setSelectedId(i + 1, juce::dontSendNotification);
  }
  snapDivision_.onChange = [this] {
    static const int kDiv[] = {0, 4, 8, 16, 32};
    cmd_.setSnapDivision(kDiv[std::max(0, std::min(4, snapDivision_.getSelectedId() - 1))]);
  };
  addAndMakeVisible(snapDivision_);
  editMode_.setToggleState(model_.editMode(), juce::dontSendNotification);
  editMode_.onClick = [this] { model_.setEditMode(editMode_.getToggleState()); };
  bars_.addItemList(juce::StringArray::fromTokens(cf::host::parameterManifest()[cf::host::kPatternBars].choices, "|", ""), 1);
  bars_.setTitle("Pattern length in bars");
  addAndMakeVisible(bars_);
  barsAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts_, "pattern_bars", bars_);
  tips_.bind(bars_, "pattern_bars", control_help::forName("Pattern length in bars"));
  barsLabel_.setFont(cdl::font(cdl::FontRole::Label));
  barsLabel_.setColour(juce::Label::textColourId, cdl::pal().inkMuted);
  addAndMakeVisible(barsLabel_);
  detect_.setEnabled(model_.view().hasSource);
}

juce::String ShellChrome::roleName(cf::DrumRole r, int idx) {
  switch (r) {
    case cf::DrumRole::Kick: return "Kick";
    case cf::DrumRole::Snare: return "Snare";
    case cf::DrumRole::Ghost: return "Ghost";
    case cf::DrumRole::Hat: return "Hat";
    case cf::DrumRole::OpenHat: return "Open hat";
    case cf::DrumRole::Cymbal: return "Cymbal";
    case cf::DrumRole::Perc: return "Perc";
    case cf::DrumRole::Other: return "Other";
    default: return "Chop " + juce::String(idx + 1);
  }
}

const char* ShellChrome::roleLetter(cf::DrumRole r) {
  switch (r) {
    case cf::DrumRole::Kick: return "K";
    case cf::DrumRole::Snare: return "S";
    case cf::DrumRole::Ghost: return "g";
    case cf::DrumRole::Hat: return "H";
    case cf::DrumRole::OpenHat: return "O";
    case cf::DrumRole::Cymbal: return "C";
    case cf::DrumRole::Perc: return "P";
    case cf::DrumRole::Other: return "X";
    default: return "-";
  }
}

void ShellChrome::syncWaveButtons() {
  const bool src = model_.view().hasSource;
  fit_.setEnabled(src && model_.waveZoomed());
  undoMarker_.setEnabled(src && cmd_.canUndoMarkers());
  redoMarker_.setEnabled(src && cmd_.canRedoMarkers());
  snap_.setEnabled(src);
}

void ShellChrome::modelChanged(Change c) {
  switch (c) {
    case Change::View:
      detect_.setEnabled(model_.view().hasSource);
      syncWaveButtons();
      repaint();
      break;
    case Change::Selection:
    case Change::Scroll:
      repaint();
      break;
    case Change::Flags:
    case Change::Extras:
      if (editMode_.getToggleState() != model_.editMode()) editMode_.setToggleState(model_.editMode(), juce::dontSendNotification);
      if (snap_.getSelectedId() != model_.extras().ui.markerSnap + 1) snap_.setSelectedId(model_.extras().ui.markerSnap + 1, juce::dontSendNotification);
      if (gridSnap_.getToggleState() != model_.extras().ui.gridSnap) gridSnap_.setToggleState(model_.extras().ui.gridSnap, juce::dontSendNotification);
      {
        static const int kDiv[] = {0, 4, 8, 16, 32};
        for (int i = 0; i < 5; ++i)
          if (kDiv[i] == model_.extras().ui.snapDivision && snapDivision_.getSelectedId() != i + 1) snapDivision_.setSelectedId(i + 1, juce::dontSendNotification);
      }
      break;
    default:
      break;
  }
}

void ShellChrome::setLayout(const shell::ShellRects& r, bool advanced) {
  rects_ = r;
  advanced_ = advanced;
  layoutWidgets();
  repaint();
}

void ShellChrome::resized() { layoutWidgets(); }

void ShellChrome::layoutWidgets() {
  const float s = rects_.scale;
  auto px = [s](float v) { return static_cast<int>(std::lround(v * s)); };
  const int gap = px(6);
  {
    auto t = rects_.sourceParts.toolbar.withTrimmedLeft(px(84));
    load_.setBounds(t.removeFromLeft(px(104)).reduced(0, px(2)));
    t.removeFromLeft(gap);
    detect_.setBounds(t.removeFromLeft(px(124)).reduced(0, px(2)));
    // Marker tools, as many as fit: the chop count on the right needs about 90 px.
    t.removeFromRight(px(96));
    struct Slot { juce::Component* c; int w; };
    for (const Slot& slot : {Slot{&fit_, 44}, Slot{&undoMarker_, 112}, Slot{&redoMarker_, 112}, Slot{&snap_, 128}}) {
      const int w = px(static_cast<float>(slot.w));
      t.removeFromLeft(gap);
      const bool fits = t.getWidth() >= w;
      slot.c->setVisible(fits);
      if (fits) slot.c->setBounds(t.removeFromLeft(w).reduced(0, px(2)));
    }
  }
  {
    auto t = rects_.patternParts.header;
    t.removeFromLeft(px(84));
    barsLabel_.setBounds(t.removeFromLeft(px(48)));
    bars_.setBounds(t.removeFromLeft(px(64)).reduced(0, px(2)));
    t.removeFromLeft(gap);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&zoom_, &collapse_, &lock_}) {
      c->setVisible(advanced_);
      c->setBounds(t.removeFromLeft(px(70)).reduced(0, px(2)));
      t.removeFromLeft(gap);
    }
    // Snap controls on the right of the header, as many as fit.
    for (const auto& slot : {std::pair<juce::Component*, int>{&gridSnap_, 62}, std::pair<juce::Component*, int>{&snapDivision_, 104}}) {
      const int w = px(static_cast<float>(slot.second));
      const bool fits = t.getWidth() >= w;
      slot.first->setVisible(fits);
      if (fits) slot.first->setBounds(t.removeFromLeft(w).reduced(0, px(2)));
      t.removeFromLeft(gap);
    }
  }
}

void ShellChrome::mouseDown(const juce::MouseEvent& e) {
  const auto pt = e.getPosition();
  const auto& v = model_.view();
  const auto& lh = rects_.patternParts.laneHeaders;
  const auto& grid = rects_.patternParts.grid;
  if (lh.contains(pt) && !v.chops.empty()) {
    grabEditorFocus();
    const auto lanes = model_.lanes();
    const int lane = std::max(0, cf::ui::laneAtScrolledY(lanes, static_cast<double>(pt.y - grid.getY())));
    const float laneH = static_cast<float>(lanes.laneHeight);
    const juce::Rectangle<int> band(0, static_cast<int>(cf::ui::laneTop(lanes, lane)), grid.getWidth(), std::max(1, static_cast<int>(laneH)));
    model_.selectSectionsInPattern(band, band.getCentre(), false, {0, 0, grid.getWidth(), grid.getHeight()});
    return;
  }
  const auto& strip = rects_.sourceParts.chopStrip;
  if (strip.contains(pt) && v.sourceFrames > 0) {
    grabEditorFocus();
    // The strip is aligned with the (possibly zoomed) waveform; the selection helper wants whole-clip pixels.
    auto win = model_.waveWindow();
    win.widthPx = static_cast<double>(std::max(1, strip.getWidth()));
    const std::int64_t frame = cf::ui::xToFrame(win, static_cast<double>(pt.x - strip.getX()));
    const int fullX = static_cast<int>(static_cast<double>(frame) / static_cast<double>(std::max<std::int64_t>(1, v.sourceFrames)) * strip.getWidth());
    model_.selectSectionsInWave(fullX, fullX, true, strip.getWidth());
    for (const auto id : model_.selectedChops()) {
      cmd_.audition(id);
      break;
    }
    model_.rebuild();
  }
}

void ShellChrome::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  const float s = rects_.scale;
  const auto& v = model_.view();
  auto panel = [&](juce::Rectangle<int> r) {
    g.setColour(p.panel);
    g.fillRoundedRectangle(r.toFloat(), 4.0f);
    g.setColour(p.edge);
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 4.0f, 1.0f);
  };
  panel(rects_.source);
  panel(rects_.pattern);

  // ---- source ----
  {
    const auto& sp = rects_.sourceParts;
    cdl::paintSectionLabel(g, "Source", sp.toolbar.withWidth(static_cast<int>(84 * s)));
    g.setColour(p.inkMuted);
    g.setFont(cdl::font(cdl::FontRole::Hint, s));
    g.drawText(v.hasSource ? juce::String(v.chops.size()) + (v.chops.size() == 1 ? " chop" : " chops") : juce::String("no sample loaded"), sp.toolbar, juce::Justification::centredRight, false);
    cdl::paintTrace(g, static_cast<float>(sp.toolbar.getX()), static_cast<float>(sp.toolbar.getRight()), static_cast<float>(sp.toolbar.getBottom()) - 1.0f,
                    static_cast<float>(sp.toolbar.getX()) + 0.3f * static_cast<float>(sp.toolbar.getWidth()));
    // ruler: a tick and the chop number at every chop start, aligned with the (possibly zoomed) waveform
    auto win = model_.waveWindow();
    win.widthPx = static_cast<double>(std::max(1, sp.wave.getWidth()));
    if (win.endFrame <= win.startFrame) win.endFrame = win.startFrame + 1;
    if (v.sourceFrames > 0) {
      g.setFont(cdl::font(cdl::FontRole::Value, 0.9f * s));
      for (std::size_t i = 0; i < v.chops.size(); ++i) {
        const double fx = cf::ui::frameToX(win, v.chops[i].range.start);
        if (fx < 0.0 || fx > sp.wave.getWidth()) continue;
        const int x = sp.wave.getX() + static_cast<int>(fx);
        g.setColour(p.edgeHi);
        g.fillRect(x, sp.ruler.getBottom() - 4, 1, 4);
        g.setColour(p.inkMuted);
        g.drawText(juce::String(i + 1), x + 3, sp.ruler.getY(), 24, sp.ruler.getHeight(), juce::Justification::centredLeft, false);
      }
    }
    // chop strip
    const auto selected = model_.selectedChops();
    juce::Graphics::ScopedSaveState stripClip(g);
    g.reduceClipRegion(sp.chopStrip);
    for (std::size_t i = 0; i < v.chops.size() && v.sourceFrames > 0; ++i) {
      const auto& c = v.chops[i];
      const float x0 = static_cast<float>(sp.chopStrip.getX()) + static_cast<float>(cf::ui::frameToX(win, c.range.start) * sp.chopStrip.getWidth() / win.widthPx);
      const float x1 = static_cast<float>(sp.chopStrip.getX()) + static_cast<float>(cf::ui::frameToX(win, c.range.end) * sp.chopStrip.getWidth() / win.widthPx);
      if (x1 < static_cast<float>(sp.chopStrip.getX()) || x0 > static_cast<float>(sp.chopStrip.getRight())) continue;
      const auto cell = juce::Rectangle<float>(x0, static_cast<float>(sp.chopStrip.getY()) + 4.0f, std::max(2.0f, x1 - x0 - 2.0f), static_cast<float>(sp.chopStrip.getHeight()) - 4.0f);
      const auto lane = editor_theme::laneColour(static_cast<int>(i));
      const bool sel = std::find(selected.begin(), selected.end(), c.id) != selected.end();
      g.setColour(lane.withAlpha(sel ? 0.40f : 0.18f));
      g.fillRoundedRectangle(cell, 2.0f);
      g.setColour(lane);
      g.fillRect(cell.withHeight(2.0f));
      if (sel) g.drawRoundedRectangle(cell.reduced(0.5f), 2.0f, 1.0f);
      if (cell.getWidth() > 26.0f) {
        g.setColour(p.ink);
        g.setFont(cdl::font(cdl::FontRole::Value, s));
        g.drawText(c.role == cf::DrumRole::Unknown ? juce::String(i + 1) : juce::String(i + 1) + " " + roleLetter(c.role), cell.toNearestInt().reduced(4, 0), juce::Justification::centredLeft, false);
      } else if (cell.getWidth() > 10.0f) {
        g.setColour(p.ink);
        g.setFont(cdl::font(cdl::FontRole::Value, 0.9f * s));
        g.drawText(juce::String(i + 1), cell.toNearestInt(), juce::Justification::centred, false);
      }
    }
  }

  // ---- pattern ----
  {
    const auto& pp = rects_.patternParts;
    cdl::paintSectionLabel(g, "Pattern", pp.header.withWidth(static_cast<int>(84 * s)));
    cdl::paintTrace(g, static_cast<float>(pp.header.getX()), static_cast<float>(pp.header.getRight()), static_cast<float>(pp.header.getBottom()) - 1.0f,
                    static_cast<float>(pp.header.getX()) + 0.45f * static_cast<float>(pp.header.getWidth()));
    const auto& pat = v.pattern;
    g.setFont(cdl::font(cdl::FontRole::Value, 0.9f * s));
    for (double bx : pat.beatLines) {
      g.setColour(p.edge);
      g.fillRect(pp.grid.getX() + static_cast<int>(bx * pp.grid.getWidth()), pp.ruler.getBottom() - 3, 1, 3);
    }
    for (std::size_t b = 0; b < pat.barLines.size(); ++b) {
      const int x = pp.grid.getX() + static_cast<int>(pat.barLines[b] * pp.grid.getWidth());
      g.setColour(p.edgeHi);
      g.fillRect(x, pp.ruler.getY() + 2, 1, pp.ruler.getHeight() - 2);
      g.setColour(p.inkMuted);
      g.drawText(juce::String(b + 1), x + 4, pp.ruler.getY(), 28, pp.ruler.getHeight(), juce::Justification::centredLeft, false);
    }
    // lane headers, one per role; they scroll together with the grid and are clipped to their column
    if (!v.chops.empty()) {
      const auto lanes = model_.lanes();
      const float laneH = static_cast<float>(lanes.laneHeight > 0.0 ? lanes.laneHeight : pp.grid.getHeight() / static_cast<double>(v.chops.size()));
      const float scroll = static_cast<float>(lanes.scroll);
      juce::Graphics::ScopedSaveState laneClip(g);
      g.reduceClipRegion(pp.laneHeaders);
      std::vector<bool> laneSelected(v.chops.size(), false);
      for (const auto& r : pat.rects)
        if (r.id == model_.selected() || std::find(model_.selectedHits().begin(), model_.selectedHits().end(), r.id) != model_.selectedHits().end())
          if (r.chopIndex >= 0 && r.chopIndex < static_cast<int>(laneSelected.size())) laneSelected[static_cast<std::size_t>(r.chopIndex)] = true;
      for (std::size_t i = 0; i < v.chops.size(); ++i) {
        const auto row = juce::Rectangle<float>(static_cast<float>(pp.laneHeaders.getX()), static_cast<float>(pp.grid.getY()) + static_cast<float>(i) * laneH - scroll,
                                                static_cast<float>(pp.laneHeaders.getWidth()) - 6.0f, laneH);
        const auto lane = editor_theme::laneColour(static_cast<int>(i));
        if (laneSelected[i]) {
          g.setColour(p.sprayWash);
          g.fillRect(row);
        }
        g.setColour(lane);
        g.fillRect(row.getX(), row.getY() + 2.0f, 5.0f, std::max(2.0f, row.getHeight() - 4.0f));
        if (row.getHeight() >= 12.0f) {
          g.setColour(laneSelected[i] ? p.sprayHi : p.ink);
          g.setFont(cdl::font(cdl::FontRole::Value, std::min(1.0f, row.getHeight() / 18.0f) * s));
          g.drawText(juce::String(i + 1), row.withTrimmedLeft(10.0f).withWidth(22.0f), juce::Justification::centredLeft, false);
          g.setFont(cdl::font(cdl::FontRole::Body, std::min(1.0f, row.getHeight() / 20.0f) * s));
          g.drawText(roleName(v.chops[i].role, static_cast<int>(i)), row.withTrimmedLeft(34.0f), juce::Justification::centredLeft, true);
        }
      }
    }
  }
}

void ShellChrome::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
  const auto pt = e.getPosition();
  if (rects_.patternParts.laneHeaders.contains(pt)) {  // wheel over the lane headers scrolls the lanes
    model_.setLaneScroll(model_.laneScroll() - static_cast<double>(wheel.deltaY) * 3.0 * model_.lanes().laneHeight);
    return;
  }
  const auto& strip = rects_.sourceParts.chopStrip;
  if (strip.contains(pt) && model_.view().sourceFrames > 0) {  // wheel over the chop strip zooms / scrolls the waveform like the waveform itself
    if (e.mods.isShiftDown() || std::fabs(wheel.deltaX) > std::fabs(wheel.deltaY))
      model_.scrollWaveBy(-(std::fabs(wheel.deltaX) > std::fabs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY) * strip.getWidth() * 0.5f);
    else
      model_.zoomWaveAround(static_cast<double>(pt.x - strip.getX()), std::pow(1.25, static_cast<double>(wheel.deltaY) * 4.0));
  }
}
