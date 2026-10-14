# Alpha 2 focused QA

The shell test `four_essential_workflows_then_35_seconds_of_randomized_qa` checks a real dropped WAV import, standalone Play/Stop, multi-section selection with nested-hit Zoom, and embedded project save/restore. It then runs randomized editor and audio QA for at least 35 seconds of wall time and prints seeds and audio-check counts. GUI runs execute on Windows/macOS and under xvfb on Linux; the headless Linux pass skips GUI-only tests.

Minor follow-ups retained: source selection highlights complete chops and their corresponding pattern hits, rather than creating a destructive audio trim; grouped Move/Resize and grouped Duplicate are not implemented (those controls operate on the primary hit); nested Zoom can partially succeed before a later selected hit fails its depth/lock/budget validation; a locally supplied JUCE checkout does not receive the fetched-checkout-only banner patch. Existing pre-alpha issues remain listed in BUILD_STATUS.md. Real Windows device selection, physical speakers and DAW integration still need human testing.

The standalone banner X hides the notice without changing the audio input mute Value. Status text wraps and scrolls, and can be selected/copied. Plain drag selects; Move / resize hits restores editing gestures. Delete and M apply to all selected pattern hits as one session edit. Drop loading uses the existing worker pool and file size/decoder guards.

## Minor warnings to fix later

The newly added WAV fixtures use JUCE's deprecated `AudioFormat::createWriterFor` overload. Migrate test fixtures to `AudioFormatWriterOptions` in a later cleanup. This is a compiler warning in test setup, not a failure of the product audio loader or renderer.

## Verification record

Revision `80c9c6707931b1fd24c359a7893cbab8f2b99950`: Linux plugin and GUI shell suite passed (26 cases, zero failures), with 35.37 seconds of random QA, 84 sessions and 672 audio checks (seeds 35000 through 35083). Steinberg VST3 validator: 47 passed, zero failed. The final revision adds the hover-help catalog and its coverage test; final Windows and tooltip-suite verification is pending at the time of this entry.

## Final Windows and Linux verification

Release source `80444369b31ff8d3b55a9860064a00925cc23c12` includes Follow DAW and a regression test that returns from local preview to the host's play/stop, tempo and position. Windows: 28 shell cases, zero failures; timed QA 35.13 seconds, 82 sessions and 656 audio checks, seeds 35000 through 35081. Linux: 28 cases with and without a display, zero failures; timed GUI QA 35.01 seconds, 83 sessions and 664 audio checks; Steinberg validator 47 passed, zero failed. The four essential workflows are checked before the timed QA portion.

Additional minor follow-ups from the quick review, intentionally deferred:

- The random button picker still samples its original 18 button entries. Play/Stop and Follow DAW have dedicated functional tests; extend the random picker to the new transport controls later.
- Local transport changes currently reset sounding voices immediately. Add a short stop fade as transport polish.
- Potential rare command-coalescing edge: Follow DAW resets the local command epoch. If Follow DAW and Play occur before another audio callback, a reused command number could preserve the old clock. Keep the epoch monotonic across mode changes in a later transport pass. Normal GUI use and the tested host transition pass.

Major issue fixed during review: local preview originally had no way to return to DAW transport. Follow DAW now restores that behavior, verified by the new processor test.
