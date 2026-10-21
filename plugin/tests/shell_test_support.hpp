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
#include <vector>

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
  juce::Optional<PositionInfo> getPosition() const override {
    PositionInfo info;
    info.setIsPlaying(playing);
    if (hasBpm) info.setBpm(bpm);
    if (hasPpq) info.setPpqPosition(ppq);
    info.setTimeSignature(TimeSignature{4, 4});
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
      d->samples[static_cast<std::size_t>(k * 12000 + i)] = static_cast<float>(rng.uniform01() * 2.0 - 1.0) * std::exp(-static_cast<float>(i) / 900.f) * 0.8f;
  return d;
}

inline void prepare(ChopFractalProcessor& p, int block = 512) {
  p.setPlayConfigDetails(2, 2, 48000.0, block);
  p.prepareToPlay(48000.0, block);
}

inline void makeSession(ChopFractalProcessor& p, std::uint64_t seed = 42) {
  p.withSession([&](cf::composition::ProjectSession& s) {
    CHECK(s.loadSource(drumLoop(), "loop", "").ok());
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
  // A press, a move and a release in one call.
  void dragFromTo(juce::Point<int> from, juce::Point<int> to, juce::ModifierKeys mods = {}) {
    down(from, mods);
    drag(to, mods);
    up(to, mods);
  }

 private:
  // The canvas panel under the point: walks down the visible children; a panel whose ID names one of the four canvases
  // is the target, any other widget is not. A point no child covers belongs to `root` itself, so the helper also
  // works on a component that draws its canvases directly.
  static bool isCanvasId(const juce::String& id) { return id == "WavePanel" || id == "PatternPanel" || id == "HistoryPanel" || id == "OrbitPanel"; }
  static juce::Component* canvasAt(juce::Component& c, juce::Point<int> local, bool isRoot = true) {
    for (int i = c.getNumChildComponents(); --i >= 0;) {
      auto* child = c.getChildComponent(i);
      if (!child->isVisible() || !child->getBounds().contains(local)) continue;
      if (isCanvasId(child->getComponentID())) return child;
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
