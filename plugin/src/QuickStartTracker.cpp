#include "QuickStartTracker.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "AppPaths.h"

namespace {
juce::File gOverride;
constexpr std::size_t kMaxRecents = 8;
constexpr juce::uint32 kDebounceMs = 400;
}  // namespace

juce::File QuickStartTracker::defaultFile() { return gOverride != juce::File() ? gOverride : apppaths::dataRoot().getChildFile("quickstart.json"); }
void QuickStartTracker::setFileOverride(const juce::File& f) { gOverride = f; }

const char* QuickStartTracker::stepKey(QuickStep s) {
  static const char* k[] = {"load", "detect", "generate", "play", "export"};
  return k[static_cast<int>(s)];
}
const char* QuickStartTracker::stepTitle(QuickStep s) {
  static const char* k[] = {"Load a loop", "Detect chops", "Generate a pattern", "Play it", "Export"};
  return k[static_cast<int>(s)];
}

QuickStartTracker::QuickStartTracker(juce::File file) : file_(std::move(file)) { load(false); }
QuickStartTracker::~QuickStartTracker() { flush(true); }

int QuickStartTracker::doneCount() const {
  int n = 0;
  for (bool b : steps_) n += b ? 1 : 0;
  return n;
}

void QuickStartTracker::load(bool merge) {
  if (!merge) {
    steps_.fill(false);
    dismissed_ = false;
    recents_.clear();
  }
  if (!file_.existsAsFile()) return;
  juce::var v = juce::JSON::parse(file_.loadFileAsString());
  auto* o = v.getDynamicObject();
  if (o == nullptr) {
    DBG("quickstart.json is unreadable; using defaults");
    return;
  }
  if (auto* st = o->getProperty("steps").getDynamicObject())
    for (int i = 0; i < kQuickSteps; ++i)
      if (static_cast<bool>(st->getProperty(stepKey(static_cast<QuickStep>(i))))) steps_[static_cast<std::size_t>(i)] = true;
  if (static_cast<bool>(o->getProperty("dismissed"))) dismissed_ = true;
  if (!merge) {
    if (auto* arr = o->getProperty("recents").getArray())
      for (const auto& r : *arr) {
        const std::string id = r.toString().toStdString();
        if (!id.empty() && id.size() <= 96 && std::find(recents_.begin(), recents_.end(), id) == recents_.end() && recents_.size() < kMaxRecents) recents_.push_back(id);
      }
  }
}

void QuickStartTracker::reload() {
  load(true);
  ++revision_;
}

bool QuickStartTracker::write() {
  auto* o = new juce::DynamicObject();
  auto* st = new juce::DynamicObject();
  for (int i = 0; i < kQuickSteps; ++i) st->setProperty(stepKey(static_cast<QuickStep>(i)), steps_[static_cast<std::size_t>(i)]);
  juce::Array<juce::var> rec;
  for (const auto& r : recents_) rec.add(juce::String(r));
  o->setProperty("version", 1);
  o->setProperty("dismissed", dismissed_);
  o->setProperty("steps", juce::var(st));
  o->setProperty("recents", juce::var(rec));
  if (!file_.getParentDirectory().createDirectory().wasOk()) return false;
  juce::TemporaryFile tmp(file_);  // temp file beside the target, renamed over it
  if (!tmp.getFile().replaceWithText(juce::JSON::toString(juce::var(o)))) return false;
  return tmp.overwriteTargetFileWithTemporary();
}

void QuickStartTracker::flush(bool force) {
  if (!dirty_) return;
  if (!force && juce::Time::getMillisecondCounter() - dirtySince_ < kDebounceMs) return;
  load(true);  // another instance may have finished other steps meanwhile; steps only turn true so the merge converges
  writable_ = write();
  dirty_ = false;
}

namespace {
void touch(bool& dirty, juce::uint32& since, std::uint32_t& rev) {
  if (!dirty) since = juce::Time::getMillisecondCounter();
  dirty = true;
  ++rev;
}
}  // namespace

bool QuickStartTracker::markDone(QuickStep s) {
  auto& slot = steps_[static_cast<std::size_t>(s)];
  if (slot) return false;
  slot = true;
  touch(dirty_, dirtySince_, revision_);
  return true;
}

void QuickStartTracker::setDismissed(bool on) {
  if (dismissed_ == on) return;
  dismissed_ = on;
  if (!on) {
    // a merge on flush must not bring the old dismissal back, so write straight away
    touch(dirty_, dirtySince_, revision_);
    writable_ = write();
    dirty_ = false;
    return;
  }
  touch(dirty_, dirtySince_, revision_);
}

void QuickStartTracker::reset() {
  steps_.fill(false);
  dismissed_ = false;
  ++revision_;
  writable_ = write();  // immediate: a merge must not resurrect the cleared steps
  dirty_ = false;
}

void QuickStartTracker::pushRecent(const std::string& id) {
  if (id.empty()) return;
  recents_.erase(std::remove(recents_.begin(), recents_.end(), id), recents_.end());
  recents_.insert(recents_.begin(), id);
  if (recents_.size() > kMaxRecents) recents_.resize(kMaxRecents);
  touch(dirty_, dirtySince_, revision_);
}

void QuickStartTracker::keepRecents(const std::vector<std::string>& known) {
  const auto before = recents_.size();
  recents_.erase(std::remove_if(recents_.begin(), recents_.end(), [&](const std::string& r) { return std::find(known.begin(), known.end(), r) == known.end(); }), recents_.end());
  if (recents_.size() != before) touch(dirty_, dirtySince_, revision_);
}
