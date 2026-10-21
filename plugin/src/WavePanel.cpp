#include "WavePanel.h"

#include <algorithm>
#include <cmath>

#include "CdlLookAndFeel.h"
#include "EditorTheme.h"

namespace cf = chopfractal;

namespace {
constexpr int kHandlePx = 12;        // marker hit zone
constexpr int kDragStartPx = 3;
constexpr int kScrollBarH = 6;
}  // namespace

WavePanel::WavePanel(EditorModel& model, EditorCommands& commands) : EditorPanelBase(model, commands, "WavePanel") {
  setOpaque(true);
  setTitle("Source waveform");
  setWantsKeyboardFocus(false);
}

void WavePanel::modelChanged(Change c) {
  if (c == Change::View || c == Change::Selection || c == Change::Detect) repaint();
}

cf::ui::ViewWindow WavePanel::window() const {
  auto w = model_.waveWindow();
  w.widthPx = static_cast<double>(std::max(1, getWidth()));
  if (w.endFrame <= w.startFrame) w.endFrame = w.startFrame + 1;
  return w;
}

std::int64_t WavePanel::frameAtX(double x) const {
  const auto w = window();
  return std::clamp<std::int64_t>(cf::ui::xToFrame(w, x), 0, std::max<std::int64_t>(0, model_.view().sourceFrames - 1));
}

double WavePanel::framesPerPixel() const {
  const auto w = window();
  return static_cast<double>(w.endFrame - w.startFrame) / w.widthPx;
}

cf::ChopId WavePanel::markerAtX(double x, double tolerancePx) const {
  const auto w = window();
  cf::ChopId best;
  double bestDist = std::max(tolerancePx, 0.0) + 1e-9;
  for (const auto& m : model_.view().markers) {
    if (!m.visible) continue;
    const double mx = cf::ui::frameToX(w, m.position);
    const double d = std::fabs(mx - x);
    if (d <= bestDist) {
      bestDist = d;
      best = m.id;
    }
  }
  return best;
}

juce::Rectangle<float> WavePanel::scrollThumb() const {
  if (!model_.waveZoomed() || model_.view().sourceFrames <= 0) return {};
  const auto& w = model_.waveWindow();
  const double total = static_cast<double>(model_.view().sourceFrames);
  const float width = static_cast<float>(getWidth());
  const float x0 = static_cast<float>(static_cast<double>(w.startFrame) / total) * width;
  const float x1 = static_cast<float>(static_cast<double>(w.endFrame) / total) * width;
  return {x0, static_cast<float>(getHeight() - kScrollBarH), std::max(12.0f, x1 - x0), static_cast<float>(kScrollBarH)};
}

