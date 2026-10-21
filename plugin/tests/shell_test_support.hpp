#pragma once
// Fixtures and GUI helpers shared by the plugin shell test files (inline: every test .cpp includes this header).
#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <chopfractal/chop_contracts/rng.hpp>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <chopfractal/plugin_ui_adapter/edit_geometry.hpp>

#include "PluginProcessor.h"
#include "chop_test.hpp"

namespace shelltest {
namespace cf = chopfractal;

// GUI tests need a display. Linux has one only when DISPLAY is set; Windows and macOS always have a desktop session.
// With CHOPFRACTAL_REQUIRE_DISPLAY=1 (set by the CI job that runs under xvfb) a missing display fails instead of
// silently skipping every GUI and fuzz test.
inline bool haveDisplay() {
#if JUCE_LINUX || JUCE_BSD
  const bool has = std::getenv("DISPLAY") != nullptr;
  if (!has && std::getenv("CHOPFRACTAL_REQUIRE_DISPLAY")) {
    std::fprintf(stderr, "CHOPFRACTAL_REQUIRE_DISPLAY is set but there is no display; GUI tests cannot run\n");
    CHECK(false);
  }
  return has;
#else
  return true;
#endif
}

// A host playhead we control.
struct FakePlayHead : juce::AudioPlayHead {
  bool playing = true;
  bool hasBpm = true;
  bool hasPpq = true;
  double bpm = 120.0;
  double ppq = 0.0;
  int numerator = 4, denominator = 4;
  juce::Optional<PositionInfo> getPosition() const override {
    PositionInfo info;
    info.setIsPlaying(playing);
    if (hasBpm) info.setBpm(bpm);
    if (hasPpq) info.setPpqPosition(ppq);
    info.setTimeSignature(TimeSignature{numerator, denominator});
    return info;
  }
};

inline std::shared_ptr<const cf::render::SourceData> drumLoop() {
  auto d = std::make_shared<cf::render::SourceData>();
  d->channels = 1;
  d->sampleRate = 48000;
  d->frames = 96000;
  d->samples.assign(96000, 0.f);
  cf::Rng rng(3);
  for (int k = 0; k < 8; ++k)
    for (int i = 0; i < 5000; ++i)
      d->samples[static_cast<std::size_t>(k * 12000 + i)] = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * static_cast<float>(std::exp(static_cast<double>(-static_cast<float>(i) / 900.f))) * 0.8f;  // double exp rounded once: expf differs per libm
  return d;
}

inline void prepare(ChopFractalProcessor& p, int block = 512) {
  p.setPlayConfigDetails(2, 2, 48000.0, block);
  p.prepareToPlay(48000.0, block);
}

// `path` empty means a source with no file behind it (saved inside the project whatever the embed flag says); pass a
// path to test reference-only projects.
inline void makeSession(ChopFractalProcessor& p, std::uint64_t seed = 42, const std::string& path = "") {
  p.withSession([&](cf::composition::ProjectSession& s) {
    CHECK(s.loadSource(drumLoop(), "loop", path).ok());
    cf::composition::DetectOptions d;
    d.analysis.sensitivity = 0.8;
    CHECK(s.applyChops(d).ok());
    cf::pattern::Settings settings;
    settings.bars = 2;
    settings.density = 0.7;
    settings.seed = seed;
    CHECK(s.generate(settings).ok());
  });
}

inline std::vector<std::uint8_t> patternBytes(ChopFractalProcessor& p) {
  std::vector<std::uint8_t> out;
  p.withSession([&](cf::composition::ProjectSession& s) {
    if (s.pattern()) out = cf::pattern::serialize(*s.pattern());
  });
  return out;
}

inline void pump(int ms = 60) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

// Pumps the message loop until `pred` holds (a worker result arrives through callAsync) or the timeout passes.
template <class Pred>
bool waitFor(Pred&& pred, int timeoutMs = 5000) {
  const double end = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
  while (!pred()) {
    if (juce::Time::getMillisecondCounterHiRes() > end) return false;
    pump(20);
  }
  return true;
}

// setStateInformation returns before the audio of a reference-only project is decoded; tests that need the final
// state wait for the restore to finish.
inline bool waitForRestore(ChopFractalProcessor& p) {
  const bool ok = waitFor([&] { return !p.restoreInFlight(); });
  CHECK(ok);
  return ok;
}

// ---- recursive widget lookup (the editor is a tree of panels, not a flat list of children) ----

// True for the leaf widgets: the walk never descends into their internal sub-components (text boxes, viewports).
inline bool isLeafWidget(juce::Component* c) {
  return dynamic_cast<juce::Button*>(c) || dynamic_cast<juce::Slider*>(c) || dynamic_cast<juce::ComboBox*>(c) ||
         dynamic_cast<juce::TextEditor*>(c) || dynamic_cast<juce::Label*>(c);
}

// Visits every control below `root` depth-first, in child order.
template <class Fn>
void forEachControl(juce::Component& root, Fn&& fn) {
  for (auto* c : root.getChildren()) {
    fn(*c);
    if (!isLeafWidget(c)) forEachControl(*c, fn);
  }
}

// The component with this component ID anywhere below `root` (Component::findChildWithID only looks at direct children).
inline juce::Component* findById(juce::Component& root, const juce::String& id) {
  juce::Component* found = nullptr;
  forEachControl(root, [&](juce::Component& c) {
    if (!found && c.getComponentID() == id) found = &c;
  });
  return found;
}

// The slider whose accessibility title matches (the first one found).
inline juce::Slider* findSlider(juce::Component& root, const juce::String& title) {
  juce::Slider* found = nullptr;
  forEachControl(root, [&](juce::Component& c) {
    if (auto* sl = dynamic_cast<juce::Slider*>(&c))
      if (!found && sl->getTitle() == title) found = sl;
  });
  return found;
}

// The combo box whose accessibility title matches (the first one found).
inline juce::ComboBox* findComboBox(juce::Component& root, const juce::String& title) {
  juce::ComboBox* found = nullptr;
  forEachControl(root, [&](juce::Component& c) {
    if (auto* cb = dynamic_cast<juce::ComboBox*>(&c))
      if (!found && cb->getTitle() == title) found = cb;
  });
  return found;
}

// Exactly one button must carry the text, or the helper fails the test (a second match would make a click ambiguous).
inline juce::Button* findButton(juce::Component& root, const juce::String& text) {
  juce::Button* found = nullptr;
  int matches = 0;
  forEachControl(root, [&](juce::Component& c) {
    if (auto* b = dynamic_cast<juce::Button*>(&c))
      if (b->getButtonText() == text) {
        found = b;
        ++matches;
      }
  });
  CHECK(matches <= 1);
  return found;
}

inline juce::ComboBox* findCombo(juce::Component& root, const juce::String& title) {
  juce::ComboBox* found = nullptr;
  int matches = 0;
  forEachControl(root, [&](juce::Component& c) {
    if (auto* b = dynamic_cast<juce::ComboBox*>(&c))
      if (b->getTitle() == title) {
        found = b;
        ++matches;
      }
  });
  CHECK(matches <= 1);
  return found;
}

inline juce::TextEditor* findTextEditor(juce::Component& root, const juce::String& title) {
  juce::TextEditor* found = nullptr;
  forEachControl(root, [&](juce::Component& c) {
    if (auto* t = dynamic_cast<juce::TextEditor*>(&c))
      if (t->getTitle() == title) found = t;
  });
  return found;
}

// ---- mouse simulation ----
// Delivers synthesized mouse events to the panel canvas under a point of `root` (editor coordinates), the way the
// windowing layer would: the press picks the target, the drag and release go to the same target even outside it,
// and events are expressed in the target's own coordinates. Presses over buttons, sliders and other widgets are
// dropped (the helper simulates canvas gestures; widgets are driven through their own API).
// The point on a hit rectangle in editor coordinates; scrolls the lane into view first (lanes scroll when they do not fit).
template <class Editor, class Rect>
juce::Point<int> hitPointOf(Editor& ed, const Rect& r, double fraction = 0.5) {
  ed.revealLane(r.chopIndex);
  const auto b = ed.patternBounds();
  const auto g = ed.laneGeometry();
  const double y = chopfractal::ui::laneTop(g, r.chopIndex) + g.laneHeight * 0.5;
  return {b.getX() + static_cast<int>((r.x0 + (r.x1 - r.x0) * fraction) * b.getWidth()), b.getY() + static_cast<int>(y)};
}

class MouseSim {
 public:
  explicit MouseSim(juce::Component& root) : root_(root) {}

