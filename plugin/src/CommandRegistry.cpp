#include "CommandRegistry.h"

#include <chopfractal/plugin_ui_adapter/command_match.hpp>
#include <cmath>
#include <cstdio>

#include "EditorKeys.h"
#include "PluginEditor.h"

namespace cf = chopfractal;

namespace {
std::string trimmedNumber(double v) {
  char b[48];
  std::snprintf(b, sizeof b, "%.2f", v);
  return b;
}
}  // namespace

std::string CommandRegistry::formatValue(const cf::host::ParamDef& d, double plain) {
  const juce::String unit(d.unit);
  if (d.kind == cf::host::ParamKind::Bool) return plain >= 0.5 ? "On" : "Off";
  if (d.kind == cf::host::ParamKind::Choice) {
    juce::StringArray labels;
    labels.addTokens(d.choices ? d.choices : "", "|", "");
    const int i = juce::jlimit(0, std::max(0, labels.size() - 1), static_cast<int>(std::lround(plain)));
    return (labels[i] + (unit.isNotEmpty() ? " " + unit : juce::String())).toStdString();
  }
  std::string t = d.step >= 1.0 ? std::to_string(static_cast<long long>(std::llround(plain))) : trimmedNumber(plain);
  if (unit.isNotEmpty() && !(unit == "%" && d.maxValue <= 1.0)) t += (unit == "%" ? "%" : " " + unit.toStdString());
  return t;
}

