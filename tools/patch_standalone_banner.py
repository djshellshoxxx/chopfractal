#!/usr/bin/env python3
"""Add a dismiss-only button to JUCE 8.0.15's standalone mute notification.

This changes presentation only: the holder's mute Value is never written.
Run at configure time on the fetched JUCE checkout; mismatched versions fail loudly.
"""
from pathlib import Path
import sys

MARKER = "CHOPFRACTAL_DISMISS_MUTE_NOTICE"
REPLACEMENTS = [
    ('notification (this),',
     'notification (this, [this] { notificationDismissed = true; inputMutedChanged (true); }),'),
    ('NotificationArea (Button::Listener* settingsButtonListener)',
     'NotificationArea (Button::Listener* settingsButtonListener, std::function<void()> dismiss)'),
    ('                addAndMakeVisible (settingsButton);',
     '                addAndMakeVisible (settingsButton);\n'
     '                dismissButton.onClick = std::move (dismiss);\n'
     '                dismissButton.setTooltip ("Hide this notice. Audio input remains muted.");\n'
     '                addAndMakeVisible (dismissButton);'),
    ('                settingsButton.setBounds (r.removeFromRight (70));',
     '                dismissButton.setBounds (r.removeFromRight (28));\n'
     '                r.removeFromRight (5);\n'
     '                settingsButton.setBounds (r.removeFromRight (70));'),
    ('            TextButton settingsButton;',
     '            TextButton settingsButton;\n'
     '            TextButton dismissButton { "X" }; // ' + MARKER),
    ('            shouldShowNotification = newInputMutedValue;',
     '            if (! newInputMutedValue) notificationDismissed = false;\n'
     '            shouldShowNotification = newInputMutedValue && ! notificationDismissed;'),
    ('        bool shouldShowNotification = false;',
     '        bool notificationDismissed = false;\n'
     '        bool shouldShowNotification = false;'),
]


def patch(text):
    if MARKER in text:
        return text
    for before, after in REPLACEMENTS:
        if text.count(before) != 1:
            raise ValueError("Unsupported JUCE standalone header: " + before.strip())
        text = text.replace(before, after, 1)
    return text


if __name__ == "__main__":
    header = Path(sys.argv[1])
    original = header.read_text(encoding="utf-8")
    updated = patch(original)
    if updated != original:
        header.write_text(updated, encoding="utf-8")
