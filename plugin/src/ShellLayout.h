#pragma once
// Pure layout of the editor shell (docs/specs/gui-redesign.md section 6): no components, so it can be unit tested.
// All sizes are logical pixels; `scale` follows s = clamp(min(W / 1100, H / 720), 0.8, 2.0) times the user interface scale.
#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <cmath>

namespace shell {
constexpr int kDefaultW = 1100, kDefaultH = 720, kMinW = 880, kMinH = 560, kMaxW = 2200, kMaxH = 1440;

struct SourceParts {
  juce::Rectangle<int> toolbar, ruler, wave, chopStrip;
};
struct PatternParts {
  juce::Rectangle<int> header, ruler, laneHeaders, grid;
};
struct ShellRects {
  float scale = 1.0f;
  int margin = 12, gap = 8;
  juce::Rectangle<int> topBar, source, pattern, macro, status, drawer;  // drawer is empty when closed
  SourceParts sourceParts;
  PatternParts patternParts;
};

inline float scaleFor(int w, int h, float uiScale = 1.0f) {
  const float base = std::clamp(std::min(static_cast<float>(w) / kDefaultW, static_cast<float>(h) / kDefaultH), 0.8f, 2.0f);
  // The user scale never pushes the content past what the window can hold (the layout needs about 880 x 600 at scale 1).
  const float fit = std::min(static_cast<float>(w) / kMinW, static_cast<float>(h) / 600.0f);
  const float want = base * uiScale;
  return uiScale <= 1.0f ? want : std::max(base, std::min(want, fit));
}

// drawerWidth is in logical pixels (0 = closed). The drawer sits right of the source and pattern panels; the macro strip keeps the full width.
inline ShellRects computeShellLayout(int W, int H, float uiScale = 1.0f, int drawerWidth = 0) {
  ShellRects r;
  const float s = scaleFor(W, H, uiScale);
  r.scale = s;
  auto px = [s](float v) { return static_cast<int>(std::lround(v * s)); };
  r.margin = px(12);
  r.gap = px(8);
  const int topH = px(44), statusH = px(28), macroH = std::max(128, px(168));
  r.topBar = {0, 0, W, topH};
  r.status = {0, H - statusH, W, statusH};
  const int macroTop = H - statusH - r.gap - macroH;
  r.macro = {r.margin, macroTop, W - 2 * r.margin, macroH};
  const int y0 = topH + r.gap;
  const int avail = std::max(0, macroTop - r.gap - y0);
  int sourceH = std::max(150, static_cast<int>(std::lround((avail - r.gap) * 0.44)));
  int patternH = avail - r.gap - sourceH;
  if (patternH < 170) {
    patternH = 170;
    sourceH = std::max(0, avail - r.gap - patternH);
  }
  int panelW = W - 2 * r.margin;
  if (drawerWidth > 0) {
    const int dw = std::min(drawerWidth, panelW / 2);
    r.drawer = {W - r.margin - dw, y0, dw, avail};
    panelW -= dw + r.gap;
  }
  r.source = {r.margin, y0, panelW, sourceH};
  r.pattern = {r.margin, y0 + sourceH + r.gap, panelW, patternH};

  const int pad = px(8);
  {
    auto in = r.source.reduced(pad, pad / 2);
    SourceParts& p = r.sourceParts;
    p.toolbar = in.removeFromTop(px(28));
    in.removeFromTop(px(2));
    p.ruler = in.removeFromTop(px(16));
    p.chopStrip = in.removeFromBottom(std::max(28, px(36)));
    p.wave = in;
  }
  {
    auto in = r.pattern.reduced(pad, pad / 2);
    PatternParts& p = r.patternParts;
    p.header = in.removeFromTop(px(28));
    p.ruler = in.removeFromTop(px(18)).withTrimmedLeft(0);
    const int laneW = std::max(120, px(148));
    p.laneHeaders = in.removeFromLeft(laneW);
    p.grid = in;
    p.ruler = p.ruler.withTrimmedLeft(laneW);
  }
  return r;
}
}  // namespace shell
