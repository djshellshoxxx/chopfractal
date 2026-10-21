#pragma once
// Every action, host parameter and preset the command palette (F19) can run. The registry holds no behaviour of its own: each
// command calls the function the matching button calls, so messages, undo steps and enable checks are identical. Parameters come
// from the host manifest, so a new parameter appears here without any edit.
#include <chopfractal/plugin_host_adapter/parameters.hpp>
#include <functional>
#include <string>
#include <vector>

class ChopFractalEditor;

enum class CommandKind { Action, Number, Toggle, Choice, Preset };

struct Command {
  std::string id;        // "action.generate", "param.swing", "preset.factory:Name"
  std::string title;
  std::string category;  // "Action", "Parameter", "Preset", "Help"
  std::string shortcut;  // display text, empty when none
  std::vector<std::string> keywords;
  CommandKind kind = CommandKind::Action;
  const chopfractal::host::ParamDef* param = nullptr;  // Number, Toggle and Choice
  std::string presetKey;                                // Preset
  std::function<std::string()> reason;                  // why the command cannot run now (lower case, no final period), empty = enabled
  std::function<void()> run;                            // Action and Preset
};

class CommandRegistry {
 public:
  explicit CommandRegistry(ChopFractalEditor&);

  const std::vector<Command>& all() const { return commands_; }
  const Command* find(const std::string& id) const;
  void refreshPresets();  // the preset list changes while the editor lives

  // Empty when the command can run.
  std::string disabledReason(const Command& c) const { return c.reason ? c.reason() : std::string(); }
  // The right-hand column: the current value of a parameter, else the shortcut.
  std::string valueText(const Command&) const;
  std::string rangeText(const Command&) const;                 // "0 to 1"
  std::vector<std::string> choiceLabels(const Command&) const;  // Choice and Toggle ("Off", "On")
  // Parameter commands. Both report through the status bar ("Swing set to 0.60.") and return false with `error` for bad input.
  bool setNumber(const Command&, const std::string& text, std::string& error);
  bool setChoice(const Command&, int index);
  bool toggle(const Command&);
  bool runCommand(const Command&, std::string& error);          // Action, Preset and Toggle; error is the disabled reason

  static std::string formatValue(const chopfractal::host::ParamDef&, double plain);

 private:
  void add(Command);
  ChopFractalEditor& ed_;
  std::vector<Command> commands_;
  std::size_t fixedCount_ = 0;
};