void WavePanel::paint(juce::Graphics& g) {
  const auto& v = model_.view();
  const auto area = getLocalBounds();
  const auto w = window();
  const auto& p = cdl::pal();
  g.setFont(cdl::font(cdl::FontRole::Body, cdl::uiScale()));
  g.setColour(editor_theme::background());
  g.fillRect(area);
  const float height = static_cast<float>(area.getHeight());
  auto xOf = [&](std::int64_t frame) { return static_cast<float>(cf::ui::frameToX(w, frame)); };
  // Chop lanes: each chop's range tinted in its lane colour with a top bar, so waveform, chop strip and pattern lanes read as one.
  if (v.sourceFrames > 0)
    for (std::size_t i = 0; i < v.chops.size(); ++i) {
      const auto& chop = v.chops[i];
      const float x = xOf(chop.range.start), x1 = xOf(chop.range.end);
      if (x1 < 0.0f || x > static_cast<float>(area.getWidth())) continue;
      const auto lane = editor_theme::laneColour(static_cast<int>(i));
      g.setColour(lane.withAlpha(0.14f));
      g.fillRect(x, 0.0f, x1 - x, height);
      g.setColour(lane);
      g.fillRect(x, 0.0f, x1 - x, 2.0f);
    }
  if (!v.peaks.min.empty()) {
    g.setColour(editor_theme::accentBlue());
    const int n = static_cast<int>(v.peaks.min.size());
    const float cy = static_cast<float>(area.getCentreY());
    const float h = height * 0.45f;
    for (int i = 0; i < n; ++i)
      g.drawVerticalLine(i, cy - v.peaks.max[static_cast<std::size_t>(i)] * h, cy - v.peaks.min[static_cast<std::size_t>(i)] * h);
  } else {
    g.setColour(p.inkMuted);
    g.drawText(model_.sourceMissing() ? "The project's audio file was not found. Press Locate Source (your markers, pattern and history are kept)."
                                      : "Drop an audio sample here, or press Load Loop. Then Detect Chops, Generate and Play.",
               area, juce::Justification::centred);
  }
  // Waveform selections correspond to complete chop sections, not destructive source trims.
  const auto& selectedChops = model_.selectedChops();
  for (const auto& chop : v.chops)
    if (std::find(selectedChops.begin(), selectedChops.end(), chop.id) != selectedChops.end() && v.sourceFrames > 0) {
      g.setColour(p.spray.withAlpha(0.28f));
      g.fillRect(xOf(chop.range.start), 0.0f, xOf(chop.range.end) - xOf(chop.range.start), height);
    }
  // Trimmed frames of each chop: a darker overlay over what the chop no longer plays.
  for (std::size_t i = 0; i < v.markers.size(); ++i) {
    const auto& m = v.markers[i];
    if (!m.enabled || (m.trimStart == 0 && m.trimEnd == 0)) continue;
    std::int64_t regionEnd = v.sourceFrames;
    for (std::size_t j = i + 1; j < v.markers.size(); ++j)
      if (v.markers[j].enabled) {
        regionEnd = v.markers[j].position;
        break;
      }
    g.setColour(p.bg.withAlpha(0.62f));
    if (m.trimStart > 0) g.fillRect(xOf(m.position), 0.0f, xOf(m.position + m.trimStart) - xOf(m.position), height);
    if (m.trimEnd > 0) g.fillRect(xOf(regionEnd - m.trimEnd), 0.0f, xOf(regionEnd) - xOf(regionEnd - m.trimEnd), height);
  }
  // The selected marker's chop region gets a light wash.
  if (model_.selectedMarker().valid())
    for (std::size_t i = 0; i < v.markers.size(); ++i)
      if (v.markers[i].id == model_.selectedMarker()) {
        std::int64_t regionEnd = v.sourceFrames;
        for (std::size_t j = i + 1; j < v.markers.size(); ++j)
          if (v.markers[j].enabled) {
            regionEnd = v.markers[j].position;
            break;
          }
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.fillRect(xOf(v.markers[i].position), 0.0f, xOf(regionEnd) - xOf(v.markers[i].position), height);
      }
  // Proposed markers from Detect Chops (not applied yet): dashed lines in the rain colour, a diamond cue at the top.
  const auto& preview = model_.detectPreview();
  if (preview.active)
    for (const auto frame : preview.frames) {
      const float x = xOf(frame);
      if (x < 0.0f || x > static_cast<float>(area.getWidth())) continue;
      g.setColour(p.rain);
      for (float y = 0.0f; y < height; y += 6.0f) g.fillRect(x - 0.5f, y, 1.0f, 3.0f);
      juce::Path d;
      d.addQuadrilateral(x, 1.0f, x + 4.0f, 5.0f, x, 9.0f, x - 4.0f, 5.0f);
      g.fillPath(d);
    }
  int markerNumber = 0;
  g.setFont(cdl::font(cdl::FontRole::Value, 0.9f * cdl::uiScale()));
  for (const auto& m : v.markers) {
    ++markerNumber;
    const bool dragged = dragging_ && m.id == dragMarker_;
    const bool selected = m.id == model_.selectedMarker();
    const float x = dragged ? xOf(dragFrame_) : static_cast<float>(m.x);
    if (x < -2.0f || x > static_cast<float>(area.getWidth()) + 2.0f) continue;
    const juce::Colour colour = m.enabled ? editor_theme::accentAmber() : p.inkMuted;
    g.setColour(selected || dragged ? p.ink : colour);
    const float lineW = selected || dragged ? 2.0f : 1.0f;
    if (m.enabled) {
      g.fillRect(x - lineW / 2.0f, 0.0f, lineW, height);
    } else {  // disabled markers are dashed as well as grey
      for (float y = 0.0f; y < height; y += 5.0f) g.fillRect(x - lineW / 2.0f, y, lineW, 3.0f);
    }
    // Manual markers: filled square; detected ones: triangle. Shape, not only colour.
    g.setColour(colour);
    if (m.manual) {
      g.fillRect(x - 3.0f, 0.0f, 6.0f, 6.0f);
    } else {
      juce::Path t;
      t.addTriangle(x - 4.0f, 0.0f, x + 4.0f, 0.0f, x, 7.0f);
      g.fillPath(t);
    }
    const juce::String text = m.label.empty() ? juce::String(markerNumber) : juce::String(m.label);
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(g.getCurrentFont(), text, 0.0f, 0.0f);
    const int tw = std::min(90, static_cast<int>(glyphs.getBoundingBox(0, -1, true).getWidth()) + 8);
    if (selected || dragged) {
      g.setColour(p.ink);
      g.fillRect(static_cast<int>(x) + 3, 8, tw, 14);
      g.setColour(p.bg);
    } else {
      g.setColour(colour);
    }
    g.drawText(text, static_cast<int>(x) + 7, 8, tw - 4, 14, juce::Justification::left, true);
  }
  if (selecting_) {
    const auto rectangle = juce::Rectangle<int>(start_, end_);
    g.setColour(editor_theme::accentBlue().withAlpha(0.2f));
    g.fillRect(rectangle);
    g.setColour(editor_theme::accentBlue());
    g.drawRect(rectangle, 1);
  }
  if (const auto thumb = scrollThumb(); !thumb.isEmpty()) {
    g.setColour(p.edge);
    g.fillRect(0.0f, thumb.getY(), static_cast<float>(area.getWidth()), thumb.getHeight());
    g.setColour(p.inkMuted);
    g.fillRoundedRectangle(thumb.reduced(0.0f, 1.0f), 2.0f);
  }
}

