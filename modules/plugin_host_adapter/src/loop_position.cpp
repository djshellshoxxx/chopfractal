#include <chopfractal/plugin_host_adapter/loop_position.hpp>

#include <cmath>

namespace chopfractal::host {

LoopPosition computeLoopPosition(const render::TransportBlock& block, int frames, double sampleRate, double quarters) {
  LoopPosition out;
  if (!(block.playing && block.positionValid && quarters > 0.0)) return out;  // playheadQuarters stays -1
  // The operations and their order are exactly those of the original inline code in processBlock().
  const double end = block.ppq + static_cast<double>(frames) * block.bpm / (60.0 * sampleRate);
  if (std::floor(block.ppq / quarters) != std::floor(end / quarters)) out.boundary = true;
  if (std::floor((block.ppq - 0.5 * quarters) / quarters) != std::floor((end - 0.5 * quarters) / quarters)) out.midpoint = true;
  double ph = std::fmod(block.ppq, quarters);
  if (ph < 0.0) ph += quarters;
  out.playheadQuarters = ph;
  return out;
}

}  // namespace chopfractal::host
