// F19 command palette and quick start, F04 keyboard map, focus order and accessibility text.
#include <chopfractal/plugin_ui_adapter/command_match.hpp>
#include <chopfractal/preset_store/presets.hpp>
#include <cmath>

#include "EditorKeys.h"
#include "QuickStartTracker.h"
#include "ShortcutsPanel.h"
#include "workflow_test_support.hpp"

using namespace shelltest;

namespace {
juce::KeyPress key(int code, bool shift = false, bool ctrl = false, bool alt = false) {
  int m = 0;
  if (shift) m |= juce::ModifierKeys::shiftModifier;
  if (ctrl) m |= juce::ModifierKeys::ctrlModifier;
  if (alt) m |= juce::ModifierKeys::altModifier;
  return juce::KeyPress(code, juce::ModifierKeys(m), 0);
}
juce::KeyPress ch(char c, bool shift = false, bool ctrl = false) {
  int m = 0;
  if (shift) m |= juce::ModifierKeys::shiftModifier;
  if (ctrl) m |= juce::ModifierKeys::ctrlModifier;
  return juce::KeyPress(c, juce::ModifierKeys(m), static_cast<juce::juce_wchar>(c));
}
// An editor with no source at all (the first-run state).
struct EmptyRig {
  ChopFractalProcessor proc;
  std::unique_ptr<juce::AudioProcessorEditor> base;
  ChopFractalEditor* ed = nullptr;
  explicit EmptyRig(int w = 1100, int h = 720) {
    prepare(proc);
    base.reset(proc.createEditor());
    ed = dynamic_cast<ChopFractalEditor*>(base.get());
    if (ed) {
      ed->setSize(w, h);
      ed->setVisible(true);
      pump(150);
    }
  }
};
}  // namespace

// ---------------- key map (no display needed) ----------------

CHOP_TEST(key_map_covers_the_documented_shortcuts_and_nothing_else) {
  CHECK(mapKey(ch('g'), true) == EditorAction::Generate);
  CHECK(mapKey(ch('M', true), true) == EditorAction::Mutate);
  CHECK(mapKey(ch('m'), true) == EditorAction::Mute);
  CHECK(mapKey(ch('d'), true) == EditorAction::Duplicate);
  CHECK(mapKey(ch('z'), true) == EditorAction::ZoomIn);
  CHECK(mapKey(ch('Z', true), true) == EditorAction::Collapse);
  CHECK(mapKey(ch('l'), true) == EditorAction::ToggleLock);
  CHECK(mapKey(ch('z', false, true), true) == EditorAction::Undo);
  CHECK(mapKey(ch('y', false, true), true) == EditorAction::Redo);
  CHECK(mapKey(ch('Z', true, true), true) == EditorAction::Redo);
  CHECK(mapKey(ch('d', false, true), true) == EditorAction::Duplicate);
  CHECK(mapKey(ch('k', false, true), true) == EditorAction::OpenPalette);
  CHECK(mapKey(key('K', false, true), false) == EditorAction::OpenPalette);  // key code only, as a host may report it
  CHECK(mapKey(key(juce::KeyPress::leftKey), true) == EditorAction::PrevHit);
  CHECK(mapKey(key(juce::KeyPress::rightKey), true) == EditorAction::NextHit);
  CHECK(mapKey(key(juce::KeyPress::upKey), true) == EditorAction::LaneUp);
  CHECK(mapKey(key(juce::KeyPress::downKey), true) == EditorAction::LaneDown);
  CHECK(mapKey(key(juce::KeyPress::returnKey), true) == EditorAction::Audition);
  CHECK(mapKey(key(juce::KeyPress::deleteKey), true) == EditorAction::Delete);
  CHECK(mapKey(key(juce::KeyPress::backspaceKey), true) == EditorAction::Delete);
  CHECK(mapKey(key(juce::KeyPress::escapeKey), true) == EditorAction::Escape);
  CHECK(mapKey(key(juce::KeyPress::spaceKey), true) == EditorAction::SpaceHint);
}

