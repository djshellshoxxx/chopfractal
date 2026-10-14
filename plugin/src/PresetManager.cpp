#include "PresetManager.h"

#include "AppPaths.h"

#include <algorithm>

namespace cf = chopfractal;

namespace {
juce::File& folderOverride() {
  static juce::File f;
  return f;
}
}  // namespace

juce::File PresetManager::userFolder() {
  if (folderOverride() != juce::File()) return folderOverride();
  return apppaths::dataRoot().getChildFile("Presets");
}

void PresetManager::setFolderOverride(const juce::File& folder) { folderOverride() = folder; }

std::vector<PresetManager::Entry> PresetManager::list(int* skipped) {
  std::vector<Entry> out;
  if (skipped) *skipped = 0;
  for (const auto& p : cf::presets::factoryPresets()) {
    Entry e;
    e.label = "Factory: " + juce::String(p.name);
    e.factory = true;
    e.preset = p;
    out.push_back(std::move(e));
  }
  std::vector<Entry> user;
  const juce::File dir = userFolder();
  if (dir.isDirectory())
    for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.cfpreset")) {
      juce::MemoryBlock mb;
      bool ok = f.getSize() > 0 && f.getSize() <= 4096 && f.loadFileAsData(mb);
      if (ok) {
        auto r = cf::presets::deserialize(static_cast<const std::uint8_t*>(mb.getData()), mb.getSize());
        if (r.ok()) {
          Entry e;
          e.label = juce::String(r.value().name);
          e.factory = false;
          e.preset = std::move(r.value());
          e.file = f;
          user.push_back(std::move(e));
          continue;
        }
      }
      if (skipped) ++*skipped;
    }
  std::sort(user.begin(), user.end(), [](const Entry& a, const Entry& b) { return a.label.compareIgnoreCase(b.label) < 0; });
  for (auto& e : user) out.push_back(std::move(e));
  return out;
}

cf::Status PresetManager::save(const cf::presets::Preset& p, bool overwrite) {
  cf::Status v = cf::presets::validate(p);
  if (!v.ok()) return v;
  const juce::File dir = userFolder();
  if (!dir.createDirectory().wasOk()) return cf::makeError(cf::ErrorCode::InvalidArgument, "cannot create the preset folder");
  const juce::File target = dir.getChildFile(juce::String(cf::presets::fileNameFor(p.name)));
  if (target.existsAsFile() && !overwrite) return cf::makeError(cf::ErrorCode::Conflict, "a preset with this name already exists");
  const auto bytes = cf::presets::serialize(p);
  juce::TemporaryFile tmp(target, juce::TemporaryFile::useHiddenFile);  // hidden temp name, never matches *.cfpreset
  if (!tmp.getFile().replaceWithData(bytes.data(), bytes.size())) return cf::makeError(cf::ErrorCode::InvalidArgument, "could not write the preset");
  if (!tmp.overwriteTargetFileWithTemporary()) return cf::makeError(cf::ErrorCode::InvalidArgument, "could not finish writing the preset");
  return {};
}

cf::Status PresetManager::remove(const juce::File& file) {
  if (!file.existsAsFile() || !file.isAChildOf(userFolder())) return cf::makeError(cf::ErrorCode::NotFound, "that preset is not in the preset folder");
  return file.deleteFile() ? cf::Status{} : cf::Status{cf::makeError(cf::ErrorCode::InvalidArgument, "could not delete the preset")};
}
