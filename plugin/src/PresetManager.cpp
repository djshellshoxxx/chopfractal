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
    e.key = "factory:" + juce::String(p.name);
    e.factory = true;
    e.preset = p;
    out.push_back(std::move(e));
  }
  std::vector<Entry> user;
  const juce::File dir = userFolder();
  if (dir.isDirectory())
    for (const auto& f : dir.findChildFiles(juce::File::findFiles | juce::File::ignoreHiddenFiles, false, "*.cfpreset")) {
      juce::MemoryBlock mb;
      bool ok = f.getSize() > 0 && f.getSize() <= static_cast<juce::int64>(cf::presets::kMaxFileBytes) && f.loadFileAsData(mb);
      if (ok) {
        auto r = cf::presets::deserialize(static_cast<const std::uint8_t*>(mb.getData()), mb.getSize());
        if (r.ok()) {
          Entry e;
          e.label = juce::String(r.value().name);
          e.key = "user:" + f.getFileNameWithoutExtension();
          e.factory = false;
          e.preset = std::move(r.value());
          e.file = f;
          user.push_back(std::move(e));
          continue;
        }
        if (r.error().code == cf::ErrorCode::UnsupportedVersion) {  // listed greyed out, never applied
          Entry e;
          e.label = f.getFileNameWithoutExtension();
          e.key = "user:" + f.getFileNameWithoutExtension();
          e.factory = false;
          e.loadable = false;
          e.preset.name = e.label.toStdString();
          e.preset.category = "User";
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

cf::Status PresetManager::rename(const juce::File& file, const juce::String& newName) {
  if (!file.existsAsFile() || !file.isAChildOf(userFolder())) return cf::makeError(cf::ErrorCode::NotFound, "that preset is not in the preset folder");
  juce::MemoryBlock mb;
  if (!file.loadFileAsData(mb)) return cf::makeError(cf::ErrorCode::InvalidArgument, "could not read the preset");
  auto r = cf::presets::deserialize(static_cast<const std::uint8_t*>(mb.getData()), mb.getSize());
  if (!r.ok()) return cf::makeError(r.error().code, r.error().message);
  cf::presets::Preset p = r.value();
  p.name = newName.toStdString();
  const juce::File target = userFolder().getChildFile(juce::String(cf::presets::fileNameFor(p.name)));
  const bool sameFile = target == file;
  cf::Status st = save(p, sameFile);
  if (!st.ok()) return st;
  if (!sameFile && !file.deleteFile()) return cf::makeError(cf::ErrorCode::InvalidArgument, "could not remove the old preset file");
  return {};
}

juce::StringArray PresetManager::favorites() {
  juce::StringArray out;
  const juce::File f = userFolder().getChildFile("favorites.txt");
  if (!f.existsAsFile() || f.getSize() > 64 * 1024) return out;
  juce::StringArray lines;
  lines.addLines(f.loadFileAsString());
  for (const auto& l : lines) {
    const auto t = l.trim();
    if (t.isNotEmpty() && out.size() < 200) out.addIfNotAlreadyThere(t);
  }
  return out;
}

bool PresetManager::isFavorite(const juce::String& key) { return favorites().contains(key); }

cf::Status PresetManager::setFavorite(const juce::String& key, bool on) {
  juce::StringArray f = favorites();
  if (on) f.addIfNotAlreadyThere(key);
  else f.removeString(key);
  while (f.size() > 200) f.remove(f.size() - 1);
  const juce::File dir = userFolder();
  if (!dir.createDirectory().wasOk()) return cf::makeError(cf::ErrorCode::InvalidArgument, "cannot create the preset folder");
  const juce::File target = dir.getChildFile("favorites.txt");
  juce::TemporaryFile tmp(target, juce::TemporaryFile::useHiddenFile);
  if (!tmp.getFile().replaceWithText(f.joinIntoString("\n") + "\n") || !tmp.overwriteTargetFileWithTemporary())
    return cf::makeError(cf::ErrorCode::InvalidArgument, "could not write the favorites list");
  return {};
}