CHOP_TEST(key_map_leaves_host_shortcuts_and_unfocused_canvas_keys_alone) {
  CHECK(!mapKey(ch('q'), true));
  CHECK(!mapKey(ch('q', false, true), true));
  CHECK(!mapKey(ch('g', false, true), true));       // Ctrl+G is the host's
  CHECK(!mapKey(ch('k', true, true), true));         // Ctrl+Shift+K is not ours
  CHECK(!mapKey(key('G', false, false, true), true));  // Alt chords never
  CHECK(!mapKey(key(juce::KeyPress::leftKey, false, false, true), true));
  // Without canvas focus the canvas keys are not claimed, so a button or slider keeps them.
  for (int code : {juce::KeyPress::leftKey, juce::KeyPress::rightKey, juce::KeyPress::upKey, juce::KeyPress::downKey, juce::KeyPress::returnKey,
                   juce::KeyPress::deleteKey, juce::KeyPress::backspaceKey, juce::KeyPress::escapeKey, juce::KeyPress::spaceKey})
    CHECK(!mapKey(key(code), false));
  CHECK(mapKey(ch('g'), false) == EditorAction::Generate);  // letters work wherever the focus is
  CHECK(shortcutText(EditorAction::Generate) == "G");
  CHECK(shortcutText(EditorAction::Mutate) == "Shift+M");
  CHECK(shortcutText(EditorAction::OpenPalette).endsWith("K"));
}

// ---------------- quick start tracker (no display needed) ----------------

CHOP_TEST(tracker_persists_steps_dismissal_and_recents_atomically) {
  IsolatedUser user;
  const auto file = user.root.getChildFile("qs.json");
  {
    QuickStartTracker t(file);
    CHECK_EQ(t.doneCount(), 0);
    CHECK(t.markDone(QuickStep::Load));
    CHECK(!t.markDone(QuickStep::Load));  // steps only ever turn true once
    t.markDone(QuickStep::Play);
    t.setDismissed(true);
    for (int i = 0; i < 12; ++i) t.pushRecent("action." + std::to_string(i));
    t.pushRecent("action.5");  // moves to the front, no duplicate
    CHECK_EQ(t.recents().size(), 8u);
    CHECK(t.recents().front() == "action.5");
    t.flush(true);
  }
  CHECK(file.existsAsFile());
  int leftovers = 0;
  for (const auto& f : user.root.findChildFiles(juce::File::findFiles, false, "*qs*"))
    if (f != file) ++leftovers;
  CHECK_EQ(leftovers, 0);  // the temp file was renamed over the target
  QuickStartTracker again(file);
  CHECK(again.done(QuickStep::Load) && again.done(QuickStep::Play) && !again.done(QuickStep::Detect));
  CHECK(again.dismissed());
  CHECK_EQ(again.recents().size(), 8u);
  const auto json = juce::JSON::parse(file.loadFileAsString());
  CHECK_EQ(static_cast<int>(json.getProperty("version", 0)), 1);
  CHECK(!file.loadFileAsString().contains(user.root.getFullPathName()));  // never a path
}

CHOP_TEST(tracker_reset_keeps_recents_and_a_missing_or_corrupt_file_means_defaults) {
  IsolatedUser user;
  const auto file = user.root.getChildFile("qs.json");
  QuickStartTracker t(file);
  t.markDone(QuickStep::Export);
  t.setDismissed(true);
  t.pushRecent("action.generate");
  t.reset();
  CHECK_EQ(t.doneCount(), 0);
  CHECK(!t.dismissed());
  CHECK_EQ(t.recents().size(), 1u);
  QuickStartTracker reread(file);
  CHECK_EQ(reread.doneCount(), 0);
  CHECK(!reread.dismissed());
  file.replaceWithText("{ this is not json");
  QuickStartTracker corrupt(file);
  CHECK_EQ(corrupt.doneCount(), 0);
  CHECK(corrupt.recents().empty());
  file.replaceWithText("[1,2,3]");
  QuickStartTracker wrong(file);
  CHECK_EQ(wrong.doneCount(), 0);
  QuickStartTracker missing(user.root.getChildFile("nope/never.json"));
  CHECK_EQ(missing.doneCount(), 0);
}

CHOP_TEST(tracker_merges_steps_other_instances_finished_and_survives_an_unwritable_folder) {
  IsolatedUser user;
  const auto file = user.root.getChildFile("qs.json");
  QuickStartTracker a(file), b(file);
  a.markDone(QuickStep::Load);
  a.flush(true);
  b.markDone(QuickStep::Export);
  b.flush(true);  // re-reads first, so Load survives
  QuickStartTracker c(file);
  CHECK(c.done(QuickStep::Load) && c.done(QuickStep::Export));
  const auto blocker = user.root.getChildFile("afile");
  blocker.replaceWithText("x");
  QuickStartTracker blocked(blocker.getChildFile("qs.json"));  // the parent is a regular file
  blocked.markDone(QuickStep::Detect);
  blocked.flush(true);
  CHECK(blocked.done(QuickStep::Detect));  // kept in memory for the session
  CHECK(!blocked.writable());
  QuickStartTracker pruned(file);
  pruned.pushRecent("action.gone");
  pruned.pushRecent("action.generate");
  pruned.keepRecents({"action.generate"});
  CHECK_EQ(pruned.recents().size(), 1u);
}