CommandRegistry::CommandRegistry(ChopFractalEditor& ed) : ed_(ed) {
  auto& cmd = ed.commands();
  auto& model = ed.model();
  auto needSource = [&model]() -> std::string { return model.view().hasSource ? std::string() : std::string("load a loop first"); };
  auto needChops = [&model]() -> std::string {
    if (!model.view().hasSource) return "load a loop first";
    return model.view().chops.empty() ? "detect chops first" : std::string();
  };
  auto needPattern = [&model]() -> std::string {
    if (!model.view().hasSource) return "load a loop first";
    return model.view().hasPattern ? std::string() : std::string("generate a pattern first");
  };
  auto needHit = [&model]() -> std::string { return model.hasSelection() ? std::string() : std::string("select a hit first"); };
  auto act = [this](std::string id, std::string title, std::string shortcut, std::vector<std::string> kw, std::function<std::string()> reason, std::function<void()> run, std::string category = "Action") {
    Command c;
    c.id = "action." + id;
    c.title = std::move(title);
    c.category = std::move(category);
    c.shortcut = std::move(shortcut);
    c.keywords = std::move(kw);
    c.reason = std::move(reason);
    c.run = std::move(run);
    add(std::move(c));
  };
  const auto sc = [](EditorAction a) { return shortcutText(a).toStdString(); };
  auto* edp = &ed;

  act("load", "Load loop", "", {"open", "sample", "wav", "file"}, nullptr, [&cmd] { cmd.chooseFile(); });
  act("locate", "Locate source", "", {"missing", "relink"}, [&model]() -> std::string { return model.sourceMissing() ? std::string() : std::string("the source audio is not missing"); }, [&cmd] { cmd.locateSource(); });
  act("detect", "Detect chops", "", {"markers", "transients", "slice"}, needSource, [&cmd, &model] { cmd.detectChops(static_cast<DetectChoice>(model.detectMode())); });
  act("setup", "Smart setup", "", {"roles", "suggest", "tempo"}, needChops, [&cmd] { cmd.smartSetup(); });
  act("generate", "Generate new pattern", sc(EditorAction::Generate), {"pattern", "make", "create"}, needChops, [&cmd] { cmd.generate(); });
  act("mutate", "Mutate pattern", sc(EditorAction::Mutate), {"vary", "variation"}, needPattern, [&cmd] { cmd.mutate(); });
  act("reroll", "Reroll with new seed", "", {"random", "again"}, needChops, [&cmd] { cmd.reroll(); });
  act("reseed", "New seed", "", {"random", "reseed"}, nullptr, [&cmd] { cmd.newSeed(); });
  act("undo", "Undo", sc(EditorAction::Undo), {"back", "revert"}, nullptr, [&cmd] { cmd.undo(); });
  act("redo", "Redo", sc(EditorAction::Redo), {"forward"}, nullptr, [&cmd] { cmd.redo(); });
  act("lock", "Lock or unlock bar", sc(EditorAction::ToggleLock), {"freeze", "protect"}, needHit, [&cmd] { cmd.toggleLockSelectedBar(); });
  act("zoomin", "Zoom into selected hit", sc(EditorAction::ZoomIn), {"subdivide", "nested"}, needHit, [&cmd] { cmd.zoomInSelected(); });
  act("collapse", "Collapse selected hit", sc(EditorAction::Collapse), {"unzoom"}, needHit, [&cmd] { cmd.collapseSelected(); });
  act("mute", "Mute or unmute selected hit", sc(EditorAction::Mute), {"toggle"}, needHit, [&cmd] { cmd.toggleMuteSelected(); });
  act("duplicate", "Duplicate selected hit", sc(EditorAction::Duplicate), {"copy"}, needHit, [&cmd] { cmd.duplicateSelected(); });
  act("delete", "Delete selected hit", sc(EditorAction::Delete), {"remove"}, needHit, [&cmd] { cmd.deleteSelected(); });
  act("audition", "Audition selected hit", sc(EditorAction::Audition), {"hear", "preview", "play"}, needHit, [edp] { edp->auditionSelected(); });
  act("play", "Play pattern", "", {"start", "transport"}, needPattern, [&cmd] { cmd.play(); });
  act("stop", "Stop playback", "", {"pause", "transport"}, nullptr, [&cmd] { cmd.stop(); });
  act("follow", "Follow DAW transport", "", {"host", "sync"}, nullptr, [&cmd] { cmd.followHost(); });
  act("evolve", "Toggle Evolve", "", {"morph", "auto"}, needPattern, [edp] { edp->toggleEvolve(); });
  act("exportwav", "Export WAV", "", {"render", "bounce", "audio"}, needPattern, [&cmd] { cmd.exportWav(cmd.exportUi()); });
  act("exportkit", "Export Producer Kit", "", {"slices", "midi", "folder"}, needPattern, [&cmd] { cmd.exportKit(); });
  act("exportdrawer", "Open export settings", "", {"midi", "arrangement", "format", "bit depth"}, nullptr, [edp] { edp->openDrawer("export"); });
  act("presets", "Open presets", "", {"browse", "styles"}, nullptr, [edp] {
    edp->openDrawer("presets");
    edp->presetBrowser().focusSearch();
  });
  act("savepreset", "Save preset", "", {"store", "name"}, nullptr, [edp] {
    edp->openDrawer("presets");
    edp->presetBrowser().askSave();
  });
  act("tools", "Open tools", "", {"groove", "outputs", "pads", "capture", "history"}, nullptr, [edp] { edp->openDrawer("tools"); });
  act("settings", "Open settings", "", {"scale", "motion", "defaults", "preferences"}, nullptr, [edp] { edp->openDrawer("settings"); });
  act("theme", "Switch theme", "", {"night", "concrete", "dark", "light"}, nullptr, [edp] { edp->setTheme(cdl::currentTheme() == cdl::ThemeId::Night ? cdl::ThemeId::Concrete : cdl::ThemeId::Night); });
  act("view", "Switch Simple and Advanced view", "", {"mode"}, nullptr, [edp] { edp->setAdvanced(!edp->advanced()); });
  act("diagnostics", "Copy diagnostics", "", {"support", "bug", "report"}, nullptr, [&cmd] { cmd.copyDiagnostics(); }, "Help");
  act("quickstart", "Show quick start", "", {"checklist", "first run", "help"}, nullptr, [edp] { edp->showQuickStart(); }, "Help");
  act("resetquickstart", "Reset quick start", "", {"checklist", "first run"}, nullptr, [edp] { edp->resetQuickStart(); }, "Help");
  act("shortcuts", "Show keyboard shortcuts", "", {"keys", "keyboard", "help"}, nullptr, [edp] { edp->showShortcuts(); }, "Help");

  // Every manifest parameter, with the same change gestures as a drag.
  for (const auto& def : cf::host::parameterManifest()) {
    Command c;
    c.id = std::string("param.") + def.id;
    c.title = def.name;
    c.category = "Parameter";
    c.param = &def;
    c.kind = def.kind == cf::host::ParamKind::Float ? CommandKind::Number : def.kind == cf::host::ParamKind::Bool ? CommandKind::Toggle : CommandKind::Choice;
    c.keywords = {def.id};
    add(std::move(c));
  }
  fixedCount_ = commands_.size();
  refreshPresets();
}

