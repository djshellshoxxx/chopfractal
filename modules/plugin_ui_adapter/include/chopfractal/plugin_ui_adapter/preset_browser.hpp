#pragma once
// View model of the preset browser: filtering, search, keyboard stepping and name rules. Pure functions over plain
// items, so the browser's behaviour is unit tested without a window.
#include <string>
#include <vector>

namespace chopfractal::ui {

struct PresetItem {
  std::string key;          // "factory:<name>" or "user:<file stem>"
  std::string name;
  std::string category;     // the break style, or "User"
  std::string description;
  bool factory = true;
  bool favorite = false;
  bool loadable = true;     // false: made by a newer version
};

enum class PresetScope { All, Factory, User, Favorites };

// Indices into `items`, in item order. The query is split at spaces; every word must appear (case-insensitively) in the
// name, style, description or the tags "factory" / "user" / "favorite" of the item. An empty query keeps everything in scope.
std::vector<int> filterPresets(const std::vector<PresetItem>& items, PresetScope scope, const std::string& query);

// Up / Down in the list: the row after moving `delta` rows from `current` (-1 = nothing selected), clamped to the list.
// An empty list gives -1.
int stepRow(int rowCount, int current, int delta);

// Empty string when the name is acceptable, else the message to show: 1 to 48 characters, none of \ / : * ? " < > |.
std::string validatePresetName(const std::string& name);

// `wanted`, or `wanted 2`, `wanted 3` ... when a user preset of that name already exists (case-insensitive, at most 48 bytes).
std::string uniquePresetName(const std::vector<PresetItem>& items, const std::string& wanted);

// "Boom Bap Basics" -> "BOOM BAP BASICS" is a style question; here only the one-line summary under the name:
// "<style>, factory" or "<style>, yours".
std::string presetSubtitle(const PresetItem& item);

}  // namespace chopfractal::ui