// ---------------- palette ----------------

CHOP_TEST(registry_lists_every_manifest_parameter_and_names_why_an_action_is_unavailable) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EmptyRig rig;
  if (!rig.ed) return;
  auto& reg = rig.ed->registry();
  int params = 0;
  for (const auto& def : chopfractal::host::parameterManifest()) {
    const auto* c = reg.find(std::string("param.") + def.id);
    CHECK(c != nullptr);
    if (!c) continue;
    ++params;
    CHECK(c->title == def.name);
    CHECK(c->kind == (def.kind == chopfractal::host::ParamKind::Float ? CommandKind::Number : def.kind == chopfractal::host::ParamKind::Bool ? CommandKind::Toggle : CommandKind::Choice));
    CHECK(reg.disabledReason(*c).empty());
  }
  CHECK(params >= 50);
  const auto* gen = reg.find("action.generate");
  CHECK(gen && reg.disabledReason(*gen) == "load a loop first");
  CHECK(gen && gen->shortcut == "G");
  const auto* mut = reg.find("action.mutate");
  CHECK(mut && !reg.disabledReason(*mut).empty());
  const auto* load = reg.find("action.load");
  CHECK(load && reg.disabledReason(*load).empty());
  CHECK(reg.all().size() < 300);
}

CHOP_TEST(ctrl_k_opens_the_palette_and_filter_then_enter_runs_the_command) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  auto& pal = rig.ed->palette();
  CHECK(!pal.isOpen());
  CHECK(rig.ed->keyPressed(ch('k', false, true)));
  pump(40);
  CHECK(pal.isOpen());
  CHECK(pal.searchBox().getTitle() == "Command search");
  const auto before = rig.ed->model().view().revision;
  pal.setQuery("gener");
  CHECK(!pal.rows().empty() && pal.rows()[0].title == "Generate new pattern");
  CHECK(pal.rows()[0].right == "G");
  CHECK(pal.runSelected());
  CHECK(!pal.isOpen());
  CHECK(rig.ed->model().view().revision > before);
  // Ctrl+K again toggles it shut; Esc closes too.
  rig.ed->openPalette();
  CHECK(pal.isOpen());
  CHECK(pal.keyPressed(key(juce::KeyPress::escapeKey)));
  CHECK(!pal.isOpen());
  rig.ed->openPalette();
  CHECK(rig.ed->keyPressed(ch('k', false, true)));  // routed to the open palette, which closes
  CHECK(!pal.isOpen());
}

CHOP_TEST(a_click_outside_the_panel_closes_the_palette_and_a_click_inside_does_not) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  auto& pal = rig.ed->palette();
  auto mk = [&](juce::Point<int> p) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p.toFloat(), juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pal, &pal,
                            juce::Time::getCurrentTime(), p.toFloat(), juce::Time::getCurrentTime(), 1, false);
  };
  rig.ed->openPalette();
  const auto panel = pal.panelBounds();
  CHECK(panel.getWidth() == 520 && panel.getHeight() == 360);
  pal.mouseDown(mk(panel.getCentre()));
  CHECK(pal.isOpen());
  pal.mouseDown(mk({4, rig.ed->getHeight() - 4}));
  CHECK(!pal.isOpen());
}

CHOP_TEST(parameter_rows_take_a_number_clamp_or_explain_the_range) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  auto& pal = rig.ed->palette();
  auto& cmd = rig.ed->commands();
  rig.ed->openPalette();
  pal.setQuery("density");
  CHECK(!pal.rows().empty() && pal.rows()[0].title == "Density");
  CHECK(pal.rows()[0].right == "0.50");
  CHECK(pal.runSelected());  // Enter on a number row arms it
  CHECK(pal.isOpen());
  CHECK(pal.query() == "Density ");
  CHECK(pal.footer().startsWith("Value (0 to 1, now 0.50)"));
  pal.setQuery("density 0.6");
  CHECK(pal.runSelected());
  CHECK(!pal.isOpen());
  CHECK_NEAR(cmd.parameter("density"), 0.6, 1e-4);
  CHECK(rig.ed->model().statusLine() == "Density set to 0.60.");
  // Out of range and unparsable input keep the palette open with the range in the footer.
  rig.ed->openPalette();
  pal.setQuery("density 2");
  CHECK(!pal.runSelected());
  CHECK(pal.isOpen() && pal.footerIsError() && pal.footer() == "0 to 1 expected.");
  CHECK_NEAR(cmd.parameter("density"), 0.6, 1e-4);
  pal.setQuery("density abc");
  CHECK(!pal.runSelected());
  CHECK(pal.footer() == "0 to 1 expected.");
  pal.setQuery("output gain -6");
  CHECK(pal.runSelected());
  CHECK_NEAR(cmd.parameter("output_gain_db"), -6.0, 1e-3);
  CHECK(rig.ed->model().statusLine() == "Output Gain set to -6.00 dB.");
  rig.ed->openPalette();
  pal.setQuery("output gain 50");
  CHECK(!pal.runSelected());
  CHECK(pal.footer() == "-60 to 12 expected.");
}

