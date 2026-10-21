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
  for (const auto& f : folder().findChildFiles(juce::File::findFiles, true))
    if (f.getLastModificationTime() < cutoff && f.deleteFile()) ++removed;
  for (const auto& d : folder().findChildFiles(juce::File::findDirectories, false))
    if (d.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0) d.deleteFile();  // a per-drag folder emptied above
  return removed;
}

int cleanUpDraggedFiles() { return cleanupOlderThanDays(kDragCleanupAgeDays); }

juce::File DragFileSet::add(const juce::String& name, const std::vector<std::uint8_t>& bytes) {
  if (failed_) return {};
  if (dir_ == juce::File()) {
    const juce::File root = folder();
    if (!root.createDirectory().wasOk()) {
      failed_ = true;
      return {};
    }
    dir_ = root.getNonexistentChildFile("drag-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S"), "", false);
    if (!dir_.createDirectory().wasOk()) {
      failed_ = true;
      dir_ = juce::File();
      return {};
    }
  }
  const juce::File f = dir_.getChildFile(juce::File::createLegalFileName(name));
  if (!f.replaceWithData(bytes.data(), bytes.size())) {
    failed_ = true;
    dir_.deleteRecursively();  // never offer a partial set
    files_.clear();
    return {};
  }
  files_.add(f);
  return f;
}

juce::StringArray DragFileSet::paths() const {
  juce::StringArray out;
  for (const auto& f : files_) out.add(f.getFullPathName());
  return out;
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
  if (started_ || !isEnabled() || (!provider_ && !makeFiles_)) return;
  if (e.getDistanceFromDragStart() < kDragThresholdPx) return;
  started_ = true;
  if (makeFiles_) {
    lastSet_ = makeFiles_();
    if (lastSet_ == nullptr || lastSet_->failed() || lastSet_->size() == 0) return;
    juce::DragAndDropContainer::performExternalDragDropOfFiles(lastSet_->paths(), false, this);
    return;
  }
  lastFile_ = provider_();
  if (lastFile_ == juce::File() || !lastFile_.existsAsFile()) return;
  juce::DragAndDropContainer::performExternalDragDropOfFiles(juce::StringArray(lastFile_.getFullPathName()), false, this);
}
