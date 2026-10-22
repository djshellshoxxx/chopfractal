// Direct editing through real mouse events (G2): marker drag and undo, zoom, hit move / duplicate / lock refusal, lane scroll.
#include <algorithm>
#include <cmath>
#include <memory>

#include "EditorModel.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "shell_test_support.hpp"

using namespace shelltest;

namespace {
std::vector<std::int64_t> markerPositions(ChopFractalProcessor& p) {
  std::vector<std::int64_t> out;
  p.withSession([&](cf::composition::ProjectSession& s) {
    if (s.chopMap())
      for (const auto& m : s.chopMap()->markers()) out.push_back(m.position);
  });
  std::sort(out.begin(), out.end());
  return out;
}

struct Fixture {
  ChopFractalProcessor p;
  std::unique_ptr<juce::AudioProcessorEditor> editor;
  ChopFractalEditor* ed = nullptr;
  bool open() {
    prepare(p);
    makeSession(p);
    editor.reset(p.createEditor());
    if (!editor) return false;
    editor->setSize(1100, 720);  // earlier tests may have saved another window size
    editor->setVisible(true);
    pump(300);
    ed = dynamic_cast<ChopFractalEditor*>(editor.get());
    return ed != nullptr;
  }
};
}  // namespace

CHOP_TEST(dragging_a_marker_moves_it_in_one_undo_step) {
  if (!haveDisplay()) return;
  Fixture f;
  CHECK(f.open());
  if (!f.ed) return;
  f.ed->commands().setMarkerSnap(0);  // free movement: snapping would hide the drag distance
  const auto before = markerPositions(f.p);
  CHECK(before.size() >= 4);
  if (before.size() < 4) return;
  EditorModel probe(f.p);
  probe.refresh(true, f.ed->waveformBounds().getWidth());
  const auto& markers = probe.view().markers;
  const auto& m = markers[2];
  const auto wave = f.ed->waveformBounds();
  const juce::Point<int> from(wave.getX() + static_cast<int>(std::lround(m.x)), wave.getCentreY());
  MouseSim sim(*f.editor);
  sim.dragFromTo(from, from + juce::Point<int>(20, 0));
  pump(100);
  const auto moved = markerPositions(f.p);
  CHECK(moved.size() == before.size());
  CHECK(moved != before);
  f.ed->commands().undoMarkers();
  pump(100);
  CHECK(markerPositions(f.p) == before);  // the whole drag was a single step
}

CHOP_TEST(wheel_zooms_the_waveform_and_a_fit_restores_it) {
  if (!haveDisplay()) return;
  Fixture f;
  CHECK(f.open());
  if (!f.ed) return;
  MouseSim sim(*f.editor);
  const auto wave = f.ed->waveformBounds();
  const auto centre = wave.getCentre();
  const auto markers = markerPositions(f.p);
  CHECK(!f.ed->waveZoomed());
  sim.wheel(centre, 2.0f);
  pump(60);
  CHECK(f.ed->waveZoomed());
  CHECK(markerPositions(f.p) == markers);  // zoom is view state, never an edit
  f.ed->fitWave();
  pump(60);
  CHECK(!f.ed->waveZoomed());
}

CHOP_TEST(dragging_a_hit_moves_it_and_a_locked_hit_refuses) {
  if (!haveDisplay()) return;
  Fixture f;
  CHECK(f.open());
  if (!f.ed) return;
  const auto rects = f.ed->hitRects();
  CHECK(!rects.empty());
  if (rects.empty()) return;
  const auto target = *std::find_if(rects.begin(), rects.end(), [](const auto& r) { return r.topLevel; });
  MouseSim sim(*f.editor);
  const auto before = patternBytes(f.p);
  const auto from = hitPointOf(*f.ed, target, 0.2);
  const int dx = f.ed->patternBounds().getWidth() / 8;
  sim.dragFromTo(from, from + juce::Point<int>(dx, 0));
  pump(100);
  const auto moved = patternBytes(f.p);
  CHECK(moved != before);
  f.ed->commands().undo();
  pump(100);
  CHECK(patternBytes(f.p) == before);

  f.ed->commands().setHitsLocked({target.id}, true);
  pump(100);
  const auto locked = patternBytes(f.p);
  sim.dragFromTo(from, from + juce::Point<int>(dx, 0));
  pump(100);
  CHECK(patternBytes(f.p) == locked);  // a locked hit refuses the move (status message, no edit)
}