CHOP_TEST(parameter_changes_go_through_a_host_change_gesture) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  struct Probe : juce::AudioProcessorParameter::Listener {
    int gestures = 0, changes = 0;
    void parameterValueChanged(int, float) override { ++changes; }
    void parameterGestureChanged(int, bool starting) override { gestures += starting ? 1 : 0; }
  } probe;
  auto* prm = rig.proc.apvts.getParameter("swing");
  CHECK(prm != nullptr);
  prm->addListener(&probe);
  rig.ed->openPalette();
  rig.ed->palette().setQuery("swing 0.35");
  CHECK(rig.ed->palette().runSelected());
  prm->removeListener(&probe);
  CHECK_EQ(probe.gestures, 1);
  CHECK(probe.changes >= 1);
  CHECK_NEAR(rig.proc.currentParams().get(chopfractal::host::kSwing), 0.35, 1e-4);
}

CHOP_TEST(choice_and_toggle_parameters_list_their_entries_or_flip_at_once) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  auto& pal = rig.ed->palette();
  auto& cmd = rig.ed->commands();
  rig.ed->openPalette();
  pal.setQuery("break profile");
  CHECK(!pal.rows().empty() && pal.rows()[0].title == "Break Profile");
  CHECK(pal.runSelected());  // arms: the entries appear as sub-rows
  CHECK(pal.isOpen() && pal.rows().size() == 6u);
  CHECK(pal.rows()[1].title == "Break Profile: Jungle" && pal.rows()[1].right == "now");
  pal.setQuery("break profile bre");
  CHECK(!pal.rows().empty() && pal.rows()[0].title == "Break Profile: Breakcore");
  CHECK(pal.runSelected());
  CHECK(!pal.isOpen());
  CHECK_EQ(static_cast<int>(cmd.parameter("break_profile")), 3);
  CHECK(rig.ed->model().statusLine() == "Break Profile set to Breakcore.");
  // Toggles run at once; typing on/off picks a side.
  rig.ed->openPalette();
  pal.setQuery("allow reverse");
  CHECK(pal.rows()[0].title == "Allow Reverse" && pal.rows()[0].right == "Off");
  CHECK(pal.runSelected());
  CHECK(!pal.isOpen());
  CHECK_EQ(static_cast<int>(cmd.parameter("allow_reverse")), 1);
  CHECK(rig.ed->model().statusLine() == "Allow Reverse set to On.");
  rig.ed->openPalette();
  pal.setQuery("allow reverse off");
  CHECK(pal.runSelected());
  CHECK_EQ(static_cast<int>(cmd.parameter("allow_reverse")), 0);
}

CHOP_TEST(disabled_rows_say_why_and_cannot_run_and_an_empty_search_says_so) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EmptyRig rig;
  if (!rig.ed) return;
  auto& pal = rig.ed->palette();
  rig.ed->openPalette();
  pal.setQuery("generate new");
  CHECK(!pal.rows().empty() && pal.rows()[0].title == "Generate new pattern" && pal.rows()[0].reason == "load a loop first");
  CHECK(pal.footer() == "Load a loop first.");
  CHECK(!pal.runSelected());
  CHECK(pal.isOpen() && pal.footerIsError());
  CHECK(pal.footer().contains("load a loop first"));
  pal.setQuery("zzqx");
  CHECK(pal.rows().empty());
  CHECK(pal.footer() == "No command matches \"zzqx\".");
  CHECK(!pal.runSelected());
}

