#include "NoticeLog.h"

#include <algorithm>
#include <cstddef>
#include <utility>

void NoticeLog::add(Severity severity, const juce::String& text) {
  if (text.isEmpty()) return;
  const juce::ScopedLock lock(lock_);
  const juce::int64 now = clock_ ? clock_() : juce::Time::currentTimeMillis();
  ++revision_;
  if (!entries_.empty()) {
    auto& last = entries_.back();
    if (last.severity == severity && last.text == text && now - lastAddMillis_ <= 2000) {
      ++last.count;
      last.time = juce::Time(now);
      lastAddMillis_ = now;
      return;
    }
  }
  LogEntry e;
  e.time = juce::Time(now);
  e.severity = severity;
  e.text = text;
  entries_.push_back(std::move(e));
  if (entries_.size() > kCapacity) entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - kCapacity));
  lastAddMillis_ = now;
}

std::vector<LogEntry> NoticeLog::snapshot() const {
  const juce::ScopedLock lock(lock_);
  return entries_;
}

bool NoticeLog::latest(LogEntry& out) const {
  const juce::ScopedLock lock(lock_);
  if (entries_.empty()) return false;
  out = entries_.back();
  return true;
}

std::size_t NoticeLog::size() const {
  const juce::ScopedLock lock(lock_);
  return entries_.size();
}

void NoticeLog::clear() {
  const juce::ScopedLock lock(lock_);
  entries_.clear();
  ++revision_;
}

std::uint64_t NoticeLog::revision() const {
  const juce::ScopedLock lock(lock_);
  return revision_;
}

void NoticeLog::setClock(std::function<juce::int64()> millis) {
  const juce::ScopedLock lock(lock_);
  clock_ = std::move(millis);
}
