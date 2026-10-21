#include "PluginEditor.h"
#include "BuildInfo.h"

#include <algorithm>
#include <initializer_list>

#include <chopfractal/plugin_ui_adapter/access_text.hpp>
#include <chopfractal/plugin_ui_adapter/command_match.hpp>
#include <chopfractal/plugin_ui_adapter/settings_view.hpp>

#include "ControlHelp.h"
#include "EditorTheme.h"
#include "ShortcutsPanel.h"

namespace cf = chopfractal;

namespace {
constexpr int kLegacyW = 920, kLegacyH = 344;
}

ChopFractalEditor::ChopFractalEditor(ChopFractalProcessor& processor)
    : AudioProcessorEditor(&processor),
      model_(processor),
      commands_(processor, model_),
      chrome_(model_, commands_, processor.apvts),
      wave_(model_, commands_),
      pattern_(model_, commands_),
      macro_(model_, commands_, processor.apvts),
      topBar_(model_, commands_),
      status_(model_, commands_),
      controls_(model_, commands_, processor.apvts),
      features_(model_, commands_),
      history_(model_, commands_),
      historyDrawer_(model_, commands_, history_),
      orbit_(model_, commands_),
      presets_(model_, commands_),
      export_(model_, commands_),
      inspector_(model_, commands_),
      fxRack_(model_, commands_),
      rules_(model_, commands_),
      perform_(processor, model_, commands_),
      registry_(*this),
      quickCard_(model_, commands_, quickStartTracker_),
      palette_(*this, registry_, quickStartTracker_) {
  const auto saved = cdl::loadUserSettings();
  cdl::setCurrentTheme(saved.theme);
  advanced_ = saved.advanced;
  uiScalePercent_ = saved.uiScalePercent;
  cdl::setReducedMotion(saved.reducedMotion);
  lnf_.applyTheme();
  setLookAndFeel(&lnf_);

  // Child order is z-order: the furniture sits at the back, the drawer on top.
  for (juce::Component* c : std::initializer_list<juce::Component*>{&chrome_, &wave_, &pattern_, &macro_, &topBar_, &status_, &drawer_}) addAndMakeVisible(*c);

  // Tools drawer: the older panels, unchanged, at their old relative positions.
  controls_.setBounds(0, 0, 920, 232 - 36 + 30);
  features_.setBounds(0, 232 - 36 + 30 + 6, 920, 104);
  for (juce::Component* c : std::initializer_list<juce::Component*>{&controls_, &features_}) legacyBody_.addAndMakeVisible(*c);  // the Orbit View and the family tree have their own drawers
  legacyBody_.setSize(kLegacyW, kLegacyH);
  legacyView_.setViewedComponent(&legacyBody_, false);
  legacyView_.setScrollBarsShown(true, true);
  legacyView_.setComponentID("ToolsDrawer");

  registerDrawer("presets", "Presets", presets_, 380);
  presets_.onApply = [this](const chopfractal::presets::Preset& p, bool generate) {
    const bool ok = features_.applyPreset(p, generate);
    macro_.resetDriftTicks();
    macro_.setEvolveState(features_.evolveEnabled());
    return ok;
  };
  presets_.onUndo = [this] {
    const bool ok = features_.undoPreset();
    macro_.resetDriftTicks();
    return ok;
  };
  presets_.onCapture = [this](const juce::String& name) { return features_.currentPreset(name); };
  presets_.onApplied = [this](const juce::String& name) { topBar_.setPresetName(name); };
  registerDrawer("export", "Export", export_, 400);
  registerDrawer("tools", "Tools", legacyView_, 420);
  registerMenuDrawer("inspector", "Inspector", inspector_, 320);
  registerMenuDrawer("fx", "Effects", fxRack_, 340);
  registerMenuDrawer("rules", "Rules", rules_, 420);
  registerMenuDrawer("history", "History", historyDrawer_, 440);
  inspector_.onOpenFx = [this] { openDrawer("fx"); };
  pattern_.onEditHit = [this](chopfractal::EventId) { openDrawer("inspector"); };
  registerDrawer("settings", "Settings", settings_, 300, false);
  perform_.registerAll([this](const juce::String& id, const juce::String& title, juce::Component& c, int w) { registerMenuDrawer(id, title, c, w); });
  registerMenuDrawer("orbit", "Orbit", orbit_, 320);
  features_.attachEvolve(&perform_.evolve());
  perform_.commands().openDrawer = [this](const juce::String& id) { openDrawer(id); };
  topBar_.onMenuChoice = [this](const juce::String& id) { toggleDrawer(id); };
  settings_.showTheme(cdl::currentTheme());
  settings_.onTheme = [this](cdl::ThemeId t) { setTheme(t); };
  settings_.onResetSize = [this] { setSize(shell::kDefaultW, shell::kDefaultH); };
  settings_.onScale = [this](int percent) { setUiScalePercent(percent); };
  settings_.onReducedMotion = [this](bool on) { setReducedMotion(on); };
  settings_.onCopyDiagnostics = [this] { commands_.copyDiagnostics(); };
  settings_.showUserSettings(saved);
  drawer_.onChanged = [this] {
    topBar_.setDrawerButtonOn(drawer_.openId());
    resized();
  };

  topBar_.setAdvanced(advanced_);
  topBar_.onTheme = [this] { setTheme(cdl::currentTheme() == cdl::ThemeId::Night ? cdl::ThemeId::Concrete : cdl::ThemeId::Night); };
  topBar_.onSettings = [this] { toggleDrawer("settings"); };
  topBar_.onMode = [this] { setAdvanced(!advanced_); };
  topBar_.onPresetClick = [this] {
    openDrawer("presets");
    presets_.focusSearch();
  };
  macro_.onEvolve = [this](bool on) {
    features_.setEvolveEnabled(on);
    macro_.setEvolveState(features_.evolveEnabled());
  };

  addChildComponent(quickCard_);
  addChildComponent(palette_);  // last child: on top of everything
  commands_.onMilestone = [this](QuickStep s) { quickStartTracker_.markDone(s); };
  quickCard_.onExport = [this] { commands_.exportWav(commands_.exportUi()); };
  quickCard_.onChanged = [this] {
    status_.setQuickStartVisible(!quickStartTracker_.dismissed());
    resized();
  };
  status_.onPalette = [this] { palette_.open(); };
  status_.onQuickStart = [this] { showQuickStart(); };
  status_.setQuickStartVisible(!quickStartTracker_.dismissed());
  // Tab order (F04): top bar, macro strip, waveform, pattern, drawer (controls, history, orbit), status bar.
  topBar_.setExplicitFocusOrder(1);
  macro_.setExplicitFocusOrder(2);
  wave_.setExplicitFocusOrder(3);
  pattern_.setExplicitFocusOrder(4);
  drawer_.setExplicitFocusOrder(5);
  status_.setExplicitFocusOrder(6);
  controls_.setExplicitFocusOrder(1);
  features_.setExplicitFocusOrder(2);
  orbit_.setExplicitFocusOrder(5);
  history_.setExplicitFocusOrder(6);
  controls_.setTitle("Controls");
  features_.setTitle("Features");
  status_.setTitle("Status");
  announcedRevision_ = commands_.noticeLog().revision();

  setWantsKeyboardFocus(true);
  setResizeLimits(shell::kMinW, shell::kMinH, shell::kMaxW, shell::kMaxH);
  setResizable(true, true);
  setSize(saved.width, saved.height);
  savedSize_ = {saved.width, saved.height};
  syncHelpTree();
  model_.refresh(true, wave_.getWidth());
  startTimerHz(10);
}