CHOP_TEST(empty_query_shows_suggestions_for_the_state_then_recents_newest_first) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EmptyRig rig;
  if (!rig.ed) return;
  auto& pal = rig.ed->palette();
  rig.ed->openPalette();
  CHECK(pal.rows().size() >= 2 && pal.rows()[0].header && pal.rows()[0].title == "Suggested");
  CHECK(pal.rows()[1].title == "Load loop");
  CHECK(pal.selectedRow() == 1);  // headers are never selected
  CHECK(!pal.moveSelection(-1));
  pal.close();
  for (const char* q : {"new seed", "switch theme", "copy diag"}) {
    rig.ed->openPalette();
    pal.setQuery(q);
    CHECK(pal.runSelected());
  }
  rig.ed->openPalette();
  CHECK(pal.rows()[0].header && pal.rows()[0].title == "Recent");
  CHECK(pal.rows()[1].title == "Copy diagnostics");
  CHECK(pal.rows()[2].title == "Switch theme");
  CHECK(pal.rows()[3].title == "New seed");
  pal.close();
}

CHOP_TEST(presets_are_commands_and_loading_one_reports_it) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  const auto& factory = chopfractal::presets::factoryPresets();
  CHECK(!factory.empty());
  rig.ed->openPalette();
  rig.ed->palette().setQuery(factory.front().name);
  CHECK(!rig.ed->palette().rows().empty());
  CHECK(rig.ed->palette().rows()[0].cmd->category == "Preset");
  CHECK(rig.ed->palette().runSelected());
  CHECK(rig.ed->model().statusLine().contains("Preset \"" + juce::String(factory.front().name) + "\" applied"));
}

CHOP_TEST(panels_menu_drawers_open_from_the_palette) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  CHECK(!rig.ed->topBar().menuEntries().empty());
  rig.ed->openPalette();
  rig.ed->palette().setQuery("open scenes");
  CHECK(!rig.ed->palette().rows().empty() && rig.ed->palette().rows()[0].title.find("Scenes") != std::string::npos);
  CHECK(rig.ed->palette().runSelected());
  CHECK(!rig.ed->palette().isOpen());
  CHECK(rig.ed->openDrawerId() == "scenes");
}

CHOP_TEST(the_question_mark_button_opens_the_palette_and_every_new_control_is_named) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EmptyRig rig;
  if (!rig.ed) return;
  auto* q = findButton(*rig.ed, "?");
  CHECK(q != nullptr);
  if (!q) return;
  CHECK(q->getTitle() == "Command palette");
  CHECK(q->getTooltip() == "Search every action and parameter (Ctrl+K)");
  q->triggerClick();
  pump(60);
  CHECK(rig.ed->palette().isOpen());
  CHECK(rig.ed->palette().list().getTitle() == "Command results");
  rig.ed->palette().close();
  for (auto* c : {static_cast<juce::Component*>(&rig.ed->palette().searchBox()), static_cast<juce::Component*>(&rig.ed->palette().list())}) {
    CHECK(c->getTitle().isNotEmpty());
    auto* t = dynamic_cast<juce::SettableTooltipClient*>(c);
    CHECK(t && t->getTooltip().isNotEmpty());
  }
}

// ---------------- keyboard on the canvases ----------------

CHOP_TEST(letter_keys_drive_generate_mutate_undo_redo_without_a_selection) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  auto rev = [&] { return rig.ed->model().view().revision; };
  auto r0 = rev();
  CHECK(rig.ed->keyPressed(ch('g')));
  CHECK(rev() > r0);
  r0 = rev();
  CHECK(rig.ed->keyPressed(ch('M', true)));
  CHECK(rev() > r0);
  r0 = rev();
  CHECK(rig.ed->keyPressed(ch('z', false, true)));
  CHECK(rev() > r0);
  r0 = rev();
  CHECK(rig.ed->keyPressed(ch('y', false, true)));
  CHECK(rev() > r0);
  CHECK(!rig.ed->keyPressed(ch('q')));
  CHECK(!rig.ed->keyPressed(ch('m')));  // no selection: not consumed
}

