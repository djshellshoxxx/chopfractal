#pragma once
// First-run checklist state (F19): which of the five quick-start steps the user has ever completed, whether the card was
// dismissed for good, and the palette's recent commands. Per user (quickstart.json next to presets and ui.json), never part
// of a project or of the plugin state. A missing or corrupt file means defaults; an unwritable folder keeps the state in memory.
#include <juce_core/juce_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class QuickStep { Load = 0, Detect, Generate, Play, Export };
constexpr int kQuickSteps = 5;

class QuickStartTracker {
 public:
  explicit QuickStartTracker(juce::File file = defaultFile());
  ~QuickStartTracker();

  static juce::File defaultFile();
  static void setFileOverride(const juce::File&);  // tests: keep the real user file untouched

  static const char* stepKey(QuickStep);      // "load", "detect", "generate", "play", "export"
  static const char* stepTitle(QuickStep);    // "Load a loop", "Detect chops", "Generate a pattern", "Play it", "Export"

  bool done(QuickStep s) const { return steps_[static_cast<std::size_t>(s)]; }
  int doneCount() const;
  bool allDone() const { return doneCount() == kQuickSteps; }
  // Steps only ever turn true. Returns true when this call changed it. Saves within about half a second.
  bool markDone(QuickStep);
  bool dismissed() const { return dismissed_; }
  void setDismissed(bool);
  void reset();  // clears the steps and the dismissal; recents stay

  const std::vector<std::string>& recents() const { return recents_; }  // newest first, at most 8
  void pushRecent(const std::string& id);
  void keepRecents(const std::vector<std::string>& knownIds);  // drops ids that no longer exist

  // Re-reads the file (steps and dismissal merge: a step another instance finished stays finished).
  void reload();
  // Writes when something changed and `force` or the debounce time has passed. Called from the editor's 10 Hz tick.
  void flush(bool force = false);
  bool dirty() const { return dirty_; }
  std::uint32_t revision() const { return revision_; }  // bumps on every visible change
  bool writable() const { return writable_; }

 private:
  void load(bool merge);
  bool write();

  juce::File file_;
  std::array<bool, kQuickSteps> steps_{};
  bool dismissed_ = false;
  std::vector<std::string> recents_;
  bool dirty_ = false, writable_ = true;
  juce::uint32 dirtySince_ = 0;
  std::uint32_t revision_ = 0;
};