void WavePanel::updateSelection(juce::Point<int> point) {
  end_ = {juce::jlimit(0, getWidth() - 1, point.x), juce::jlimit(0, getHeight() - 1, point.y)};
  const auto rect = juce::Rectangle<int>(start_, end_);
  const bool click = rect.getWidth() < 3 && rect.getHeight() < 3;
  // The selection helper maps x over the whole clip; with a zoom window the pixels are translated to whole-clip pixels first.
  const auto w = window();
  const std::int64_t total = std::max<std::int64_t>(1, model_.view().sourceFrames);
  auto toFull = [&](int x) { return static_cast<int>(std::llround(static_cast<double>(cf::ui::xToFrame(w, x)) / static_cast<double>(total) * getWidth())); };
  model_.selectSectionsInWave(toFull(start_.x), toFull(end_.x), click, getWidth());
  repaint();
}

void WavePanel::mouseDown(const juce::MouseEvent& e) {
  const auto pt = e.getPosition();
  const std::int64_t frames = model_.view().sourceFrames;
  selecting_ = dragging_ = panning_ = scrollDrag_ = false;
  dragMarker_ = cf::ChopId{};
  if (frames <= 0) return;
  grabEditorFocus();
  if (e.mods.isPopupMenu()) {
    showContextMenu(pt);
    return;
  }
  if (const auto thumb = scrollThumb(); !thumb.isEmpty() && pt.y >= getHeight() - kScrollBarH - 2) {  // the scroll bar of a zoomed waveform
    scrollDrag_ = true;
    panAnchorX_ = pt.x;
    if (!thumb.contains(pt.toFloat())) {  // a click beside the thumb centres it there
      const double total = static_cast<double>(frames);
      const double span = static_cast<double>(model_.waveWindow().endFrame - model_.waveWindow().startFrame);
      const std::int64_t centre = static_cast<std::int64_t>(static_cast<double>(pt.x) / getWidth() * total);
      model_.setWaveWindow(centre - static_cast<std::int64_t>(span / 2), centre + static_cast<std::int64_t>(span / 2));
    }
    return;
  }
  if (e.mods.isMiddleButtonDown()) {
    panning_ = true;
    panAnchorX_ = pt.x;
    return;
  }
  if (e.mods.isAltDown()) {  // Alt-click removes the nearest marker
    cmd_.removeMarkerNear(static_cast<double>(pt.x), kHandlePx);
    return;
  }
  if (e.mods.isShiftDown()) {  // Shift-click adds a marker at the snapped frame
    cmd_.addMarkerAt(cmd_.snapFrame(frameAtX(pt.x), framesPerPixel(), e.mods.isCommandDown()));
    return;
  }
  if (const auto marker = markerAtX(pt.x, kHandlePx / 2.0); marker.valid()) {
    dragMarker_ = marker;
    pressX_ = pt.x;
    dragFrame_ = frameAtX(pt.x);
    for (const auto& m : model_.view().markers)
      if (m.id == marker) dragFrame_ = m.position;
    model_.selectMarker(marker);
    repaint();
    return;
  }
  model_.selectMarker(cf::ChopId{});
  selecting_ = true;
  start_ = end_ = pt;
  updateSelection(pt);
  for (const auto id : model_.selectedChops()) {
    cmd_.audition(id);
    break;
  }
}