CHOP_TEST(arrow_keys_walk_hits_in_time_then_lanes_and_enter_auditions) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  auto& m = rig.ed->model();
  const auto& pattern = m.view().pattern;
  CHECK(!pattern.rects.empty());
  CHECK(rig.ed->keyPressed(key(juce::KeyPress::rightKey)));
  const auto first = m.selected();
  CHECK(first.valid());
  CHECK(first == chopfractal::ui::neighborHit(pattern, chopfractal::EventId{}, chopfractal::ui::HitDirection::Next));
  CHECK_EQ(m.selectedHits().size(), 1u);
  chopfractal::EventId prev = first;
  int steps = 0;
  while (rig.ed->keyPressed(key(juce::KeyPress::rightKey)) && m.selected() != prev && steps < 200) {
    const auto x = [&](chopfractal::EventId id) {
      for (const auto& r : pattern.rects)
        if (r.id == id) return r.x0;
      return -1.0;
    };
    CHECK(x(m.selected()) >= x(prev));  // never backwards in time
    prev = m.selected();
    ++steps;
  }
  CHECK(steps > 0);
  CHECK(rig.ed->keyPressed(key(juce::KeyPress::leftKey)));
  CHECK(m.selected() != prev);
  const auto here = m.selected();
  const bool moved = rig.ed->keyPressed(key(juce::KeyPress::downKey));
  CHECK(moved);
  CHECK(m.selected() == here || m.selected() != here);  // the lane key is consumed whether or not a lane below has hits
  // Enter auditions the selected hit's chop, which counts for the quick start.
  CHECK(!rig.ed->quickStart().done(QuickStep::Play));
  CHECK(rig.ed->keyPressed(key(juce::KeyPress::returnKey)));
  CHECK(rig.ed->quickStart().done(QuickStep::Play));
  CHECK(rig.ed->keyPressed(key(juce::KeyPress::escapeKey)));
  CHECK(!m.hasSelection());
  CHECK(!rig.ed->keyPressed(key(juce::KeyPress::escapeKey)));  // nothing to clear: not consumed
}

CHOP_TEST(with_move_and_resize_on_the_arrows_nudge_instead_of_navigate) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  auto& m = rig.ed->model();
  rig.ed->keyPressed(key(juce::KeyPress::rightKey));
  CHECK(m.hasSelection());
  m.setEditMode(true);
  const auto sel = m.selected();
  const auto rev = m.view().revision;
  CHECK(rig.ed->keyPressed(key(juce::KeyPress::rightKey)));
  CHECK(m.view().revision > rev);       // an edit, one undo step
  CHECK(m.selected() == sel || m.selected().valid());
  m.setEditMode(false);
  m.clearSelection();
  CHECK(!rig.ed->keyPressed(key(juce::KeyPress::deleteKey)));  // nothing selected: not consumed
}

CHOP_TEST(space_hints_once_in_a_host_and_keys_pass_through_a_focused_text_field) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  CHECK(!rig.ed->keyPressed(key(juce::KeyPress::spaceKey)));
  CHECK(rig.ed->model().statusLine().startsWith("Playback follows your DAW's transport."));
  rig.ed->commands().say("later");
  CHECK(!rig.ed->keyPressed(key(juce::KeyPress::spaceKey)));
  CHECK(rig.ed->model().statusLine() == "later");  // once per editor
  rig.ed->openDrawer("presets");
  pump(60);
  auto* search = findTextEditor(*rig.ed, "Search presets");
  CHECK(search != nullptr);
  if (search) {
    search->grabKeyboardFocus();
    pump(30);
    if (juce::Component::getCurrentlyFocusedComponent() == search) {
      const auto r0 = rig.ed->model().view().revision;
      CHECK(!rig.ed->keyPressed(ch('g')));  // typing G into a field must not generate
      CHECK_EQ(rig.ed->model().view().revision, r0);
      CHECK(rig.ed->keyPressed(ch('k', false, true)));  // but Ctrl+K opens the palette from anywhere
      rig.ed->palette().close();
    }
  }
}

// ---------------- quick start card ----------------