ChopFractalEditor::~ChopFractalEditor() {
  stopTimer();
  if (shortcutsBox_ != nullptr) shortcutsBox_->dismiss();
  quickStartTracker_.flush(true);
  saveUserState();
  setLookAndFeel(nullptr);
}

void ChopFractalEditor::registerDrawer(const juce::String& id, const juce::String& title, juce::Component& content, int preferredWidth, bool showButton) {
  drawer_.registerDrawer(id, title, content, preferredWidth);
  if (showButton) topBar_.addDrawerButton(id, title, [this](const juce::String& d) { toggleDrawer(d); });
}

void ChopFractalEditor::saveUserState() {
  auto s = cdl::loadUserSettings();
  s.theme = cdl::currentTheme();
  s.advanced = advanced_;
  s.width = juce::jlimit(shell::kMinW, shell::kMaxW, getWidth());
  s.height = juce::jlimit(shell::kMinH, shell::kMaxH, getHeight());
  cdl::saveUserSettings(s);
  savedSize_ = {s.width, s.height};
}

void ChopFractalEditor::setTheme(cdl::ThemeId t) {
  cdl::setCurrentTheme(t);
  lnf_.applyTheme();
  settings_.showTheme(t);
  saveUserState();
  sendLookAndFeelChange();
  repaint();
}