void WavePanel::mouseDrag(const juce::MouseEvent& e) {
  const auto pt = e.getPosition();
  if (scrollDrag_) {
    const double total = static_cast<double>(std::max<std::int64_t>(1, model_.view().sourceFrames));
    model_.scrollWaveBy((pt.x - panAnchorX_) * total / static_cast<double>(std::max(1, getWidth())) / framesPerPixel());
    panAnchorX_ = pt.x;
    return;
  }
  if (panning_) {
    model_.scrollWaveBy(panAnchorX_ - pt.x);
    panAnchorX_ = pt.x;
    return;
  }
  if (dragMarker_.valid()) {
    if (!dragging_ && std::abs(pt.x - pressX_) <= kDragStartPx) return;
    dragging_ = true;
    const auto& markers = model_.view().markers;
    std::int64_t prev = 0, next = model_.view().sourceFrames;
    std::int64_t own = 0;
    for (const auto& m : markers) {
      if (m.id == dragMarker_) own = m.position;
    }
    for (const auto& m : markers) {
      if (m.id == dragMarker_) continue;
      if (m.position < own) prev = std::max(prev, m.position);
      if (m.position > own) next = std::min(next, m.position);
    }
    std::int64_t frame = frameAtX(pt.x);
    frame = cmd_.snapFrame(frame, framesPerPixel(), e.mods.isCommandDown());
    dragFrame_ = cf::ui::clampMarkerFrame(frame, prev, next, cf::source::kDefaultMinRegionFrames);
    repaint();
    return;
  }
  if (selecting_) updateSelection(pt);
}

void WavePanel::mouseUp(const juce::MouseEvent& e) {
  if (scrollDrag_ || panning_) {
    scrollDrag_ = panning_ = false;
    return;
  }
  if (dragMarker_.valid()) {
    const auto id = dragMarker_;
    const bool moved = dragging_;
    const std::int64_t frame = dragFrame_;
    dragMarker_ = cf::ChopId{};
    dragging_ = false;
    if (moved) {
      bool changed = true;
      for (const auto& m : model_.view().markers) changed = changed && !(m.id == id && m.position == frame);
      if (changed) cmd_.moveMarker(id, frame);
    } else {  // a click on a marker selects it and auditions its chop
      cmd_.audition(id);
    }
    repaint();
    return;
  }
  if (!selecting_) return;
  updateSelection(e.getPosition());
  selecting_ = false;
  model_.rebuild();
}

void WavePanel::mouseMove(const juce::MouseEvent& e) {
  const auto marker = markerAtX(e.position.x, kHandlePx / 2.0);
  setMouseCursor(marker.valid() ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
  hoverMarker_ = marker;
}

void WavePanel::mouseDoubleClick(const juce::MouseEvent& e) {
  const std::int64_t frames = model_.view().sourceFrames;
  if (frames <= 0 || e.mods.isPopupMenu()) return;
  const auto pt = e.getPosition();
  if (const auto marker = markerAtX(pt.x, kHandlePx / 2.0); marker.valid()) {  // double-click on a marker names it
    askRename(marker);
    return;
  }
  cmd_.addMarkerAt(cmd_.snapFrame(frameAtX(pt.x), framesPerPixel(), e.mods.isCommandDown()));  // double-click on empty waveform adds a marker
}

void WavePanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
  if (model_.view().sourceFrames <= 0) return;
  const bool horizontal = std::fabs(wheel.deltaX) > std::fabs(wheel.deltaY);
  if (e.mods.isShiftDown() || horizontal) {
    const float d = horizontal ? wheel.deltaX : wheel.deltaY;
    model_.scrollWaveBy(-d * getWidth() * 0.5f);
    return;
  }
  model_.zoomWaveAround(e.position.x, std::pow(1.25, static_cast<double>(wheel.deltaY) * 4.0));
}

// ---------------------------------------------------------------------------------------------
// menus
// ---------------------------------------------------------------------------------------------