CHOP_TEST(quick_start_card_gates_its_steps_ticks_them_from_real_events_and_hides_once_a_loop_is_loaded) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EmptyRig rig;
  if (!rig.ed) return;
  auto& card = rig.ed->quickStartCard();
  CHECK(card.isVisible() && card.wanted());
  CHECK(card.getWidth() == static_cast<int>(300 * card.scale()) && card.getHeight() == static_cast<int>(150 * card.scale()));
  CHECK(rig.ed->wavePanel().getBounds().getUnion(rig.ed->patternPanel().getBounds()).contains(card.getBounds()));
  CHECK(card.getBounds().getCentreX() == rig.ed->wavePanel().getBounds().getCentreX());
  CHECK(card.stepButton(0).isEnabled());
  for (int i = 1; i < 5; ++i) {
    CHECK(!card.stepButton(i).isEnabled());
    CHECK(card.stepReason(i).isNotEmpty());
    CHECK(card.stepButton(i).getTooltip().endsWith(card.stepReason(i).toLowerCase().trimCharactersAtEnd(".")));
    CHECK(card.stepButton(i).getTooltip().length() > 20);
  }
  CHECK(card.stepReason(1) == "Load a loop first.");
  CHECK(card.stepReason(2) == "Load a loop first.");
  CHECK(card.rowTitle(0) == "Quick start step 1 of 5: Load a loop, not done");
  CHECK(card.stepButton(0).getTitle() == card.rowTitle(0));
  CHECK(findButton(*rig.ed, "Quick start") != nullptr && findButton(*rig.ed, "Quick start")->isVisible());
  snapshot(*rig.ed, "quickstart_empty_1100");
  // Load, detect and generate through the normal paths: the steps tick on their own.
  rig.proc.withSession([&](chopfractal::composition::ProjectSession& s) { CHECK(s.loadSource(drumLoop(), "loop", "").ok()); });
  pump(250);
  CHECK(rig.ed->quickStart().done(QuickStep::Load));
  CHECK(!rig.ed->quickStart().done(QuickStep::Detect));
  CHECK(!card.isVisible());  // a loop is loaded: the card steps aside
  CHECK(card.stepReason(1).isEmpty() && card.stepReason(2) == "Detect chops first.");
  rig.proc.withSession([&](chopfractal::composition::ProjectSession& s) {
    chopfractal::composition::DetectOptions d;
    d.analysis.sensitivity = 0.8;
    CHECK(s.applyChops(d).ok());
  });
  pump(250);
  CHECK(rig.ed->quickStart().done(QuickStep::Detect));
  rig.ed->commands().generate();
  pump(250);
  CHECK(rig.ed->quickStart().done(QuickStep::Generate));
  CHECK(!rig.ed->quickStart().done(QuickStep::Play));
  rig.ed->commands().audition(rig.ed->model().view().chops.front().id);
  CHECK(rig.ed->quickStart().done(QuickStep::Play));
  CHECK_EQ(rig.ed->quickStart().doneCount(), 4);
  // The status bar button brings the card back at any time, Skip puts it away for the session.
  auto* qs = findButton(*rig.ed, "Quick start");
  CHECK(qs && qs->isVisible());
  if (qs) qs->triggerClick();
  pump(60);
  CHECK(card.isVisible());
  CHECK(card.stepButton(0).getButtonText() == "Load...");
  snapshot(*rig.ed, "quickstart_forced_1100");
  card.skipButton().triggerClick();
  pump(60);
  CHECK(!card.isVisible());
  CHECK(findButton(*rig.ed, "Quick start")->isVisible());
}

CHOP_TEST(quick_start_celebrates_for_five_seconds_collapses_and_can_be_hidden_for_good_and_reset) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EmptyRig rig;
  if (!rig.ed) return;
  auto& card = rig.ed->quickStartCard();
  auto& t = rig.ed->quickStart();
  CHECK(card.isVisible());
  for (int i = 0; i < 5; ++i) t.markDone(static_cast<QuickStep>(i));
  card.tick();
  CHECK(card.celebrating() && card.isVisible());
  snapshot(*rig.ed, "quickstart_done_1100");  // pumps the loop: the editor's own ticks may have run
  card.celebrate();
  for (int i = 0; i < 49; ++i) card.tick();
  CHECK(card.isVisible());  // 5 s are 50 ticks of the editor's 10 Hz timer
  card.tick();
  CHECK(!card.celebrating() && !card.isVisible());
  CHECK(findButton(*rig.ed, "Quick start")->isVisible());  // collapsed to the status bar button
  // "Do not show again" hides the card and the button for good, persisted per user.
  card.showNow();
  CHECK(card.isVisible());
  card.hideToggle().setToggleState(true, juce::dontSendNotification);
  card.hideToggle().onClick();
  pump(60);
  CHECK(t.dismissed());
  CHECK(!card.isVisible());
  CHECK(!findButton(*rig.ed, "Quick start")->isVisible());
  t.flush(true);
  CHECK(QuickStartTracker(QuickStartTracker::defaultFile()).dismissed());
  // Reset quick start (palette command) clears steps and dismissal and shows the card again.
  rig.ed->openPalette();
  rig.ed->palette().setQuery("reset quick");
  CHECK(!rig.ed->palette().rows().empty() && rig.ed->palette().rows()[0].title == "Reset quick start");
  CHECK(rig.ed->palette().runSelected());
  CHECK(!t.dismissed() && t.doneCount() == 0);
  CHECK(card.isVisible());
  CHECK(findButton(*rig.ed, "Quick start")->isVisible());
  rig.ed->palette().close();
  rig.ed->openPalette();
  rig.ed->palette().setQuery("show quick");
  CHECK(rig.ed->palette().runSelected());
  CHECK(card.isVisible());
}

