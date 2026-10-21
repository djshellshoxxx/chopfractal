#pragma once
// View model of the per-hit effects rack (G2): the seven effects a hit can carry, their controls, and the pure functions that
// read a control from and write it into a hit's effect state. The rack panel draws FxRackView and calls fxSetControl /
// fxSetModuleOn on a copy of each selected hit's HitFx; the editor commands then store the result through
// pattern::setEventFx and pattern::setEventGestureFx. UI thread only, no JUCE.
//
// Every module has the same first control, "intensity" (0..100 %), which is the strength of the effect:
//   Filter    how far the cutoff is from open (low-pass: 1 - cutoff, high-pass: cutoff)
//   Glide     the size of the pitch ramp, 0..24 semitones
//   Crunch    the bit and sample-rate reduction
//   Stutter   how fine the repeats are (the note value the repeats start at)
//   Roll      how fine the roll ends up (the note value it speeds up to)
//   Echo      the number of repeats, 1 to 5 (20 % each)
//   Trem-Pan  the depth of the modulation
// The remaining controls are per effect. A hit's own probability (chance it plays in a loop) sits in the rack footer.
#include <chopfractal/chop_contracts/event.hpp>
#include <chopfractal/pattern_engine/pattern.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::ui {

enum class FxModule : std::uint8_t { Filter = 0, Glide, Crunch, Stutter, Roll, Echo, TremPan };
constexpr int kFxModuleCount = 7;
const char* fxModuleName(FxModule);
const char* fxModuleTooltip(FxModule);

enum class FxControlKind : std::uint8_t { Slider, Choice, Toggle };
struct FxControlSpec {
  std::string id;       // stable key: "intensity", "type", "resonance", ...
  std::string label;
  std::string tooltip;  // one clause, no final period
  FxControlKind kind = FxControlKind::Slider;
  double min = 0.0, max = 1.0, step = 1.0, def = 0.0;
  std::string unit;                  // "%", "st", "dB" or empty; shown muted
  std::vector<std::string> choices;  // Choice: the value is the index
};
// Control 0 is always "intensity".
const std::vector<FxControlSpec>& fxControlSpecs(FxModule);
const FxControlSpec* fxControlSpec(FxModule, const std::string& id);

struct HitFx {
  EventFx fx;
  pattern::GestureFx gesture;
  friend bool operator==(const HitFx& a, const HitFx& b) { return a.fx == b.fx && a.gesture == b.gesture; }
};
HitFx hitFxOf(const Event&);

bool fxModuleOn(const HitFx&, FxModule);
// The control's current value; the spec default while the module is off.
double fxControlValue(const HitFx&, FxModule, const std::string& id);
// Off clears the module (Echo and Trem-Pan write the "forced off" block so the effect pass leaves the hit alone); on writes the
// module's defaults. `restore` (optional) is the module's last state, remembered by the panel for one toggle back.
void fxSetModuleOn(HitFx&, FxModule, bool on, const HitFx* restore = nullptr);
// Writes one control; the module is switched on first (with its defaults) when it was off. Values are clamped to the spec.
void fxSetControl(HitFx&, FxModule, const std::string& id, double value);
// Display text of a value: "50 %", "-3.5 dB", "1/16" (a note value for the Stutter and Roll intensity), "3 repeats", "Both".
std::string fxControlText(FxModule, const FxControlSpec&, double value);

struct FxControlView {
  FxControlSpec spec;
  bool mixed = false;   // the selected hits disagree (only counts hits that have the module on)
  double value = 0.0;
  std::string text;     // "mixed" when mixed
};
struct FxModuleView {
  FxModule module = FxModule::Filter;
  std::string name, tooltip;
  int onCount = 0, total = 0;
  bool on = false;      // every selected hit has it on
  bool mixed = false;   // some do, some do not
  std::vector<FxControlView> controls;
};
struct FxRackView {
  int hitCount = 0;
  bool editable = false;
  std::string title;    // "Hit 3.2" / "3 hits" / "No hit selected"
  std::string refusal;  // why nothing can be edited (empty when editable)
  std::vector<FxModuleView> modules;  // always kFxModuleCount entries, in FxModule order
  double probability = 1.0;           // 0..1
  bool probabilityMixed = false;
};
FxRackView buildFxRackView(const pattern::Pattern&, const std::vector<EventId>& ids);

}  // namespace chopfractal::ui
