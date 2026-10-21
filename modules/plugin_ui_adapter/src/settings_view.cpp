#include "chopfractal/plugin_ui_adapter/settings_view.hpp"

#include <cstddef>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chopfractal::ui {

const std::vector<int>& uiScalePercents() {
  static const std::vector<int> v{90, 100, 110, 125};
  return v;
}
const std::vector<std::string>& uiScaleItems() {
  static const std::vector<std::string> v{"90%", "100%", "110%", "125%"};
  return v;
}
int uiScaleIndex(int percent) {
  const auto& p = uiScalePercents();
  int best = 0;
  for (int i = 1; i < static_cast<int>(p.size()); ++i)
    if (std::abs(p[static_cast<std::size_t>(i)] - percent) < std::abs(p[static_cast<std::size_t>(best)] - percent)) best = i;
  return best;
}
int uiScalePercentAt(int index) {
  const auto& p = uiScalePercents();
  return p[static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(p.size()) - 1))];
}
std::string limiterCeilingText(double db) {
  char b[32];
  std::snprintf(b, sizeof b, "%.1f dB", std::fabs(db) < 0.05 ? 0.0 : std::fabs(db));
  return (db <= -0.05 ? std::string("\xE2\x88\x92") : std::string()) + b;
}

std::string buildDiagnosticsText(const DiagnosticsInput& d) {
  char b[256];
  std::string t = "ChopFractal diagnostics\n";
  auto line = [&t](const std::string& k, const std::string& v) { t += k + ": " + v + "\n"; };
  line("Version", d.version);
  line("Format", d.format + (d.host.empty() ? "" : ", host " + d.host));
  std::snprintf(b, sizeof b, "%.0f Hz, block %d", d.sampleRate, d.blockSize);
  line("Audio", b);
  std::snprintf(b, sizeof b, "%s, %dx%d at %d%%%s%s", d.theme.c_str(), d.windowWidth, d.windowHeight, d.uiScalePercent, d.reducedMotion ? ", reduced motion" : "", d.advanced ? ", advanced" : "");
  line("Interface", b);
  if (d.hasSource) {
    std::snprintf(b, sizeof b, "%s, %.2f s, %d chops, %d hits, %d bars", d.sourceName.c_str(), d.sourceSeconds, d.chops, d.events, d.bars);
    line("Source", b);
  } else {
    line("Source", "none loaded");
  }
  std::snprintf(b, sizeof b, "%.1f BPM (%s), %d/%d", d.bpm, d.localTempo ? "local" : "host", d.numerator, d.denominator);
  line("Tempo", b);
  line("Output", std::string("limiter ") + (d.limiterOn ? "on" : "off") + " at " + limiterCeilingText(d.limiterCeilingDb) + ", level match " + (d.levelMatchOn ? "on" : "off"));
  if (d.notices.empty()) {
    line("Notices", "none");
  } else {
    t += "Notices:\n";
    for (const auto& n : d.notices) t += "  " + n + "\n";
  }
  return t;
}

}  // namespace chopfractal::ui
