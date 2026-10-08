// Minimal consumer: classify a synthetic kick and suggest loop lengths for an 8 second clip.
#include <chopfractal/chop_insight/insight.hpp>
#include <cmath>
#include <cstdio>

int main() {
  using namespace chopfractal::insight;
  std::vector<float> kick(7200);
  for (std::size_t i = 0; i < kick.size(); ++i)
    kick[i] = static_cast<float>(std::sin(2.0 * 3.14159265 * 55.0 * static_cast<double>(i) / 48000.0) * std::exp(-static_cast<double>(i) / 1800.0));
  const float* ch[1] = {kick.data()};
  SourceView view{ch, 1, static_cast<std::int64_t>(kick.size()), 48000};
  auto f = analyzeChop(view, {0, view.frames});
  if (!f.ok() || classify(f.value()) != Role::Kick) return 1;
  auto loops = suggestLoop(8.0);
  if (loops.empty()) return 1;
  std::printf("kick detected; best loop: %s\n", loops[0].note.c_str());
  return 0;
}
