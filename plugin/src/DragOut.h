#pragma once
// Drag-out of the current pattern as a MIDI or WAV file into a DAW: temp-file helpers and the drag handle.
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace dragout {
// <user data>/ChopFractal/Dropped (or the override, used by tests). Files are kept for 90 days.
juce::File folder();
void setFolderOverride(const juce::File& f);
// Writes `bytes` to a new unique file "<stem>-<n>.<ext>" in folder(); returns juce::File() on failure.
juce::File writeTemp(const juce::String& stem, const juce::String& ext, const std::vector<std::uint8_t>& bytes);
// Deletes files in folder() older than `days`; returns how many were removed.
int cleanupOlderThanDays(int days);
}  // namespace dragout

// A small button-like component: press and drag beyond kDragThresholdPx to start an OS file drag. The provider is
// called only when the drag starts and returns the file to drag, or an empty File (a message is shown by the owner).
class DragHandle : public juce::Component, public juce::SettableTooltipClient {
 public:
  static constexpr int kDragThresholdPx = 6;
  using Provider = std::function<juce::File()>;

  DragHandle(const juce::String& text, Provider provider) : text_(text), provider_(std::move(provider)) {}
  void setProvider(Provider p) { provider_ = std::move(p); }
  void paint(juce::Graphics& g) override;
  void mouseDrag(const juce::MouseEvent& e) override;
  void mouseUp(const juce::MouseEvent&) override { started_ = false; }
  juce::File startedFile() const { return lastFile_; }  // for tests: what the last drag produced

 private:
  juce::String text_;
  Provider provider_;
  bool started_ = false;
  juce::File lastFile_;
};
