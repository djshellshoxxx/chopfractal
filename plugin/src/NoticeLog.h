#pragma once
// The session's notice log (F05): every warning and error the plugin raised, newest last, with a severity so it is not
// colour only. Capacity 100. Identical consecutive entries within two seconds collapse into one with a counter. Thread
// safe (status messages also arrive from file-loading callbacks), but the audio thread never touches it.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <juce_core/juce_core.h>

enum class Severity : std::uint8_t { Info = 0, Warning = 1, Error = 2 };

struct LogEntry {
  juce::Time time;
  Severity severity = Severity::Info;
  juce::String text;  // as raised
  int count = 1;      // collapsed duplicates
  juce::String shown() const { return count > 1 ? text + " (x" + juce::String(count) + ")" : text; }
};

class NoticeLog {
 public:
  static constexpr std::size_t kCapacity = 100;
  void add(Severity, const juce::String& text);
  std::vector<LogEntry> snapshot() const;           // oldest first
  bool latest(LogEntry& out) const;                 // false while empty
  std::size_t size() const;
  void clear();
  std::uint64_t revision() const;                   // changes with every add, collapse or clear
  void setClock(std::function<juce::int64()> millis);  // tests: a controllable millisecond clock

 private:
  mutable juce::CriticalSection lock_;
  std::vector<LogEntry> entries_;
  std::uint64_t revision_ = 0;
  juce::int64 lastAddMillis_ = 0;
  std::function<juce::int64()> clock_;
};