  juce::Component* target() const { return target_; }

  void down(juce::Point<int> rootPoint, juce::ModifierKeys mods = {}) {
    target_ = canvasAt(root_, rootPoint);
    if (target_) target_->mouseDown(event(rootPoint, mods));
  }
  void drag(juce::Point<int> rootPoint, juce::ModifierKeys mods = {}) {
    if (target_) target_->mouseDrag(event(rootPoint, mods));
  }
  void up(juce::Point<int> rootPoint, juce::ModifierKeys mods = {}) {
    if (target_) target_->mouseUp(event(rootPoint, mods));
    target_ = nullptr;
  }
  void click(juce::Point<int> rootPoint, juce::ModifierKeys mods = {}) {
    down(rootPoint, mods);
    up(rootPoint, mods);
  }
  void doubleClick(juce::Point<int> rootPoint, juce::ModifierKeys mods = {}) {
    if (auto* c = canvasAt(root_, rootPoint)) {
      target_ = c;
      c->mouseDoubleClick(event(rootPoint, mods));
      target_ = nullptr;
    }
  }
  // A move without a button (cursor and hover handling).
  void move(juce::Point<int> rootPoint, juce::ModifierKeys mods = {}) {
    if (auto* c = canvasAt(root_, rootPoint)) {
      target_ = c;
      c->mouseMove(event(rootPoint, mods));
      target_ = nullptr;
    }
  }
  // A wheel turn over the canvas under the point (deltaY > 0 is away from the user, like the OS: zoom in / scroll up).
  void wheel(juce::Point<int> rootPoint, float deltaY, juce::ModifierKeys mods = {}, float deltaX = 0.0f) {
    if (auto* c = canvasAt(root_, rootPoint)) {
      target_ = c;
      juce::MouseWheelDetails d;
      d.deltaX = deltaX;
      d.deltaY = deltaY;
      d.isReversed = false;
      d.isSmooth = false;
      d.isInertial = false;
      c->mouseWheelMove(event(rootPoint, mods), d);
      target_ = nullptr;
    }
  }
  // A press, a move and a release in one call.
  // A point inside the pattern grid that is not on a hit: a press there starts a marquee instead of moving a hit.
  template <class Editor>
  juce::Point<int> emptyPatternSpot(Editor& ed) {
    const auto area = ed.patternBounds();
    for (int y = area.getBottom() - 2; y > area.getY(); y -= 3)
      for (int x = area.getRight() - 2; x > area.getX(); x -= 7) {
        const juce::Point<int> pt(x, y);
        down(pt);
        const bool onHit = ed.selectedHit().valid();
        up(pt);
        if (!onHit) return pt;
      }
    return area.getTopLeft() + juce::Point<int>(1, 1);
  }

