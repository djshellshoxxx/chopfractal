#include "Log.h"

#include "BuildInfo.h"

#include <algorithm>

namespace cflog {
namespace {
juce::CriticalSection lock;
juce::File overrideFolder;
bool initialised = false;
int instances = 0;  // plugin instances alive in this process; the marker lives until the last one goes

juce::File logFolder() {
  if (overrideFolder != juce::File()) return overrideFolder;
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ChopFractal").getChildFile("logs");
}

juce::File currentFile() {
  const auto base = "chopfractal-" + juce::Time::getCurrentTime().formatted("%Y%m%d");
  for (int n = 0; n < 1000; ++n) {
    const auto f = logFolder().getChildFile(base + (n == 0 ? juce::String() : "-" + juce::String(n)) + ".log");
    if (!f.existsAsFile() || f.getSize() < kMaxFileBytes) return f;
  }
  return logFolder().getChildFile(base + "-overflow.log");
}

// Chronological key from the name (date, then rotation index); file times can tie within a clock tick.
juce::String sortKey(const juce::File& f) {
  const auto stem = f.getFileNameWithoutExtension().fromFirstOccurrenceOf("chopfractal-", false, false);
  const auto date = stem.upToFirstOccurrenceOf("-", false, false);
  const auto index = stem.contains("-") ? stem.fromFirstOccurrenceOf("-", false, false).getIntValue() : 0;
  return date + "-" + juce::String(index).paddedLeft('0', 6);
}

juce::Array<juce::File> logFilesOldestFirst() {
  auto files = logFolder().findChildFiles(juce::File::findFiles, false, "chopfractal-*.log");
  std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) { return sortKey(a) < sortKey(b); });
  return files;
}

void prune() {
  const auto files = logFilesOldestFirst();
  for (int i = 0; i < files.size() - kMaxFiles; ++i) files.getReference(i).deleteFile();
}

void write(const char* level, const juce::String& message) {
  try {
    const juce::ScopedLock guard(lock);
    if (!logFolder().createDirectory().wasOk()) return;
    const auto line = juce::Time::getCurrentTime().toISO8601(true) + " [" + level + "] " + message.replaceCharacters("\r\n", "  ") + "\n";
    juce::FileOutputStream out(currentFile());
    if (!out.openedOk()) return;
    out.setPosition(out.getFile().getSize());  // append
    out.write(line.toRawUTF8(), line.getNumBytesAsUTF8());
    out.flush();
  } catch (...) {
  }
}

juce::File marker() { return logFolder().getChildFile("session.marker"); }
}  // namespace

void setFolderForTesting(const juce::File& f) {
  const juce::ScopedLock guard(lock);
  overrideFolder = f;
  initialised = false;
  instances = 0;
}

juce::File folder() { return logFolder(); }

void init() {
  try {
    {
      const juce::ScopedLock guard(lock);
      ++instances;
      if (initialised) return;
      initialised = true;
      logFolder().createDirectory();
      prune();
    }
    const bool crashed = marker().existsAsFile();
    marker().replaceWithText("running");
    if (crashed) warn("The previous session ended unexpectedly.");
    info(juce::String("ChopFractal ") + buildinfo::kVersionFull + " (build " + buildinfo::kBuildNumber + ", " + buildinfo::kGitSha + ") on " +
         juce::SystemStats::getOperatingSystemName());
  } catch (...) {
  }
}

void shutdown() {
  try {
    const juce::ScopedLock guard(lock);
    if (instances > 0 && --instances > 0) return;
    marker().deleteFile();
    initialised = false;
  } catch (...) {
  }
}

void info(const juce::String& m) { write("INFO", m); }
void warn(const juce::String& m) { write("WARN", m); }
void error(const juce::String& m) { write("ERROR", m); }

juce::String tail(int lines) {
  try {
    const juce::ScopedLock guard(lock);
    const auto files = logFilesOldestFirst();
    juce::StringArray all;
    for (const auto& f : files) all.addLines(f.loadFileAsString());
    all.removeEmptyStrings();
    const int from = std::max(0, all.size() - lines);
    return all.joinIntoString("\n", from, lines);
  } catch (...) {
    return {};
  }
}
}  // namespace cflog
