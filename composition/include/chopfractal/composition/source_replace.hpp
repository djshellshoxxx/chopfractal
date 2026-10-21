#pragma once
// One-level "undo the last source replace" store shared by the features that swap the whole source (Capture
// "Keep", tempo-match Stretch). It keeps the complete project state saved just before the replace plus a pointer
// to the old audio, so the audio is never copied or re-encoded and a source too large to embed still restores.
// Threading: message thread only, together with the ProjectSession it belongs to.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/chop_contracts/result.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace chopfractal::composition {

class ProjectSession;

class SourceReplaceUndo {
 public:
  // Call right before replacing the source. Replaces any earlier capture. On error nothing is stored.
  Status capture(const ProjectSession& session);
  bool available() const { return captured_; }
  // loadState of the captured bytes with the captured audio as the resolver. All-or-nothing (loadState). The
  // capture is consumed on success and kept on failure.
  Status restore(ProjectSession& session);
  // Any structural change (Generate, Mutate, Load, Capture, another replace) ends the undo window.
  void clear();

 private:
  bool captured_ = false;
  std::vector<std::uint8_t> state_;
  std::shared_ptr<const render::SourceData> data_;
};

}  // namespace chopfractal::composition