  void dragFromTo(juce::Point<int> from, juce::Point<int> to, juce::ModifierKeys mods = {}) {
    down(from, mods);
    drag(to, mods);
    up(to, mods);
  }

 private:
  // The canvas panel under the point: walks down the visible children; a panel whose ID names one of the canvases (the four main ones, the drawer pads "pad-N" and "canvas-..." panels)
  // is the target, any other widget is not. A point no child covers belongs to `root` itself, so the helper also
  // works on a component that draws its canvases directly.
  static bool isCanvasId(const juce::String& id) {
    return id == "WavePanel" || id == "PatternPanel" || id == "HistoryPanel" || id == "OrbitPanel" || id == "ShellChrome" || id.startsWith("pad-") || id.startsWith("canvas-");
  }
  static juce::Component* canvasAt(juce::Component& c, juce::Point<int> local, bool isRoot = true) {
    for (int i = c.getNumChildComponents(); --i >= 0;) {
      auto* child = c.getChildComponent(i);
      if (!child->isVisible() || !child->getBounds().contains(local)) continue;
      if (isCanvasId(child->getComponentID())) {
        // The chrome draws furniture behind its own buttons: a press on one of them is a widget press, not a canvas press.
        for (int j = child->getNumChildComponents(); --j >= 0;) {
          auto* inner = child->getChildComponent(j);
          if (inner->isVisible() && inner->getBounds().contains(local - child->getPosition()) && isLeafWidget(inner)) return nullptr;
        }
        return child;
      }
      if (isLeafWidget(child) || child->getNumChildComponents() == 0) return nullptr;
      return canvasAt(*child, local - child->getPosition(), false);  // a container panel
    }
    return isRoot ? &c : nullptr;
  }
  juce::MouseEvent event(juce::Point<int> rootPoint, juce::ModifierKeys mods) const {
    const auto p = (target_ ? target_->getLocalPoint(&root_, rootPoint) : rootPoint);
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent(src, p.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, target_, target_, juce::Time::getCurrentTime(), p.toFloat(),
                            juce::Time::getCurrentTime(), 1, false);
  }

  juce::Component& root_;
  juce::Component* target_ = nullptr;
};

}  // namespace shelltest