void ChopFractalEditor::setUiScalePercent(int percent) {
  percent = cf::ui::uiScalePercentAt(cf::ui::uiScaleIndex(percent));
  uiScalePercent_ = percent;
  auto s = cdl::loadUserSettings();
  s.uiScalePercent = percent;
  cdl::saveUserSettings(s);
  settings_.showUserSettings(s);
  resized();
  repaint();
}

void ChopFractalEditor::setReducedMotion(bool on) {
  cdl::setReducedMotion(on);
  auto s = cdl::loadUserSettings();
  s.reducedMotion = on;
  cdl::saveUserSettings(s);
  settings_.showUserSettings(s);
  pattern_.setPlaying(pattern_.playing());  // switch between the per-frame and the 10 Hz playhead
}

void ChopFractalEditor::setAdvanced(bool on) {
  if (advanced_ == on) return;
  advanced_ = on;
  topBar_.setAdvanced(on);
  saveUserState();
  resized();
}

void ChopFractalEditor::syncHelpTree() {
  if (auto* root = getTopLevelComponent(); root != tooltipRoot_) {
    tooltipRoot_ = root;
    // Include the standalone wrapper's Options and feedback-notice Settings controls.
    control_help::applyToTree(*root);
  }
}

void ChopFractalEditor::timerCallback() {
  syncHelpTree();
  const bool rebuilt = model_.refresh(false, wave_.getWidth());
  const bool playing = model_.playheadQuarters() >= 0.0;
  if (!rebuilt && (playing || lastPlaying_)) orbit_.repaint();  // the Orbit View shows the playhead too
  if (playing != lastPlaying_) pattern_.setPlaying(playing);
  pattern_.refreshPlayhead();
  lastPlaying_ = playing;
  topBar_.syncTransport();
  status_.refreshReadouts();
  export_.tick();
  observeQuickStart();
  refreshAccessibility();
  // A resize is saved once the size has been stable for a moment, not on every drag step.
  const juce::Point<int> size(getWidth(), getHeight());
  if (size != savedSize_) {
    if (++stableSizeTicks_ >= 3) {
      saveUserState();
      stableSizeTicks_ = 0;
    }
  } else {
    stableSizeTicks_ = 0;
  }
}

juce::String ChopFractalEditor::getTooltip() {
  const auto point = getMouseXYRelative();
  if (wave_.getBounds().contains(point))
    return "Source waveform: click a chop to audition it, drag a marker to move it (it snaps to transients), double-click or Shift-click adds a marker, right-click for marker actions. The wheel zooms, Shift+wheel scrolls. Drop a sample anywhere to load it.";
  if (pattern_.getBounds().contains(point))
    return "Pattern grid: drag a hit to move it, drag its right edge to resize, Alt-drag copies, double-click opens the inspector or adds a hit, right-click for hit actions. Drag empty space to select hits; the wheel scrolls the lanes. M mutes, D duplicates, Delete removes.";
  const auto screen = juce::Desktop::getMousePosition();
  if (history_.isShowing() && history_.getScreenBounds().contains(screen))
    return "Variation family tree: click a node to recall it, at the next loop boundary during playback. Right-click for favorite, rename, compare and delete actions. Each Generate, Mutate, Zoom and completed edit adds a branch.";
  if (orbit_.isShowing() && orbit_.getScreenBounds().contains(screen))
    return "Orbit view: concentric rings show chop lanes and their rhythmic positions. The moving line is the playhead. Click a hit to select it in the pattern.";
  return "ChopFractal: load or drop a loop, Detect Chops, Generate and Play. Hover controls for instructions.";
}

bool ChopFractalEditor::isInterestedInFileDrag(const juce::StringArray& files) {
  for (const auto& path : files)
    if (EditorCommands::isAudioFile(juce::File(path))) return true;
  return false;
}

void ChopFractalEditor::fileDragEnter(const juce::StringArray& files, int, int) { model_.setDropHover(isInterestedInFileDrag(files)); }
void ChopFractalEditor::fileDragExit(const juce::StringArray&) { model_.setDropHover(false); }

void ChopFractalEditor::filesDropped(const juce::StringArray& files, int, int) {
  model_.setDropHover(false);
  commands_.handleDroppedFiles(files);
}

void ChopFractalEditor::paint(juce::Graphics& g) { cdl::paintWindowFrame(g, getLocalBounds()); }

