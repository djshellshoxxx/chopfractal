#include "DragOut.h"

#include "AppPaths.h"

namespace dragout {
namespace {
juce::File& override_() {
  static juce::File f;
  return f;
}
}  // namespace

juce::File folder() {
  if (override_() != juce::File()) return override_();
  // A persistent per-user folder: DAWs often link dropped media in place, so it must not live in a purged temp folder.
  return apppaths::dataRoot().getChildFile("Dropped");
}
void setFolderOverride(const juce::File& f) { override_() = f; }

juce::File writeTemp(const juce::String& stem, const juce::String& ext, const std::vector<std::uint8_t>& bytes) {
  const juce::File dir = folder();
  if (!dir.createDirectory().wasOk()) return {};
  const juce::File f = dir.getNonexistentChildFile(stem + "-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S"), "." + ext, true);
  if (!f.replaceWithData(bytes.data(), bytes.size())) return {};
  return f;
}

int cleanupOlderThanDays(int days) {
  int removed = 0;
  const juce::Time cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days(days);
  for (const auto& f : folder().findChildFiles(juce::File::findFiles, false))
    if (f.getLastModificationTime() < cutoff && f.deleteFile()) ++removed;
  return removed;
}
}  // namespace dragout

void DragHandle::paint(juce::Graphics& g) {
  const auto r = getLocalBounds().toFloat().reduced(1.0f);
  g.setColour(isEnabled() ? juce::Colour(0xff3a4a63) : juce::Colour(0xff2a2f38));
  g.fillRoundedRectangle(r, 4.0f);
  g.setColour(isEnabled() ? juce::Colours::white : juce::Colours::grey);
  g.drawRoundedRectangle(r, 4.0f, 1.0f);
  g.setFont(12.0f);
  g.drawText(text_, getLocalBounds(), juce::Justification::centred);
}

void DragHandle::mouseDrag(const juce::MouseEvent& e) {
  if (started_ || !isEnabled() || !provider_) return;
  if (e.getDistanceFromDragStart() < kDragThresholdPx) return;
  started_ = true;
  lastFile_ = provider_();
  if (lastFile_ == juce::File() || !lastFile_.existsAsFile()) return;
  juce::DragAndDropContainer::performExternalDragDropOfFiles(juce::StringArray(lastFile_.getFullPathName()), false, this);
}
