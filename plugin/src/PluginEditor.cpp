#include "PluginEditor.h"
#include "BuildInfo.h"

#include <algorithm>
#include <initializer_list>

#include "ControlHelp.h"
#include "EditorTheme.h"

namespace cf = chopfractal;

namespace {
constexpr int kLegacyW = 920, kLegacyH = 460;
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
      orbit_(model_, commands_) {
  const auto saved = cdl::loadUserSettings();
  cdl::setCurrentTheme(saved.theme);
  advanced_ = saved.advanced;
  lnf_.applyTheme();
  setLookAndFeel(&lnf_);

  // Child order is z-order: the furniture sits at the back, the drawer on top.
  for (juce::Component* c : std::initializer_list<juce::Component*>{&chrome_, &wave_, &pattern_, &macro_, &topBar_, &status_, &drawer_}) addAndMakeVisible(*c);

  // Tools drawer: the older panels, unchanged, at their old relative positions.
  controls_.setBounds(0, 0, 920, 232 - 36 + 30);
  features_.setBounds(0, 232 - 36 + 30 + 6, 920, 104);
  history_.setBounds(0, 340, 808, 106);
  orbit_.setBounds(814, 340, 106, 106);
  for (juce::Component* c : std::initializer_list<juce::Component*>{&controls_, &features_, &history_, &orbit_}) legacyBody_.addAndMakeVisible(*c);
  legacyBody_.setSize(kLegacyW, kLegacyH);
  legacyView_.setViewedComponent(&legacyBody_, false);
  legacyView_.setScrollBarsShown(true, true);
  legacyView_.setComponentID("ToolsDrawer");

  registerDrawer("tools", "Tools", legacyView_, 560);
  registerDrawer("settings", "Settings", settings_, 300, false);
  settings_.showTheme(cdl::currentTheme());
  settings_.onTheme = [this](cdl::ThemeId t) { setTheme(t); };
  settings_.onResetSize = [this] { setSize(shell::kDefaultW, shell::kDefaultH); };
  drawer_.onChanged = [this] {
    topBar_.setDrawerButtonOn(drawer_.openId());
    resized();
  };

  topBar_.setAdvanced(advanced_);
  topBar_.onTheme = [this] { setTheme(cdl::currentTheme() == cdl::ThemeId::Night ? cdl::ThemeId::Concrete : cdl::ThemeId::Night); };
  topBar_.onSettings = [this] { toggleDrawer("settings"); };
  topBar_.onMode = [this] { setAdvanced(!advanced_); };
  topBar_.onPresetClick = [this] { openDrawer("tools"); };
  macro_.onEvolve = [this](bool on) {
    features_.setEvolveEnabled(on);
    macro_.setEvolveState(features_.evolveEnabled());
  };

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
  lastPlaying_ = playing;
  topBar_.setPresetName(features_.presetName());
  topBar_.syncTransport();
  status_.refreshReadouts();
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
    return "Source waveform: click a chop to select and audition it; drag across sections to select several. Shift-click adds a marker; Alt-click removes the nearest marker. Drop a sample anywhere to load it.";
  if (pattern_.getBounds().contains(point))
    return "Pattern grid: each lane is a chop. Click a hit or drag a rectangle to select sections. Zoom In subdivides selected hits. M toggles selected hits; Delete removes them. Enable Move / resize hits to edit by dragging.";
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

void ChopFractalEditor::filesDropped(const juce::StringArray& files, int, int) { commands_.handleDroppedFiles(files); }

void ChopFractalEditor::paint(juce::Graphics& g) { cdl::paintWindowFrame(g, getLocalBounds()); }

void ChopFractalEditor::resized() {
  const float uiScale = 1.0f;
  const int base = drawer_.isOpen() ? drawer_.openPreferredWidth() : 0;
  // Drawer widths are given at scale 1; the layout scales them with the window.
  const float s0 = shell::scaleFor(getWidth(), getHeight(), uiScale);
  rects_ = shell::computeShellLayout(getWidth(), getHeight(), uiScale, static_cast<int>(std::lround(static_cast<float>(base) * s0)));
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
  if (wave_.getWidth() > 0 && wave_.getWidth() != lastWaveWidth_) {
    lastWaveWidth_ = wave_.getWidth();
    model_.refresh(true, lastWaveWidth_);  // peaks are computed for the waveform's width
  }
}

// Keyboard equivalents of the drag gestures work on the selected hit.
bool ChopFractalEditor::keyPressed(const juce::KeyPress& k) {
  if (!model_.hasSelection()) return false;
  const auto mods = k.getModifiers();
  if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown()) return false;  // leave the host's own shortcuts alone
  if (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey) {
    commands_.nudgeSelected(k.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1, mods.isShiftDown());
    return true;
  }
  if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) {
    commands_.deleteSelected();
    return true;
  }
  if (k.getTextCharacter() == 'm' || k.getTextCharacter() == 'M') {
    commands_.toggleMuteSelected();
    return true;
  }
  if (k.getTextCharacter() == 'd' || k.getTextCharacter() == 'D') {
    commands_.duplicateSelected();
    return true;
  }
  return false;
}