void ChopFractalEditor::resized() {
  const float uiScale = static_cast<float>(uiScalePercent_) / 100.0f;
  const int base = drawer_.isOpen() ? drawer_.openPreferredWidth() : 0;
  // Drawer widths are given at scale 1; the layout scales them with the window.
  const float s0 = shell::scaleFor(getWidth(), getHeight(), uiScale);
  rects_ = shell::computeShellLayout(getWidth(), getHeight(), uiScale, static_cast<int>(std::lround(static_cast<float>(base) * s0)));
  cdl::setUiScale(rects_.scale);
  chrome_.setBounds(getLocalBounds());
  chrome_.setLayout(rects_, advanced_);
  wave_.setBounds(rects_.sourceParts.wave);
  pattern_.setBounds(rects_.patternParts.grid);
  topBar_.setBounds(rects_.topBar);
  topBar_.setScale(rects_.scale);
  macro_.setBounds(rects_.macro);
  macro_.setScale(rects_.scale);
  status_.setBounds(rects_.status);
  status_.setScale(rects_.scale);
  drawer_.setBounds(rects_.drawer);
  quickCard_.setScale(rects_.scale);
  quickCard_.setBounds(juce::Rectangle<int>(quickCard_.preferredSize().x, quickCard_.preferredSize().y).withCentre({wave_.getBounds().getCentreX(), wave_.getBounds().getUnion(pattern_.getBounds()).getCentreY()}).constrainedWithin(wave_.getBounds().getUnion(pattern_.getBounds())));
  palette_.setBounds(getLocalBounds());
  if (wave_.getWidth() > 0 && wave_.getWidth() != lastWaveWidth_) {
    lastWaveWidth_ = wave_.getWidth();
    model_.refresh(true, lastWaveWidth_);  // peaks are computed for the waveform's width
  }
}

// Keyboard map (F04). Letter shortcuts work wherever the focus is except in a text field; arrows, Enter, Delete, Backspace and
// Escape belong to the canvases (and the editor itself), so a focused button or slider keeps them. Keys that are not ours are
// not consumed and the host keeps its own shortcuts.
bool ChopFractalEditor::keyPressed(const juce::KeyPress& k) {
  if (palette_.isVisible()) return palette_.keyPressed(k);  // modal
  auto* focused = juce::Component::getCurrentlyFocusedComponent();
  const bool canvas = focused == nullptr || focused == this || focused == &wave_ || focused == &pattern_ || focused == &orbit_ || focused == &history_ ||
                      wave_.isParentOf(focused) || pattern_.isParentOf(focused) || orbit_.isParentOf(focused) || history_.isParentOf(focused);
  const auto action = mapKey(k, canvas);
  if (!action) return false;
  if (*action == EditorAction::OpenPalette) {
    palette_.open();
    return true;
  }
  if (dynamic_cast<juce::TextEditor*>(focused) != nullptr) return false;
  const auto& v = model_.view();
  const bool shift = k.getModifiers().isShiftDown();
  switch (*action) {
    case EditorAction::Generate: commands_.generate(); return true;
    case EditorAction::Mutate: commands_.mutate(); return true;
    case EditorAction::Undo: commands_.undo(); return true;
    case EditorAction::Redo: commands_.redo(); return true;
    case EditorAction::ZoomIn: commands_.zoomInSelected(); return true;
    case EditorAction::Collapse: commands_.collapseSelected(); return true;
    case EditorAction::ToggleLock: commands_.toggleLockSelectedBar(); return true;
    case EditorAction::Mute:
      if (!model_.hasSelection()) return false;
      commands_.toggleMuteSelected();
      return true;
    case EditorAction::Duplicate:
      if (!model_.hasSelection()) return false;
      commands_.duplicateSelected();
      return true;
    case EditorAction::Delete:
      if (!model_.hasSelection()) return false;
      commands_.deleteSelected();
      return true;
    case EditorAction::PrevHit:
    case EditorAction::NextHit:
    case EditorAction::LaneUp:
    case EditorAction::LaneDown: {
      const bool horizontal = *action == EditorAction::PrevHit || *action == EditorAction::NextHit;
      if (horizontal && model_.editMode()) {  // Move / resize hits is on: the arrows edit, as shipped
        if (!model_.hasSelection()) return false;
        commands_.nudgeSelected(*action == EditorAction::PrevHit ? -1 : 1, shift);
        return true;
      }
      if (v.pattern.rects.empty()) return false;
      const auto dir = *action == EditorAction::PrevHit ? cf::ui::HitDirection::Previous : *action == EditorAction::NextHit ? cf::ui::HitDirection::Next
                       : *action == EditorAction::LaneUp ? cf::ui::HitDirection::LaneUp : cf::ui::HitDirection::LaneDown;
      const auto next = cf::ui::neighborHit(v.pattern, model_.selected(), dir);
      if (next.valid() && next != model_.selected()) {
        announceSelection_ = true;
        model_.selectOnly(next);
        refreshAccessibility();
      }
      return true;
    }
    case EditorAction::Audition:
      auditionSelected();
      return true;
    case EditorAction::Escape:
      if (!model_.hasSelection() && model_.selectedHits().empty() && model_.selectedChops().empty()) return false;
      model_.clearSelection();
      return true;
    case EditorAction::SpaceHint:
      if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone) {  // no host transport here: Space is play / stop
        if (model_.playheadQuarters() >= 0.0) commands_.stop();
        else commands_.play();
        return true;
      }
      if (!spaceHintShown_) {
        spaceHintShown_ = true;
        commands_.say("Playback follows your DAW's transport. Press play in the host to hear the pattern; Enter auditions the selected hit.");
      }
      return false;
    case EditorAction::OpenPalette: break;
  }
  return false;
}