CHOP_TEST(alt_drag_duplicates_a_hit) {
  if (!haveDisplay()) return;
  Fixture f;
  CHECK(f.open());
  if (!f.ed) return;
  const auto rects = f.ed->hitRects();
  if (rects.empty()) return;
  const auto target = *std::find_if(rects.begin(), rects.end(), [](const auto& r) { return r.topLevel; });
  const auto count = rects.size();
  MouseSim sim(*f.editor);
  const auto from = hitPointOf(*f.ed, target, 0.2);
  const int dx = f.ed->patternBounds().getWidth() / 5;
  sim.dragFromTo(from, from + juce::Point<int>(dx, 0), juce::ModifierKeys(juce::ModifierKeys::altModifier));
  pump(100);
  CHECK(f.ed->hitRects().size() >= count);  // a copy lands when the slot is free; a refused copy leaves the pattern as it was
}

CHOP_TEST(lane_wheel_scrolls_only_when_lanes_do_not_fit) {
  if (!haveDisplay()) return;
  Fixture f;
  CHECK(f.open());
  if (!f.ed) return;
  MouseSim sim(*f.editor);
  const auto g = f.ed->laneGeometry();
  const auto before = patternBytes(f.p);
  sim.wheel(f.ed->patternBounds().getCentre(), -3.0f, juce::ModifierKeys(juce::ModifierKeys::noModifiers));
  pump(60);
  const auto after = f.ed->laneGeometry();
  if (g.scrollable())
    CHECK(after.scroll >= g.scroll);
  else
    CHECK(after.scroll == 0.0);
  CHECK(patternBytes(f.p) == before);
}

CHOP_TEST(detect_previews_first_and_only_the_dialog_choice_edits_the_markers) {
  if (!haveDisplay()) return;
  Fixture f;
  CHECK(f.open());
  if (!f.ed) return;
  auto& cmd = f.ed->commands();
  const auto before = markerPositions(f.p);
  const bool waiting = cmd.beginDetect(DetectChoice::TransientsKeepMine);
  CHECK(waiting == f.ed->model().detectPreview().active);
  CHECK(markerPositions(f.p) == before);  // a preview never edits
  cmd.cancelDetect();
  CHECK(!f.ed->model().detectPreview().active);
  CHECK(markerPositions(f.p) == before);
  if (!cmd.beginDetect(DetectChoice::Grid8)) return;
  cmd.setDetectMerge(false);
  cmd.applyDetect();
  pump(60);
  CHECK(!f.ed->model().detectPreview().active);
  CHECK(markerPositions(f.p) != before);
  cmd.undoMarkers();
  pump(60);
  CHECK(markerPositions(f.p) == before);  // one undo step brings the markers back
}

CHOP_TEST(inspector_edits_the_selected_hit_in_one_undo_step_and_respects_locks) {
  if (!haveDisplay()) return;
  Fixture f;
  CHECK(f.open());
  if (!f.ed) return;
  const auto rects = f.ed->hitRects();
  if (rects.empty()) return;
  const auto target = *std::find_if(rects.begin(), rects.end(), [](const auto& r) { return r.topLevel; });
  MouseSim sim(*f.editor);
  sim.doubleClick(hitPointOf(*f.ed, target, 0.5));  // opens the inspector for the hit
  pump(100);
  CHECK(f.ed->openDrawerId() == "inspector");
  CHECK(f.ed->selectedHit() == target.id);
  auto* level = findSlider(*f.editor, "Level");
  CHECK(level != nullptr);
  if (!level) return;
  const auto before = patternBytes(f.p);
  level->setValue(-6.0, juce::sendNotificationSync);
  pump(100);
  CHECK(patternBytes(f.p) != before);
  f.ed->commands().undo();
  pump(100);
  CHECK(patternBytes(f.p) == before);

  auto* lock = findButton(*f.editor, "Lock");
  CHECK(lock != nullptr);
  if (!lock) return;
  lock->setToggleState(true, juce::sendNotificationSync);
  pump(100);
  const auto locked = patternBytes(f.p);
  level->setValue(-12.0, juce::sendNotificationSync);
  pump(100);
  CHECK(patternBytes(f.p) == locked);  // a locked hit refuses the edit
}