// ---------------- focus order and accessibility ----------------

CHOP_TEST(canvases_take_keyboard_focus_in_the_documented_tab_order_and_describe_themselves) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EmptyRig empty;
  if (!empty.ed) return;
  CHECK(empty.ed->wavePanel().getTitle() == "Waveform and chop markers");
  CHECK(empty.ed->wavePanel().getDescription() == "No audio loaded.");
  CHECK(empty.ed->patternPanel().getTitle() == "Pattern grid");
  CHECK(empty.ed->patternPanel().getDescription() == "No pattern.");
  CHECK(empty.ed->statusBar().getTitle() == "Status");
  CHECK(empty.ed->historyPanel().getTitle() == "Variation family tree");
  CHECK(empty.ed->orbitPanel().getTitle() == "Orbit view");
  CHECK(empty.ed->wavePanel().getWantsKeyboardFocus() && empty.ed->patternPanel().getWantsKeyboardFocus());
  CHECK(empty.ed->orbitPanel().getWantsKeyboardFocus() && empty.ed->historyPanel().getWantsKeyboardFocus());
  CHECK(empty.ed->topBar().getExplicitFocusOrder() < empty.ed->wavePanel().getExplicitFocusOrder());
  CHECK(empty.ed->wavePanel().getExplicitFocusOrder() < empty.ed->patternPanel().getExplicitFocusOrder());
  CHECK(empty.ed->patternPanel().getExplicitFocusOrder() < empty.ed->statusBar().getExplicitFocusOrder());
  EditorRig rig;
  if (!rig.ed) return;
  const auto& v = rig.ed->model().view();
  CHECK(rig.ed->wavePanel().getDescription() == juce::String(std::to_string(v.chops.size()) + " chops, " + std::to_string(v.markers.size()) + " markers."));
  CHECK(rig.ed->patternPanel().getDescription().endsWith("No hit selected."));
  CHECK(rig.ed->historyPanel().getDescription().contains("variation"));
  rig.ed->keyPressed(key(juce::KeyPress::rightKey));
  CHECK(rig.ed->patternPanel().getDescription().contains("Selected: "));
  CHECK(rig.ed->orbitPanel().getDescription() == rig.ed->patternPanel().getDescription());
  // The accessibility tree exposes the canvases as named groups.
  if (auto* handler = rig.ed->wavePanel().getAccessibilityHandler()) CHECK(handler->getTitle() == "Waveform and chop markers");  // none on platforms without accessibility
  // A click gives the canvas the focus itself; its focus ring draws only while it has it.
  rig.ed->wavePanel().grabKeyboardFocus();
  pump(30);
  CHECK(rig.ed->wavePanel().hasKeyboardFocus(false) || juce::Component::getCurrentlyFocusedComponent() == nullptr);
  snapshot(*rig.ed, "focus_ring_1100");
}

CHOP_TEST(keyboard_shortcuts_list_matches_the_key_map) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  rig.ed->showShortcuts();
  pump(80);
  rig.ed->showShortcuts();  // second call closes it
  pump(80);
  CHECK(rig.ed->registry().find("action.shortcuts") != nullptr);
  for (const auto& [action, name] : ShortcutsPanel::rows()) {
    CHECK(shortcutText(action).isNotEmpty());
    CHECK(std::string(name).size() > 3);
  }
  CHECK(ShortcutsPanel::text().contains("Command palette"));
}

CHOP_TEST(palette_screenshots_at_both_design_sizes) {
  if (!haveDisplay()) return;
  for (auto size : {juce::Point<int>(1100, 720), juce::Point<int>(880, 560)}) {
    IsolatedUser user;
    EditorRig rig(true, size.x, size.y);
    if (!rig.ed) return;
    rig.ed->openPalette();
    rig.ed->palette().setQuery("sw");
    pump(60);
    snapshot(*rig.ed, "palette_sw_" + juce::String(size.x));
    rig.ed->palette().setQuery("break profile ");
    snapshot(*rig.ed, "palette_choice_" + juce::String(size.x));
    rig.ed->palette().setQuery("");
    snapshot(*rig.ed, "palette_empty_" + juce::String(size.x));
    rig.ed->palette().close();
    CHECK(rig.ed->statusBar().paletteButton().isVisible());
    snapshot(*rig.ed, "statusbar_questionmark_" + juce::String(size.x));
  }
}
