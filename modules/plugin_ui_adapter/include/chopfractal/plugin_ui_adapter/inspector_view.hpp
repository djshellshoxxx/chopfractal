#pragma once
// View model of the per-hit inspector (G2): the values of the selected hit or hits as the inspector shows them, with mixed
// values marked, plus the unit conversions of its sliders. The inspector panel reads InspectorView and writes through the
// editor commands (setHitsTransform, setHitsProbability, setHitsLocked, setHitsLane, setHitEnabled, setRoleOfSelected).
// UI thread only, no JUCE.
#include <chopfractal/chop_contracts/chop.hpp>
#include <chopfractal/pattern_engine/pattern.hpp>
#include <chopfractal/plugin_ui_adapter/views.hpp>

#include <string>
#include <vector>

namespace chopfractal::ui {

// Level is stored as a linear gain (0..4) and shown in dB; -48 dB and below is silence.
constexpr double kInspectorMinDb = -48.0, kInspectorMaxDb = 12.0;
double levelToDb(float linear);   // 0 gives kInspectorMinDb
float dbToLevel(double db);       // <= kInspectorMinDb gives 0, clamped to the engine's 4.0
std::string formatLevelDb(float linear);   // "-2.0 dB", "-inf dB"
std::string formatPan(float pan);          // "C", "L40", "R25"
std::string formatPitch(float semitones);  // "+3 st", "0 st", "-12 st"
std::string formatPercent(double fraction); // "80 %"

struct InspectorValue {
  double value = 0.0;
  bool mixed = false;
  std::string text;
};
struct InspectorView {
  int hitCount = 0;
  bool editable = false;       // at least one selected hit is not locked
  std::string title;           // "Hit 2.3", "4 hits", "No hit selected"
  std::string subtitle;        // "chop 2, Snare" or "mixed chops"
  std::string refusal;         // why nothing can be edited
  InspectorValue levelDb, pan, pitch, probability /* 0..1 */, retrigger;
  bool reverse = false, reverseMixed = false;
  bool locked = false, lockedMixed = false;
  bool muted = false, mutedMixed = false;
  int chopIndex = -1;          // lane of the hits, -1 when they differ
  bool chopMixed = false;
  std::string role;            // role of the hits' chop, empty when none or mixed
  bool roleMixed = false;
  std::string accessibleText;
};
InspectorView buildInspectorView(const pattern::Pattern&, const std::vector<EventId>& ids, const ChopSnapshot& chops, const RoleLookup& roleOf = {});

}  // namespace chopfractal::ui