juce::PopupMenu WavePanel::buildMarkerMenu(cf::ChopId id, std::int64_t) const {
  juce::PopupMenu m;
  const cf::ui::MarkerView* marker = nullptr;
  for (const auto& mv : model_.view().markers)
    if (mv.id == id) marker = &mv;
  const bool enabled = marker && marker->enabled;
  m.addItem(kEnable, enabled ? "Disable chop" : "Enable chop");
  m.addItem(kRename, "Rename marker...");
  if (enabled) {  // trimming a disabled marker is not supported by the engine
    m.addItem(kTrimStart, "Trim start here");
    m.addItem(kTrimEnd, "Trim end here");
    m.addItem(kClearTrim, "Clear trim", marker && (marker->trimStart > 0 || marker->trimEnd > 0));
  }
  m.addSeparator();
  const int snap = model_.extras().ui.markerSnap;
  m.addItem(kSnapOff, "Snap: Off", true, snap == 0);
  m.addItem(kSnapTransient, "Snap: Transient", true, snap == 1);
  m.addItem(kSnapZero, "Snap: Zero crossing", true, snap == 2);
  m.addSeparator();
  m.addItem(kDelete, "Delete marker");
  return m;
}

juce::PopupMenu WavePanel::buildEmptyMenu() const {
  juce::PopupMenu m;
  m.addItem(kAdd, "Add marker here");
  m.addItem(kFit, "Zoom to fit", model_.waveZoomed());
  return m;
}

void WavePanel::showContextMenu(juce::Point<int> where) {
  const auto marker = markerAtX(where.x, kHandlePx / 2.0);
  const std::int64_t frame = frameAtX(where.x);
  if (marker.valid()) model_.selectMarker(marker);
  juce::Component::SafePointer<WavePanel> alive(this);
  auto menu = marker.valid() ? buildMarkerMenu(marker, frame) : buildEmptyMenu();
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(juce::Rectangle<int>(where.x, where.y, 1, 1).translated(getScreenX(), getScreenY())),
                     [alive, marker, frame](int choice) {
                       if (alive != nullptr && choice != 0) alive->handleMenuChoice(choice, marker, frame);
                     });
}

void WavePanel::handleMenuChoice(int choice, cf::ChopId id, std::int64_t atFrame) {
  const cf::ui::MarkerView* marker = nullptr;
  std::int64_t regionEnd = model_.view().sourceFrames;
  const auto& markers = model_.view().markers;
  for (std::size_t i = 0; i < markers.size(); ++i)
    if (markers[i].id == id) {
      marker = &markers[i];
      for (std::size_t j = i + 1; j < markers.size(); ++j)
        if (markers[j].enabled) {
          regionEnd = markers[j].position;
          break;
        }
    }
  switch (choice) {
    case kEnable:
      if (marker) cmd_.setMarkerEnabled(id, !marker->enabled);
      break;
    case kRename:
      askRename(id);
      break;
    case kTrimStart:
      if (marker) cmd_.trimMarker(id, static_cast<std::uint32_t>(std::max<std::int64_t>(0, atFrame - marker->position)), marker->trimEnd);
      break;
    case kTrimEnd:
      if (marker) cmd_.trimMarker(id, marker->trimStart, static_cast<std::uint32_t>(std::max<std::int64_t>(0, regionEnd - atFrame)));
      break;
    case kClearTrim:
      cmd_.trimMarker(id, 0, 0);
      break;
    case kSnapOff: cmd_.setMarkerSnap(0); break;
    case kSnapTransient: cmd_.setMarkerSnap(1); break;
    case kSnapZero: cmd_.setMarkerSnap(2); break;
    case kDelete:
      cmd_.deleteMarker(id);
      break;
    case kAdd:
      cmd_.addMarkerAt(cmd_.snapFrame(atFrame, framesPerPixel(), false));
      break;
    case kFit:
      model_.fitWave();
      break;
    default:
      break;
  }
}

void WavePanel::askRename(cf::ChopId id) {
  juce::String current;
  for (const auto& m : model_.view().markers)
    if (m.id == id) current = juce::String(m.label);
  auto* w = new juce::AlertWindow("Marker name", "Name this marker (empty clears it):", juce::MessageBoxIconType::NoIcon);
  w->addTextEditor("name", current);
  w->addButton("OK", 1);
  w->addButton("Cancel", 0);
  juce::Component::SafePointer<WavePanel> alive(this);
  w->enterModalState(true, juce::ModalCallbackFunction::create([alive, w, id](int r) {
                       if (r == 1 && alive != nullptr) alive->cmd_.renameMarker(id, w->getTextEditorContents("name").toStdString());
                     }),
                     true);
}
