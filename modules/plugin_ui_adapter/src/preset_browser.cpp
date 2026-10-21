#include <chopfractal/plugin_ui_adapter/preset_browser.hpp>

#include <algorithm>
#include <cctype>

namespace chopfractal::ui {
namespace {
std::string lower(const std::string& s) {
  std::string o = s;
  for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return o;
}
std::vector<std::string> words(const std::string& q) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : lower(q)) {
    if (c == ' ' || c == '\t') {
      if (!cur.empty()) out.push_back(cur), cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}
}  // namespace

std::vector<int> filterPresets(const std::vector<PresetItem>& items, PresetScope scope, const std::string& query) {
  const auto terms = words(query);
  std::vector<int> rows;
  for (std::size_t i = 0; i < items.size(); ++i) {
    const PresetItem& it = items[i];
    if (scope == PresetScope::Factory && !it.factory) continue;
    if (scope == PresetScope::User && it.factory) continue;
    if (scope == PresetScope::Favorites && !it.favorite) continue;
    const std::string hay = lower(it.name + " " + it.category + " " + it.description + (it.factory ? " factory" : " user") + (it.favorite ? " favorite" : ""));
    bool all = true;
    for (const auto& t : terms) all = all && hay.find(t) != std::string::npos;
    if (all) rows.push_back(static_cast<int>(i));
  }
  return rows;
}

int stepRow(int rowCount, int current, int delta) {
  if (rowCount <= 0) return -1;
  if (current < 0) return delta >= 0 ? 0 : rowCount - 1;
  return std::max(0, std::min(rowCount - 1, current + delta));
}

std::string validatePresetName(const std::string& name) {
  const std::string msg = "Preset names are 1 to 48 characters and cannot contain \\ / : * ? \" < > |";
  if (name.empty() || name.size() > 48) return msg;
  for (unsigned char c : name)
    if (c < 0x20 || c == 0x7F || std::string("\\/:*?\"<>|").find(static_cast<char>(c)) != std::string::npos) return msg;
  const auto b = name.find_first_not_of(' ');
  if (b == std::string::npos) return msg;
  return {};
}

std::string uniquePresetName(const std::vector<PresetItem>& items, const std::string& wanted) {
  auto taken = [&items](const std::string& n) {
    for (const PresetItem& it : items)
      if (lower(it.name) == lower(n)) return true;
    return false;
  };
  std::string base = wanted.substr(0, 44);
  if (!taken(wanted.substr(0, 48))) return wanted.substr(0, 48);
  for (int n = 2; n < 1000; ++n) {
    const std::string cand = base + " " + std::to_string(n);
    if (!taken(cand)) return cand;
  }
  return base;
}

std::string presetSubtitle(const PresetItem& item) { return item.category + (item.factory ? ", factory" : ", yours"); }

}  // namespace chopfractal::ui
