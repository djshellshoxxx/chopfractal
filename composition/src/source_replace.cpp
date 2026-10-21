#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/composition/source_replace.hpp>

namespace chopfractal::composition {

Status SourceReplaceUndo::capture(const ProjectSession& session) {
  // Not embedded: the audio is kept by pointer, so a source above the embed cap still restores and nothing is copied.
  auto saved = session.saveState(false);
  if (!saved.ok()) return saved.error();
  state_ = std::move(saved.value());
  data_ = session.sourceData();
  captured_ = true;
  return {};
}

Status SourceReplaceUndo::restore(ProjectSession& session) {
  if (!captured_) return makeError(ErrorCode::InvalidArgument, "there is nothing to undo");
  const std::shared_ptr<const render::SourceData> data = data_;
  Status s = session.loadState(state_.data(), state_.size(), [data](const source::SourceInfo&) { return data; });
  if (!s.ok()) return s;
  clear();
  return {};
}

void SourceReplaceUndo::clear() {
  captured_ = false;
  state_.clear();
  state_.shrink_to_fit();
  data_.reset();
}

}  // namespace chopfractal::composition