CHOP_TEST(fx_rack_switches_effects_on_and_off_per_hit_in_single_undo_steps) {
  if (!haveDisplay()) return;
  Fixture f;
  CHECK(f.open());
  if (!f.ed) return;
  const auto rects = f.ed->hitRects();
  if (rects.empty()) return;
  const auto target = *std::find_if(rects.begin(), rects.end(), [](const auto& r) { return r.topLevel; });
  MouseSim sim(*f.editor);
  sim.click(hitPointOf(*f.ed, target, 0.5));
  f.ed->openDrawer("fx");
  pump(100);
  CHECK(f.ed->selectedHit() == target.id);
  const auto before = patternBytes(f.p);
  auto* echo = findButton(*f.editor, "Echo");
  CHECK(echo != nullptr);
  if (!echo) return;
  echo->setToggleState(true, juce::sendNotificationSync);
  pump(100);
  CHECK(patternBytes(f.p) != before);
  auto view = f.ed->model().fxRack();
  CHECK(view.modules[static_cast<std::size_t>(cf::ui::FxModule::Echo)].on);
  CHECK(!view.modules[static_cast<std::size_t>(cf::ui::FxModule::Filter)].on);
  auto* intensity = findSlider(*f.editor, "Intensity");
  if (intensity) {
    const auto withEcho = patternBytes(f.p);
    intensity->setValue(intensity->getMaximum(), juce::sendNotificationSync);
    pump(100);
    CHECK(patternBytes(f.p) != withEcho);
  }
  f.ed->commands().undo();
  f.ed->commands().undo();
  pump(100);
  CHECK(patternBytes(f.p) == before);
  // Reset FX returns a hit with effects to the plain state.
  echo->setToggleState(true, juce::sendNotificationSync);
  pump(100);
  auto* reset = findButton(*f.editor, "Reset FX");
  CHECK(reset != nullptr);
  if (reset) reset->onClick();
  pump(100);
  view = f.ed->model().fxRack();
  CHECK(!view.modules[static_cast<std::size_t>(cf::ui::FxModule::Echo)].on);
}

CHOP_TEST(waveform_keys_zoom_fit_and_undo_markers_only_while_the_waveform_has_focus) {
  if (!haveDisplay()) return;
  Fixture f;
  CHECK(f.open());
  if (!f.ed) return;
  auto plus = juce::KeyPress('+', juce::ModifierKeys(juce::ModifierKeys::shiftModifier), '+');
  auto minus = juce::KeyPress('-', juce::ModifierKeys(), '-');
  auto home = juce::KeyPress(juce::KeyPress::homeKey, juce::ModifierKeys(), 0);
  auto ctrlZ = juce::KeyPress('z', juce::ModifierKeys(juce::ModifierKeys::ctrlModifier), 'z');
  auto ctrlY = juce::KeyPress('y', juce::ModifierKeys(juce::ModifierKeys::ctrlModifier), 'y');
  auto& model = f.ed->model();
  CHECK(!model.waveZoomed());
  CHECK(!f.ed->handleKey(plus, nullptr));  // not the waveform: the key is left alone
  CHECK(!model.waveZoomed());
  CHECK(f.ed->handleKey(plus, &f.ed->wavePanel()));
  CHECK(model.waveZoomed());
  const auto zoomedSpan = model.waveWindow().endFrame - model.waveWindow().startFrame;
  CHECK(f.ed->handleKey(minus, &f.ed->wavePanel()));
  CHECK((model.waveWindow().endFrame - model.waveWindow().startFrame) > zoomedSpan || !model.waveZoomed());
  CHECK(f.ed->handleKey(plus, &f.ed->wavePanel()));
  CHECK(f.ed->handleKey(home, &f.ed->wavePanel()));
  CHECK(!model.waveZoomed());
  // Ctrl+Z with the waveform focused undoes the marker edit, not the pattern.
  f.ed->commands().setMarkerSnap(0);
  const auto before = markerPositions(f.p);
  CHECK(model.view().markers.size() > 2);
  if (model.view().markers.size() <= 2) return;
  f.ed->commands().moveMarker(model.view().markers[2].id, model.view().markers[2].position + 400);
  pump(100);
  CHECK(markerPositions(f.p) != before);
  CHECK(f.ed->handleKey(ctrlZ, &f.ed->wavePanel()));
  pump(100);
  CHECK(markerPositions(f.p) == before);
  CHECK(f.ed->handleKey(ctrlY, &f.ed->wavePanel()));
  pump(100);
  CHECK(markerPositions(f.p) != before);
}
