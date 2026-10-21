// F13: multi-file drag sets and the explicit clean-up action (no window system needed).

#include "DragOut.h"
#include "chop_test.hpp"
#include "shell_test_support.hpp"

namespace {
juce::File scratch(const char* name) {
  juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cf_dragout_files").getChildFile(name);
  f.deleteRecursively();
  f.createDirectory();
  return f;
}
}  // namespace

CHOP_TEST(drag_file_set_writes_named_files_into_one_unique_sub_folder) {
  const juce::File root = scratch("set");
  dragout::setFolderOverride(root);
  dragout::DragFileSet a, b;
  CHECK(a.add("slice_01_kick.wav", {1, 2, 3}).existsAsFile());
  CHECK(a.add("chopfractal.mid", {4, 5}).existsAsFile());
  CHECK(b.add("slice_01_kick.wav", {9}).existsAsFile());
  CHECK(a.size() == 2 && !a.failed() && a.paths().size() == 2);
  CHECK(a.directory() != b.directory() && a.directory().getParentDirectory() == root);  // names are kept, directories are unique
  CHECK(a.directory().getChildFile("slice_01_kick.wav").getSize() == 3);
  CHECK(a.directory().getChildFile("chopfractal.mid").existsAsFile());
  dragout::setFolderOverride(juce::File());
}

CHOP_TEST(drag_file_set_failure_offers_nothing) {
  const juce::File root = scratch("fail");
  const juce::File blocker = root.getChildFile("not_a_folder");
  CHECK(blocker.replaceWithText("x"));
  dragout::setFolderOverride(blocker.getChildFile("Dropped"));  // a folder cannot be created below a regular file
  dragout::DragFileSet s;
  CHECK(s.add("ok.wav", {1}) == juce::File());
  CHECK(s.failed() && s.paths().isEmpty() && s.size() == 0);
  CHECK(s.add("later.wav", {3}) == juce::File());  // a failed set stays failed
  dragout::setFolderOverride(juce::File());
}

CHOP_TEST(cleanup_removes_only_files_older_than_90_days_and_empty_sub_folders) {
  const juce::File root = scratch("cleanup");
  dragout::setFolderOverride(root);
  dragout::DragFileSet oldSet, freshSet;
  const juce::File oldFile = oldSet.add("old.wav", {1});
  const juce::File freshFile = freshSet.add("fresh.wav", {2});
  const juce::File topOld = root.getChildFile("top_old.mid");
  CHECK(topOld.replaceWithText("x"));
  const juce::Time longAgo = juce::Time::getCurrentTime() - juce::RelativeTime::days(91);
  const juce::Time recent = juce::Time::getCurrentTime() - juce::RelativeTime::days(89);
  CHECK(oldFile.setLastModificationTime(longAgo) && topOld.setLastModificationTime(longAgo) && freshFile.setLastModificationTime(recent));
  CHECK(dragout::cleanUpDraggedFiles() == 2);
  CHECK(!oldFile.exists() && !topOld.exists() && freshFile.existsAsFile());
  CHECK(!oldSet.directory().exists() && freshSet.directory().exists());
  CHECK(dragout::cleanUpDraggedFiles() == 0);
  dragout::setFolderOverride(juce::File());
}

CHOP_TEST(set_provider_handle_is_constructible_and_reports_no_drag_without_a_window) {
  DragHandle h("Drag Kit", DragHandle::SetProvider([]() -> std::unique_ptr<dragout::DragFileSet> { return nullptr; }));
  CHECK(h.startedSet() == nullptr);
}
