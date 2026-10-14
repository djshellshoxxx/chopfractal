#pragma once
// Crash-safe diagnostic log (message/worker threads only; never call from processBlock).
// One UTF-8 line per event, flushed immediately; files rotate at 1 MiB and at most 7 are kept.
// Logging failures are swallowed. Nothing about user audio or project content is ever logged.
#include <juce_core/juce_core.h>

namespace cflog {
void init();  // opens today's file, prunes old files, detects an unclean previous session
void shutdown();  // removes the session marker
void info(const juce::String& message);
void warn(const juce::String& message);
void error(const juce::String& message);
juce::File folder();
juce::String tail(int lines);
void setFolderForTesting(const juce::File& folder);  // redirects all output; resets state
inline constexpr juce::int64 kMaxFileBytes = 1024 * 1024;
inline constexpr int kMaxFiles = 7;
}  // namespace cflog
