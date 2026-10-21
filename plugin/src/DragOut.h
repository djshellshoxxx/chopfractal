#pragma once
// Drag-out of the current pattern as a MIDI or WAV file into a DAW: temp-file helpers and the drag handle.
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace dragout {
// <user data>/ChopFractal/Dropped (or the override, used by tests). Files are kept for 90 days.
juce::File folder();
void setFolderOverride(const juce::File& f);
// Writes `bytes` to a new unique file "<stem>-<n>.<ext>" in folder(); returns juce::File() on failure.
juce::File writeTemp(const juce::String& stem, const juce::String& ext, const std::vector<std::uint8_t>& bytes);
// Deletes files in folder() (and in its per-drag sub-folders) older than `days`, then removes sub-folders left empty;
// returns how many files were removed.
int cleanupOlderThanDays(int days);
// The explicit "Delete dragged files older than 90 days" action (kDragCleanupAgeDays). Nothing calls it automatically.
constexpr int kDragCleanupAgeDays = 90;
int cleanUpDraggedFiles();

// The files of one multi-file drag (Drag Kit, or any drag whose files must keep their names): a unique sub-folder of
// folder(), created on the first add(). Files are written atomically. A failed write removes the sub-folder, so a
// partial set is never offered to the OS. Dragged files are never deleted when the drop finishes.
class DragFileSet {
 public:
  DragFileSet() = default;
  juce::File add(const juce::String& name, const std::vector<std::uint8_t>& bytes);  // juce::File() on failure
  juce::StringArray paths() const;
  juce::File directory() const { return dir_; }
  bool failed() const { return failed_; }
  int size() const { return files_.size(); }

 private:
  juce::File dir_;
  juce::Array<juce::File> files_;
  bool failed_ = false;
};
}  // namespace dragout

// A small button-like component: press and drag beyond kDragThresholdPx to start an OS file drag. The provider is
// called only when the drag starts and returns the file to drag, or an empty File (a message is shown by the owner).
class DragHandle : public juce::Component, public juce::SettableTooltipClient {
 public:
  static constexpr int kDragThresholdPx = 6;
  using Provider = std::function<juce::File()>;
  // Multi-file drags (Drag Kit): called when the drag starts; null or a failed set means nothing is dragged.
  using SetProvider = std::function<std::unique_ptr<dragout::DragFileSet>()>;

  DragHandle(const juce::String& text, Provider provider) : text_(text), provider_(std::move(provider)) {}
  DragHandle(const juce::String& text, SetProvider makeFiles) : text_(text), makeFiles_(std::move(makeFiles)) {}
  void setProvider(Provider p) { provider_ = std::move(p); }
  void setSetProvider(SetProvider p) { makeFiles_ = std::move(p); }
  const dragout::DragFileSet* startedSet() const { return lastSet_.get(); }  // for tests: what the last multi-file drag produced
  void paint(juce::Graphics& g) override;
  void mouseDrag(const juce::MouseEvent& e) override;
  void mouseUp(const juce::MouseEvent&) override { started_ = false; }
  juce::File startedFile() const { return lastFile_; }  // for tests: what the last drag produced

 private:
  juce::String text_;
  Provider provider_;
  SetProvider makeFiles_;
  std::unique_ptr<dragout::DragFileSet> lastSet_;
  bool started_ = false;
  juce::File lastFile_;
};
