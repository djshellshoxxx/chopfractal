# Alpha 2 focused QA

The shell test `four_essential_workflows_then_35_seconds_of_randomized_qa` checks a real dropped WAV import, standalone Play/Stop, multi-section selection with nested-hit Zoom, and embedded project save/restore. It then runs randomized editor and audio QA for at least 35 seconds of wall time and prints seeds and audio-check counts. GUI runs execute on Windows/macOS and under xvfb on Linux; the headless Linux pass skips GUI-only tests.

Minor follow-ups retained: source selection highlights complete chops and their corresponding pattern hits, rather than creating a destructive audio trim; grouped Move/Resize and grouped Duplicate are not implemented (those controls operate on the primary hit); nested Zoom can partially succeed before a later selected hit fails its depth/lock/budget validation; a locally supplied JUCE checkout does not receive the fetched-checkout-only banner patch. Existing pre-alpha issues remain listed in BUILD_STATUS.md. Real Windows device selection, physical speakers and DAW integration still need human testing.

The standalone banner X hides the notice without changing the audio input mute Value. Status text wraps and scrolls, and can be selected/copied. Plain drag selects; Move / resize hits restores editing gestures. Delete and M apply to all selected pattern hits as one session edit. Drop loading uses the existing worker pool and file size/decoder guards.

## Minor warnings to fix later

The newly added WAV fixtures use JUCE's deprecated `AudioFormat::createWriterFor` overload. Migrate test fixtures to `AudioFormatWriterOptions` in a later cleanup. This is a compiler warning in test setup, not a failure of the product audio loader or renderer.

## Verification record

Revision `80c9c6707931b1fd24c359a7893cbab8f2b99950`: Linux plugin and GUI shell suite passed (26 cases, zero failures), with 35.37 seconds of random QA, 84 sessions and 672 audio checks (seeds 35000 through 35083). Steinberg VST3 validator: 47 passed, zero failed. The final revision adds the hover-help catalog and its coverage test; final Windows and tooltip-suite verification is pending at the time of this entry.
