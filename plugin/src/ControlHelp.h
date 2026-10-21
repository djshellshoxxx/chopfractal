#pragma once
// Central help catalog for the JUCE shell. Keeps user guidance separate from editor behavior.
#include <juce_gui_basics/juce_gui_basics.h>

namespace control_help {
inline juce::String forName(const juce::String& name) {
  struct Entry { const char* name; const char* help; };
  static const Entry entries[] = {
    {"Follow DAW", "Return to the DAW's play/stop, tempo and position after using local Play or Stop. In standalone there is no DAW transport; use Play to start."},
    {"Options", "Open audio/MIDI device settings, save or load a complete project state, or reset ChopFractal to its defaults."},
    {"Settings...", "Choose audio input/output devices and sample rate. The input mute option protects against a feedback loop; generated pattern playback still reaches the output."},
    {"Play", "Start the generated pattern from the beginning using the local tempo. Works without a DAW. Load a loop, Detect Chops and Generate first."},
    {"Stop", "Stop local pattern playback. Press Play to restart from the beginning."},
    {"Move / resize hits", "Off: click or drag a rectangle to select pattern sections. On: drag a hit to move it; drag its right edge to resize. Alt-drag copies within the same bar."},
    {"Load Loop", "Choose a sample up to 60 seconds and 100 MB decoded, or drop a sample into the window. Detect Chops and Generate before Play."},
    {"Detect Chops", "Create chop markers using the detection mode. Replace removes old markers; keep my markers merges detected markers with yours."},
    {"Detection mode", "Choose transient detection or an even 1/8 or 1/16 grid. Select keep my markers to preserve manual markers, then press Detect Chops."},
    {"Generate", "Create a new pattern from enabled chops using Seed, Length, Density, Variation and allowed effects. Locked sections are preserved."},
    {"Mutate", "Vary the current pattern using the next seed and current controls. Locked sections stay unchanged. Undo returns to the previous edit."},
    {"Undo", "Undo the last pattern or marker edit. The family-tree highlight is not currently moved by Undo."},
    {"Redo", "Reapply the last undone edit."},
    {"Zoom In", "Subdivide selected pattern hits into nested rhythms. Click or drag to select first. Waveform selection targets corresponding pattern hits. Locks, depth and event limits may prevent subdivision."},
    {"Collapse", "Replace the primary selected nested hit with its parent hit. Select a zoomed hit first. Locked children prevent collapse."},
    {"Lock Bar", "Toggle the lock on the bar containing the primary selected hit. Locks preserve that bar during Generate, Mutate and Evolve."},
    {"New Seed", "Choose a random seed. Press Generate to create a pattern from it. The same seed and settings reproduce the same pattern."},
    {"Seed", "Enter the integer seed used by Generate. Seed changes do not alter the pattern until you generate it."},
    {"Embed source in project", "Store the sample inside the project so it can reopen without the original file. Larger samples increase project size; otherwise the project references the file."},
    {"Density", "Choose how many hits generation creates. Raise for a busier pattern, then Generate or Mutate."},
    {"Swing", "Delay alternate grid steps to change the groove. Generate or Mutate to apply the value to the pattern."},
    {"Variation", "Set the amount of rhythmic and transform variation used by Generate and Mutate."},
    {"Dry mix", "Mix the live audio input with the generated pattern. Zero plays only the pattern. Standalone input is initially muted to prevent feedback."},
    {"Output gain (dB)", "Adjust output level in decibels. Lower it if the pattern is too loud."},
    {"Pattern length in bars", "Select 1, 2, 4 or 8 bars for the next generated pattern. Uses the host meter when available, otherwise 4/4."},
    {"Length (bars)", "Choose the next generated pattern length with the adjacent bars control."},
    {"Effect on", "Enable pattern rendering. When disabled, the processor passes its input through."},
    {"Reverse", "Allow Generate and Mutate to reverse individual hits. Existing hits change only after generation or mutation."},
    {"Pitch", "Allow pitch variation of generated hits. Pitch is produced by changing playback rate."},
    {"Retrigger", "Allow hits to repeat rapidly inside their duration during Generate or Mutate."},
    {"Filter", "Allow per-hit filtering during Generate or Mutate."},
    {"Tape glide", "Allow per-hit tape-stop and rise effects that change playback speed over the hit."},
    {"Crunch", "Allow per-hit bit and sample-rate reduction effects during Generate or Mutate."},
    {"FX intensity", "Scale the intensity of allowed per-hit effects for Generate or Mutate."},
    {"Chaos", "Push rhythmic variation, density and effects together. Generate or Mutate to apply it; running Evolve uses the current value."},
    {"Reroll", "Choose a new seed and immediately generate a new pattern using the current settings."},
    {"Preset", "Select factory or saved settings. Presets change controls; press Generate to create a pattern from them."},
    {"Save Preset", "Save the current generation and feature settings under a name. This saves settings, not the source sample or complete project."},
    {"Delete", "Delete the selected user preset. Factory presets cannot be deleted."},
    {"Drag MIDI", "Drag this handle into a DAW or file manager to export the current pattern as MIDI for the sliced chops."},
    {"Drag WAV", "Drag this handle into a DAW or file manager to export rendered audio using the export settings."},
    {"Locate Source", "Find the original audio file for a project with missing source audio. Keeps its existing markers, pattern and history."},
    {"Smart Setup", "Analyze the loop to suggest chop roles, tempo and loop length. Suggestions are applied only when you accept them."},
    {"Accept Roles", "Apply the role suggestions from Smart Setup to the chops. Used by later Generate or Mutate actions."},
    {"Use Tempo", "Use Smart Setup's suggested local tempo. The host tempo still takes priority until local Play is used."},
    {"Fractal", "Create self-similar nested rhythms from the motif and depth controls. x is a hit, a is an accent, and . is a rest."},
    {"Motif", "Enter up to eight characters: x for hit, a for accented hit, . for rest. Press Fractal to build the rhythm."},
    {"Fractal motif: x hit, a accent, dot rest", "Enter up to eight characters: x for hit, a for accent, . for rest. Press Fractal to apply."},
    {"Depth", "Choose how many nested rhythm levels Fractal creates. Higher depth uses more of the event budget."},
    {"Fractal depth", "Choose 1 to 3 nested levels, then press Fractal. Higher values create finer rhythmic subdivisions."},
    {"Evolve", "Automatically mutate the pattern during playback. Every sets the cadence; amount sets the change size. Locked parts stay fixed. Keep preserves the current result."},
    {"Every", "Set the number of loops between Evolve changes."},
    {"Evolve every", "Choose how many playback loops pass between automatic Evolve steps."},
    {"Evolve amount", "Adjust how much Evolve changes the rhythm at each step. You can change it while Evolve runs."},
    {"Keep", "Keep the current evolved pattern and stop automatic evolution."},
    {"Export WAV", "Render the current pattern to a WAV file using Format, sample rate and Loops. Requires loaded source audio and a generated pattern."},
    {"Cancel Export", "Stop the running export. Files this export created are removed; nothing is left half written."},
    {"Export Kit", "Export each chop as its own WAV plus a MIDI file that reconstructs the pattern on the slices."},
    {"Format", "Select the bit depth or float format for WAV export."},
    {"Export format", "Choose 16-bit or 24-bit PCM, or 32-bit float for the exported WAV."},
    {"Export sample rate", "Choose the sample rate for rendered WAV export."},
    {"Loops", "Choose how many repetitions the exported WAV contains."},
    {"Export loops", "Choose how many complete repetitions of the pattern to render into the WAV."},
    {"Store A", "Store the current pattern in comparison slot A. The snapshots are saved with the project."},
    {"Recall A", "Recall comparison pattern A. It may be unavailable if its source chops have changed."},
    {"Store B", "Store the current pattern in comparison slot B. The snapshots are saved with the project."},
    {"Recall B", "Recall comparison pattern B. It may be unavailable if its source chops have changed."},
    {"Role for the selected hit's chop", "Choose a role such as kick, snare or hat for the primary selected hit's source chop. Press Set Role to apply."},
    {"Set Role", "Apply the chosen role to the primary selected hit's source chop. The role is used during future Generate or Mutate."},
    {"Status messages", "Full status and error messages. Use the mouse wheel or scrollbar to read long text. Select text to copy it."},
    {"Load Loop > Detect Chops > Generate > Play", "Start by loading or dropping a loop, detecting chops, generating a pattern, and pressing Play."},
  };
  for (const auto& entry : entries)
    if (name == entry.name) return juce::String(entry.help);
  return {};
}
inline void applyToTree(juce::Component& component) {
  if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(&component)) {
    auto name = component.getTitle();
    if (auto* button = dynamic_cast<juce::Button*>(&component)) name = button->getButtonText();
    if (auto* label = dynamic_cast<juce::Label*>(&component)) name = label->getText();
    const auto help = forName(name);
    if (help.isNotEmpty()) client->setTooltip(help);
  }
  for (int i = 0; i < component.getNumChildComponents(); ++i)
    applyToTree(*component.getChildComponent(i));
}
}
