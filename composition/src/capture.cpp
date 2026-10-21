// Input capture glue (F12): a kept capture is an ordinary source with no file behind it.
#include <chopfractal/composition/project_session.hpp>

namespace chopfractal::composition {

// A named wrapper so call sites read clearly. The empty path is what makes sourceNeedsEmbed() true, so the processor
// writes the audio into the project state and the capture survives a save without any file on disk.
Status ProjectSession::loadCapturedSource(std::shared_ptr<const render::SourceData> data, std::string name) {
  return loadSource(std::move(data), std::move(name), std::string{});
}

}  // namespace chopfractal::composition
