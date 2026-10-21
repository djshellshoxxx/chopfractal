#include <chopfractal/chop_contracts/limits.hpp>
#include <chopfractal/plugin_ui_adapter/inspector_view.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chopfractal::ui {
namespace {
std::string num(double v, int decimals) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
  std::string s = buf;
  if (s == "-0.0" || s == "-0") s = s.substr(1);
  return s;
}
}  // namespace

double levelToDb(float linear) {
  if (!(linear > 0.f)) return kInspectorMinDb;
  return std::max(kInspectorMinDb, 20.0 * std::log10(static_cast<double>(linear)));
}

float dbToLevel(double db) {
  if (!std::isfinite(db) || db <= kInspectorMinDb) return 0.f;
  return std::min(limits::kMaxLevel, static_cast<float>(std::pow(10.0, db / 20.0)));
}

std::string formatLevelDb(float linear) {
  const double db = levelToDb(linear);
  if (db <= kInspectorMinDb) return "-inf dB";
  return num(db, 1) + " dB";
}

std::string formatPan(float pan) {
  const int pct = static_cast<int>(std::lround(std::fabs(static_cast<double>(pan)) * 100.0));
  if (pct == 0) return "C";
  return std::string(pan < 0.f ? "L" : "R") + std::to_string(pct);
}

std::string formatPitch(float semitones) {
  const double st = std::round(static_cast<double>(semitones) * 10.0) / 10.0;
  const std::string n = num(std::fabs(st), std::fabs(st - std::round(st)) < 1e-9 ? 0 : 1);
  if (st > 0.0) return "+" + n + " st";
  if (st < 0.0) return "-" + n + " st";
  return "0 st";
}

std::string formatPercent(double fraction) { return std::to_string(static_cast<int>(std::lround(fraction * 100.0))) + " %"; }

InspectorView buildInspectorView(const pattern::Pattern& p, const std::vector<EventId>& ids, const ChopSnapshot& chops, const RoleLookup& roleOf) {
  InspectorView v;
  std::vector<const Event*> hits;
  for (const auto id : ids)
    if (const Event* e = pattern::findEvent(p, id)) hits.push_back(e);
  v.hitCount = static_cast<int>(hits.size());
  if (hits.empty()) {
    v.title = "No hit selected";
    v.refusal = "Select a hit to edit it.";
    v.levelDb = {0.0, false, "0.0 dB"};
    v.pan = {0.0, false, "C"};
    v.pitch = {0.0, false, "0 st"};
    v.probability = {1.0, false, "100 %"};
    v.retrigger = {1.0, false, "1"};
    return v;
  }
  auto laneOf = [&](const Event* e) {
    for (std::size_t i = 0; i < chops.chops.size(); ++i)
      if (chops.chops[i].id == e->chop) return static_cast<int>(i);
    return -1;
  };
  std::size_t editable = 0;
  for (const Event* e : hits)
    if (!pattern::isLocked(p, e->id)) ++editable;
  v.editable = editable > 0;
  if (!v.editable) v.refusal = "Those hits are locked. Unlock them first.";

  const Event* first = hits.front();
  auto mixedOf = [&](auto get) {
    for (const Event* e : hits)
      if (get(e) != get(first)) return true;
    return false;
  };
  v.levelDb.value = levelToDb(first->tx.level);
  v.levelDb.mixed = mixedOf([](const Event* e) { return e->tx.level; });
  v.levelDb.text = v.levelDb.mixed ? "mixed" : formatLevelDb(first->tx.level);
  v.pan = {first->tx.pan, mixedOf([](const Event* e) { return e->tx.pan; }), {}};
  v.pan.text = v.pan.mixed ? "mixed" : formatPan(first->tx.pan);
  v.pitch = {first->tx.pitchSemitones, mixedOf([](const Event* e) { return e->tx.pitchSemitones; }), {}};
  v.pitch.text = v.pitch.mixed ? "mixed" : formatPitch(first->tx.pitchSemitones);
  v.probability = {first->probability, mixedOf([](const Event* e) { return e->probability; }), {}};
  v.probability.text = v.probability.mixed ? "mixed" : formatPercent(first->probability);
  v.retrigger = {static_cast<double>(first->tx.retrigger), mixedOf([](const Event* e) { return e->tx.retrigger; }), {}};
  v.retrigger.text = v.retrigger.mixed ? "mixed" : std::to_string(first->tx.retrigger);
  v.reverse = first->tx.reverse;
  v.reverseMixed = mixedOf([](const Event* e) { return e->tx.reverse; });
  v.locked = first->locked;
  v.lockedMixed = mixedOf([](const Event* e) { return e->locked; });
  v.muted = !first->enabled;
  v.mutedMixed = mixedOf([](const Event* e) { return e->enabled; });
  v.chopIndex = laneOf(first);
  v.chopMixed = mixedOf([](const Event* e) { return e->chop.value; });
  if (v.chopMixed) v.chopIndex = -1;
  if (!v.chopMixed && roleOf) v.role = roleOf(first->chop);
  else if (v.chopMixed && roleOf) {
    const std::string r0 = roleOf(first->chop);
    for (const Event* e : hits)
      if (roleOf(e->chop) != r0) v.roleMixed = true;
    if (!v.roleMixed) v.role = r0;
  }

  if (hits.size() == 1) {
    const auto loc = pattern::locate(p, first->id);
    v.title = loc ? "Hit " + std::to_string(loc->bar + 1) + "." + std::to_string(loc->beat + 1) : "Hit";
    v.subtitle = v.chopIndex >= 0 ? "chop " + std::to_string(v.chopIndex + 1) + (v.role.empty() ? "" : ", " + v.role) : "no chop";
  } else {
    v.title = std::to_string(hits.size()) + " hits";
    v.subtitle = v.chopMixed ? "mixed chops" : "chop " + std::to_string(v.chopIndex + 1) + (v.role.empty() ? "" : ", " + v.role);
  }
  v.accessibleText = v.title + ", " + v.subtitle + ", level " + v.levelDb.text + ", pan " + v.pan.text + ", pitch " + v.pitch.text;
  if (v.locked) v.accessibleText += ", locked";
  if (v.muted) v.accessibleText += ", muted";
  return v;
}

}  // namespace chopfractal::ui