void ChopFractalEditor::auditionSelected() {
  const auto& v = model_.view();
  for (const auto& r : v.pattern.rects)
    if (r.id == model_.selected()) {
      if (r.chopIndex >= 0 && r.chopIndex < static_cast<int>(v.chops.size())) {
        commands_.audition(v.chops[static_cast<std::size_t>(r.chopIndex)].id);
        return;
      }
    }
  commands_.say("Select a hit in the pattern first.");
}

void ChopFractalEditor::toggleEvolve() {
  features_.setEvolveEnabled(!features_.evolveEnabled());
  macro_.setEvolveState(features_.evolveEnabled());
}

void ChopFractalEditor::resetQuickStart() {
  quickStartTracker_.reset();
  commands_.say("Quick start reset.");
  observeQuickStart();
  quickCard_.showNow();
  status_.setQuickStartVisible(true);
  resized();
}

void ChopFractalEditor::showShortcuts() {
  if (shortcutsBox_ != nullptr) {
    shortcutsBox_->dismiss();
    return;
  }
  auto content = std::make_unique<ShortcutsPanel>();
  auto& box = juce::CallOutBox::launchAsynchronously(std::move(content), getLocalArea(&status_, status_.paletteButton().getBounds()), this);
  shortcutsBox_ = &box;
}

// Marks the quick start steps that really happened, whichever control the user used.
void ChopFractalEditor::observeQuickStart() {
  const auto& v = model_.view();
  auto& t = quickStartTracker_;
  if (v.hasSource) t.markDone(QuickStep::Load);
  if (v.hasSource && !v.chops.empty()) t.markDone(QuickStep::Detect);
  if (v.hasPattern) t.markDone(QuickStep::Generate);
  if (v.hasPattern && model_.hostPlaying()) t.markDone(QuickStep::Play);
  if (static_cast<ChopFractalProcessor&>(processor).exportStatus().state == ExportStatus::State::Done) t.markDone(QuickStep::Export);
  quickCard_.tick();
  status_.setQuickStartVisible(!t.dismissed());
  t.flush();
}

void ChopFractalEditor::refreshAccessibility() {
  const auto& v = model_.view();
  std::string selected;
  int hits = 0;
  for (const auto& r : v.pattern.rects) {
    if (r.depth == 0) ++hits;
    if (r.id == model_.selected()) selected = r.description;
  }
  const std::string pattern = cf::ui::describePattern(v.hasPattern, v.barCount, hits, selected);
  wave_.setAccessibleInfo("Waveform and chop markers", juce::String(cf::ui::describeWave(v.hasSource, static_cast<int>(v.chops.size()), static_cast<int>(v.markers.size()))));
  pattern_.setAccessibleInfo("Pattern grid", juce::String(pattern));
  orbit_.setAccessibleInfo("Orbit view", juce::String(pattern));
  std::string active;
  for (const auto& n : v.tree)
    if (n.active) active = n.label;
  history_.setAccessibleInfo("Variation family tree", juce::String(cf::ui::describeHistory(static_cast<int>(v.tree.size()), active)));
  status_.setAccessibleInfo("Status", status_.displayedText());
  if (announceSelection_) {
    announceSelection_ = false;
    if (!selected.empty()) juce::AccessibilityHandler::postAnnouncement(juce::String(selected), juce::AccessibilityHandler::AnnouncementPriority::medium);
  }
  const auto rev = commands_.noticeLog().revision();
  if (rev != announcedRevision_) {
    announcedRevision_ = rev;
    LogEntry latest;
    if (commands_.noticeLog().latest(latest) && latest.text.isNotEmpty())
      juce::AccessibilityHandler::postAnnouncement(latest.shown(), juce::AccessibilityHandler::AnnouncementPriority::medium);
  }
}