void CommandRegistry::add(Command c) { commands_.push_back(std::move(c)); }

void CommandRegistry::refreshPresets() {
  commands_.resize(fixedCount_);
  auto* edp = &ed_;
  for (const auto& item : ed_.presetBrowser().items()) {
    if (!item.loadable) continue;
    Command c;
    c.id = "preset." + item.key;
    c.title = item.name;
    c.category = "Preset";
    c.kind = CommandKind::Preset;
    c.presetKey = item.key;
    c.keywords = {"load preset", item.category};
    c.run = [edp, key = item.key] { edp->presetBrowser().loadKey(juce::String(key), edp->presetBrowser().applyAndGenerate()); };
    commands_.push_back(std::move(c));
  }
  // The drawers in the Panels menu (Capture, Tempo, Pads, Scenes, Evolve, Outputs, Arrange, Orbit) open from the palette too.
  for (const auto& [id, title] : ed_.topBar().menuEntries()) {
    Command c;
    c.id = "action.open." + id.toStdString();
    c.title = "Open " + title.toStdString() + " panel";
    c.category = "Action";
    c.keywords = {id.toStdString(), "drawer", "panel"};
    c.run = [edp, id] { edp->openDrawer(id); };
    commands_.push_back(std::move(c));
  }
}

const Command* CommandRegistry::find(const std::string& id) const {
  for (const auto& c : commands_)
    if (c.id == id) return &c;
  return nullptr;
}

std::string CommandRegistry::valueText(const Command& c) const {
  if (c.param != nullptr) return formatValue(*c.param, ed_.commands().parameter(c.param->id));
  if (c.kind == CommandKind::Preset) return {};
  return c.shortcut;
}

std::string CommandRegistry::rangeText(const Command& c) const {
  if (c.param == nullptr) return {};
  const auto t = cf::ui::rangeErrorText(c.param->minValue, c.param->maxValue);
  return t.substr(0, t.size() - std::string(" expected.").size());
}

std::vector<std::string> CommandRegistry::choiceLabels(const Command& c) const {
  std::vector<std::string> out;
  if (c.param == nullptr) return out;
  if (c.param->kind == cf::host::ParamKind::Bool) return {"Off", "On"};
  juce::StringArray labels;
  labels.addTokens(c.param->choices ? c.param->choices : "", "|", "");
  for (const auto& l : labels) out.push_back(l.toStdString());
  return out;
}

bool CommandRegistry::setNumber(const Command& c, const std::string& text, std::string& error) {
  if (c.param == nullptr || c.kind != CommandKind::Number) return false;
  double v = 0;
  if (!cf::ui::parseNumberArg(text, c.param->minValue, c.param->maxValue, v, error)) {
    error = cf::ui::rangeErrorText(c.param->minValue, c.param->maxValue);
    return false;
  }
  const double now = ed_.commands().setParameter(c.param->id, v);
  ed_.model().setStatus(juce::String(c.title) + " set to " + juce::String(formatValue(*c.param, now)) + ".");
  return true;
}

bool CommandRegistry::setChoice(const Command& c, int index) {
  if (c.param == nullptr) return false;
  const auto labels = choiceLabels(c);
  if (index < 0 || index >= static_cast<int>(labels.size())) return false;
  const double now = ed_.commands().setParameter(c.param->id, static_cast<double>(index));
  ed_.model().setStatus(juce::String(c.title) + " set to " + juce::String(formatValue(*c.param, now)) + ".");
  return true;
}

bool CommandRegistry::toggle(const Command& c) {
  if (c.param == nullptr) return false;
  return setChoice(c, ed_.commands().parameter(c.param->id) >= 0.5 ? 0 : 1);
}

bool CommandRegistry::runCommand(const Command& c, std::string& error) {
  const std::string why = disabledReason(c);
  if (!why.empty()) {
    error = why;
    return false;
  }
  if (c.kind == CommandKind::Toggle) return toggle(c);
  if (c.kind == CommandKind::Number || c.kind == CommandKind::Choice) {
    error = "A value is needed.";
    return false;
  }
  if (c.run) c.run();
  return true;
}
