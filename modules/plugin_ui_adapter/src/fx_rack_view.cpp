#include <chopfractal/chop_contracts/limits.hpp>
#include <chopfractal/plugin_ui_adapter/fx_rack_view.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chopfractal::ui {
namespace {

using Kind = FxControlKind;

FxControlSpec slider(const char* id, const char* label, const char* tip, double lo, double hi, double step, double def, const char* unit) {
  FxControlSpec s;
  s.id = id;
  s.label = label;
  s.tooltip = tip;
  s.kind = Kind::Slider;
  s.min = lo;
  s.max = hi;
  s.step = step;
  s.def = def;
  s.unit = unit;
  return s;
}
FxControlSpec choice(const char* id, const char* label, const char* tip, std::vector<std::string> items, double def) {
  FxControlSpec s;
  s.id = id;
  s.label = label;
  s.tooltip = tip;
  s.kind = Kind::Choice;
  s.min = 0.0;
  s.max = static_cast<double>(items.size()) - 1.0;
  s.step = 1.0;
  s.def = def;
  s.choices = std::move(items);
  return s;
}
FxControlSpec toggle(const char* id, const char* label, const char* tip, bool def) {
  FxControlSpec s;
  s.id = id;
  s.label = label;
  s.tooltip = tip;
  s.kind = Kind::Toggle;
  s.max = 1.0;
  s.def = def ? 1.0 : 0.0;
  return s;
}

const std::vector<std::string>& divisions() {
  static const std::vector<std::string> d = {"1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64", "1/64T"};
  return d;
}

const std::vector<FxControlSpec>& specsFor(FxModule m) {
  static const std::vector<FxControlSpec> filter = {
      slider("intensity", "Intensity", "How far the cutoff is from open", 0, 100, 1, 50, "%"),
      choice("type", "Type", "Low-pass removes highs, high-pass removes lows", {"Low-pass", "High-pass"}, 0),
      slider("resonance", "Resonance", "Emphasis at the cutoff", 0, 100, 1, 20, "%"),
  };
  static const std::vector<FxControlSpec> glide = {
      slider("intensity", "Intensity", "Size of the pitch ramp across the hit, up to 24 semitones", 1, 100, 1, 50, "%"),
      choice("direction", "Direction", "Whether the pitch falls or rises", {"Down", "Up"}, 0),
  };
  static const std::vector<FxControlSpec> crunch = {
      slider("intensity", "Intensity", "Bit and sample-rate reduction", 1, 100, 1, 50, "%"),
  };
  static const std::vector<FxControlSpec> stutter = {
      slider("intensity", "Intensity", "How fine the repeats are", 0, 100, 1, 44, "%"),
      slider("ramp", "Ramp", "How many note values the repeats speed up by across the hit", 0, 9, 1, 0, ""),
      slider("gate", "Gate", "How much of each repeat is audible", 10, 100, 1, 80, "%"),
      slider("tail", "Tail", "How much of the audible part is release", 0, 100, 1, 30, "%"),
      slider("decay", "Decay", "Level change per repeat, negative fades out", -12, 6, 0.5, -1.5, "dB"),
      slider("pitch", "Pitch", "Pitch ramp over the whole hit", -12, 12, 0.5, 0, "st"),
      slider("chance", "Repeat chance", "Chance that each repeat after the first plays", 0, 100, 1, 100, "%"),
      slider("pan", "Pan width", "Width of the left and right alternation", 0, 100, 1, 0, "%"),
  };
  static const std::vector<FxControlSpec> roll = {
      slider("intensity", "Intensity", "How fine the roll ends up", 0, 100, 1, 89, "%"),
      choice("start", "Start rate", "Note value the roll starts at", divisions(), 4),
      slider("freeze", "Freeze", "Where in the chop the repeated grab is taken", 0, 100, 1, 0, "%"),
      slider("velStart", "Start level", "Level of the first repeat", 0, 100, 1, 50, "%"),
      slider("velEnd", "End level", "Level of the last repeat", 0, 100, 1, 100, "%"),
      toggle("smooth", "Smooth", "Speed up smoothly instead of in steps", false),
      toggle("palindrome", "Palindrome", "Play every other repeat backwards", false),
      slider("pitch", "Pitch", "Pitch ramp over the roll", -12, 12, 0.5, 0, "st"),
  };
  static const std::vector<FxControlSpec> echo = {
      slider("intensity", "Intensity", "Number of repeats, 1 to 5", 20, 100, 20, 60, "%"),
      choice("rate", "Rate", "Spacing of the repeats", {"Global", "1/16", "1/8", "Dotted 1/8", "1/4"}, 0),
      slider("decay", "Decay", "Level of each repeat relative to the one before", 10, 90, 1, 55, "%"),
  };
  static const std::vector<FxControlSpec> trem = {
      slider("intensity", "Intensity", "Depth of the modulation", 0, 100, 1, 60, "%"),
      choice("mode", "Mode", "Tremolo changes level, pan swings left and right, both does both", {"Tremolo", "Pan", "Both"}, 0),
      choice("rate", "Rate", "Length of one modulation cycle", {"1/16", "1/8", "1/4", "1/2"}, 1),
  };
  switch (m) {
    case FxModule::Filter: return filter;
    case FxModule::Glide: return glide;
    case FxModule::Crunch: return crunch;
    case FxModule::Stutter: return stutter;
    case FxModule::Roll: return roll;
    case FxModule::Echo: return echo;
    case FxModule::TremPan: return trem;
  }
  return filter;
}

double clampTo(const FxControlSpec& s, double v) {
  if (!std::isfinite(v)) return s.def;
  v = std::clamp(v, s.min, s.max);
  if (s.kind == Kind::Slider && s.step > 0.0) v = s.min + std::round((v - s.min) / s.step) * s.step;
  return std::clamp(v, s.min, s.max);
}

int idx(double v, int lo, int hi) { return std::clamp(static_cast<int>(std::lround(v)), lo, hi); }

std::string fmtNumber(double v, double step) {
  char buf[48];
  if (step >= 1.0 || std::fabs(v - std::round(v)) < 1e-9) std::snprintf(buf, sizeof buf, "%d", static_cast<int>(std::lround(v)));
  else std::snprintf(buf, sizeof buf, "%.1f", v);
  return buf;
}

}  // namespace

const char* fxModuleName(FxModule m) {
  switch (m) {
    case FxModule::Filter: return "Filter";
    case FxModule::Glide: return "Tape glide";
    case FxModule::Crunch: return "Crunch";
    case FxModule::Stutter: return "Stutter";
    case FxModule::Roll: return "Roll";
    case FxModule::Echo: return "Echo";
    case FxModule::TremPan: return "Tremolo-Pan";
  }
  return "";
}

const char* fxModuleTooltip(FxModule m) {
  switch (m) {
    case FxModule::Filter: return "Low-pass or high-pass filter on this hit";
    case FxModule::Glide: return "Pitch ramp across this hit, like a tape slowing down or speeding up";
    case FxModule::Crunch: return "Bit and sample-rate reduction on this hit";
    case FxModule::Stutter: return "Repeats the start of this hit on the grid";
    case FxModule::Roll: return "Freezes a grab of this hit and rolls it faster";
    case FxModule::Echo: return "Tempo-synced repeats after this hit";
    case FxModule::TremPan: return "Tremolo and auto-pan on this hit";
  }
  return "";
}

const std::vector<FxControlSpec>& fxControlSpecs(FxModule m) { return specsFor(m); }

const FxControlSpec* fxControlSpec(FxModule m, const std::string& id) {
  for (const auto& s : specsFor(m))
    if (s.id == id) return &s;
  return nullptr;
}

HitFx hitFxOf(const Event& e) {
  HitFx h;
  h.fx = e.fx;
  h.gesture = {e.echo, e.tremPan, e.stutter, e.roll};
  return h;
}

bool fxModuleOn(const HitFx& h, FxModule m) {
  switch (m) {
    case FxModule::Filter: return h.fx.filter != FilterType::Off;
    case FxModule::Glide: return h.fx.glideSemitones != 0.f;
    case FxModule::Crunch: return h.fx.crush != 0.f;
    case FxModule::Stutter: return h.gesture.stutter.has_value();
    case FxModule::Roll: return h.gesture.roll.has_value();
    case FxModule::Echo: return h.gesture.echo && h.gesture.echo->repeats > 0;
    case FxModule::TremPan: return h.gesture.tremPan && h.gesture.tremPan->mode != 254;
  }
  return false;
}

double fxControlValue(const HitFx& h, FxModule m, const std::string& id) {
  const FxControlSpec* spec = fxControlSpec(m, id);
  if (!spec) return 0.0;
  if (!fxModuleOn(h, m)) return spec->def;
  switch (m) {
    case FxModule::Filter:
      if (id == "intensity") return 100.0 * (h.fx.filter == FilterType::LowPass ? 1.0 - h.fx.cutoff : h.fx.cutoff);
      if (id == "type") return h.fx.filter == FilterType::HighPass ? 1.0 : 0.0;
      if (id == "resonance") return 100.0 * h.fx.resonance;
      break;
    case FxModule::Glide:
      if (id == "intensity") return 100.0 * std::fabs(h.fx.glideSemitones) / limits::kMaxGlideSemitones;
      if (id == "direction") return h.fx.glideSemitones > 0.f ? 1.0 : 0.0;
      break;
    case FxModule::Crunch:
      return 100.0 * h.fx.crush;
    case FxModule::Stutter: {
      const StutterFx& s = *h.gesture.stutter;
      if (id == "intensity") return 100.0 * s.rate / (kStutterDivisions - 1);
      if (id == "ramp") return std::max(0, static_cast<int>(s.rateEnd) - static_cast<int>(s.rate));
      if (id == "gate") return s.gate;
      if (id == "tail") return s.tail;
      if (id == "decay") return s.decay / 10.0;
      if (id == "pitch") return s.pitch / 10.0;
      if (id == "chance") return s.probability;
      if (id == "pan") return s.pan;
      break;
    }
    case FxModule::Roll: {
      const RollFx& r = *h.gesture.roll;
      if (id == "intensity") return 100.0 * r.rateEnd / (kStutterDivisions - 1);
      if (id == "start") return r.rate;
      if (id == "freeze") return r.freeze;
      if (id == "velStart") return r.velStart;
      if (id == "velEnd") return r.velEnd;
      if (id == "smooth") return (r.flags & kRollSmooth) ? 1.0 : 0.0;
      if (id == "palindrome") return (r.flags & kRollPalindrome) ? 1.0 : 0.0;
      if (id == "pitch") return r.pitch / 10.0;
      break;
    }
    case FxModule::Echo: {
      const EchoFx& e = *h.gesture.echo;
      if (id == "intensity") return 20.0 * e.repeats;
      if (id == "rate") return e.rateIndex == 255 ? 0.0 : 1.0 + std::min<int>(e.rateIndex, 3);
      if (id == "decay") return e.level == 255 ? 55.0 : (e.level >= 128 ? e.level - 128 : e.level);
      break;
    }
    case FxModule::TremPan: {
      const TremPanFx& t = *h.gesture.tremPan;
      if (id == "intensity") return t.depth == 255 ? 60.0 : (t.depth >= 128 ? t.depth - 128 : t.depth);
      if (id == "mode") return t.mode > 2 ? 0.0 : t.mode;
      if (id == "rate") return t.rateIndex == 255 ? 1.0 : std::min<int>(t.rateIndex, 3);
      break;
    }
  }
  return spec->def;
}

namespace {
void writeDefaults(HitFx& h, FxModule m) {
  switch (m) {
    case FxModule::Filter:
      h.fx.filter = FilterType::LowPass;
      h.fx.cutoff = 0.5f;
      h.fx.resonance = 0.2f;
      break;
    case FxModule::Glide:
      h.fx.glideSemitones = -0.5f * limits::kMaxGlideSemitones;
      break;
    case FxModule::Crunch:
      h.fx.crush = 0.5f;
      break;
    case FxModule::Stutter:
      h.gesture.stutter = StutterFx{};
      break;
    case FxModule::Roll:
      h.gesture.roll = RollFx{};
      break;
    case FxModule::Echo: {
      EchoFx e;
      e.repeats = 3;
      e.rateIndex = 255;
      e.level = 128 + 55;
      h.gesture.echo = e;
      break;
    }
    case FxModule::TremPan: {
      TremPanFx t;
      t.mode = 0;
      t.rateIndex = 1;
      t.depth = 128 + 60;
      h.gesture.tremPan = t;
      break;
    }
  }
}
}  // namespace

void fxSetModuleOn(HitFx& h, FxModule m, bool on, const HitFx* restore) {
  if (fxModuleOn(h, m) == on) return;
  if (on) {
    if (restore && fxModuleOn(*restore, m)) {  // toggle back to what it was
      switch (m) {
        case FxModule::Filter: h.fx.filter = restore->fx.filter; h.fx.cutoff = restore->fx.cutoff; h.fx.resonance = restore->fx.resonance; break;
        case FxModule::Glide: h.fx.glideSemitones = restore->fx.glideSemitones; break;
        case FxModule::Crunch: h.fx.crush = restore->fx.crush; break;
        case FxModule::Stutter: h.gesture.stutter = restore->gesture.stutter; break;
        case FxModule::Roll: h.gesture.roll = restore->gesture.roll; break;
        case FxModule::Echo: h.gesture.echo = restore->gesture.echo; break;
        case FxModule::TremPan: h.gesture.tremPan = restore->gesture.tremPan; break;
      }
    } else {
      writeDefaults(h, m);
    }
    return;
  }
  switch (m) {
    case FxModule::Filter: h.fx.filter = FilterType::Off; h.fx.cutoff = 1.f; h.fx.resonance = 0.f; break;
    case FxModule::Glide: h.fx.glideSemitones = 0.f; break;
    case FxModule::Crunch: h.fx.crush = 0.f; break;
    case FxModule::Stutter: h.gesture.stutter.reset(); break;
    case FxModule::Roll: h.gesture.roll.reset(); break;
    case FxModule::Echo: {
      EchoFx e;
      e.repeats = 0;
      e.rateIndex = 255;
      e.level = 128 + 55;  // forced off: the effect pass leaves this hit alone
      h.gesture.echo = e;
      break;
    }
    case FxModule::TremPan: {
      TremPanFx t;
      t.mode = 254;  // forced off
      h.gesture.tremPan = t;
      break;
    }
  }
}

void fxSetControl(HitFx& h, FxModule m, const std::string& id, double value) {
  const FxControlSpec* spec = fxControlSpec(m, id);
  if (!spec) return;
  if (!fxModuleOn(h, m)) fxSetModuleOn(h, m, true);
  const double v = clampTo(*spec, value);
  switch (m) {
    case FxModule::Filter:
      if (id == "intensity") h.fx.cutoff = static_cast<float>(h.fx.filter == FilterType::LowPass ? 1.0 - v / 100.0 : v / 100.0);
      else if (id == "type") {
        const double i = fxControlValue(h, m, "intensity");  // keep the strength when the type flips
        h.fx.filter = v >= 0.5 ? FilterType::HighPass : FilterType::LowPass;
        h.fx.cutoff = static_cast<float>(h.fx.filter == FilterType::LowPass ? 1.0 - i / 100.0 : i / 100.0);
      } else if (id == "resonance") h.fx.resonance = static_cast<float>(v / 100.0);
      break;
    case FxModule::Glide:
      if (id == "intensity") h.fx.glideSemitones = static_cast<float>((h.fx.glideSemitones > 0.f ? 1.0 : -1.0) * v / 100.0 * limits::kMaxGlideSemitones);
      else if (id == "direction") h.fx.glideSemitones = static_cast<float>((v >= 0.5 ? 1.0 : -1.0) * std::fabs(h.fx.glideSemitones));
      break;
    case FxModule::Crunch:
      h.fx.crush = static_cast<float>(v / 100.0);
      break;
    case FxModule::Stutter: {
      StutterFx& s = *h.gesture.stutter;
      const int ramp = std::max(0, static_cast<int>(s.rateEnd) - static_cast<int>(s.rate));
      if (id == "intensity") {
        s.rate = static_cast<std::uint8_t>(idx(v / 100.0 * (kStutterDivisions - 1), 0, kStutterDivisions - 1));
        s.rateEnd = static_cast<std::uint8_t>(std::min<int>(kStutterDivisions - 1, s.rate + ramp));
      } else if (id == "ramp") s.rateEnd = static_cast<std::uint8_t>(std::min<int>(kStutterDivisions - 1, s.rate + idx(v, 0, 9)));
      else if (id == "gate") s.gate = static_cast<std::uint8_t>(idx(v, 10, 100));
      else if (id == "tail") s.tail = static_cast<std::uint8_t>(idx(v, 0, 100));
      else if (id == "decay") s.decay = static_cast<std::int8_t>(idx(v * 10.0, -120, 60));
      else if (id == "pitch") s.pitch = static_cast<std::int8_t>(idx(v * 10.0, -120, 120));
      else if (id == "chance") s.probability = static_cast<std::uint8_t>(idx(v, 0, 100));
      else if (id == "pan") s.pan = static_cast<std::uint8_t>(idx(v, 0, 100));
      s = sanitize(s);
      break;
    }
    case FxModule::Roll: {
      RollFx& r = *h.gesture.roll;
      if (id == "intensity") r.rateEnd = static_cast<std::uint8_t>(idx(v / 100.0 * (kStutterDivisions - 1), 0, kStutterDivisions - 1));
      else if (id == "start") r.rate = static_cast<std::uint8_t>(idx(v, 0, kStutterDivisions - 1));
      else if (id == "freeze") r.freeze = static_cast<std::uint8_t>(idx(v, 0, 100));
      else if (id == "velStart") r.velStart = static_cast<std::uint8_t>(idx(v, 0, 100));
      else if (id == "velEnd") r.velEnd = static_cast<std::uint8_t>(idx(v, 0, 100));
      else if (id == "smooth") r.flags = static_cast<std::uint8_t>(v >= 0.5 ? (r.flags | kRollSmooth) : (r.flags & ~kRollSmooth));
      else if (id == "palindrome") r.flags = static_cast<std::uint8_t>(v >= 0.5 ? (r.flags | kRollPalindrome) : (r.flags & ~kRollPalindrome));
      else if (id == "pitch") r.pitch = static_cast<std::int8_t>(idx(v * 10.0, -120, 120));
      if (r.rateEnd < r.rate) r.rateEnd = r.rate;  // a roll never slows down
      r = sanitize(r);
      break;
    }
    case FxModule::Echo: {
      EchoFx& e = *h.gesture.echo;
      if (id == "intensity") e.repeats = static_cast<std::uint8_t>(idx(v / 20.0, 1, kEchoMaxRepeats));
      else if (id == "rate") e.rateIndex = v < 0.5 ? 255 : static_cast<std::uint8_t>(idx(v - 1.0, 0, 3));
      else if (id == "decay") e.level = static_cast<std::uint8_t>(128 + idx(v, 10, 90));
      e = sanitize(e);
      break;
    }
    case FxModule::TremPan: {
      TremPanFx& t = *h.gesture.tremPan;
      if (id == "intensity") t.depth = static_cast<std::uint8_t>(128 + idx(v, 0, 100));
      else if (id == "mode") t.mode = static_cast<std::uint8_t>(idx(v, 0, 2));
      else if (id == "rate") t.rateIndex = static_cast<std::uint8_t>(idx(v, 0, 3));
      t = sanitize(t);
      break;
    }
  }
}

std::string fxControlText(FxModule m, const FxControlSpec& s, double v) {
  if (s.kind == Kind::Choice) {
    const int i = idx(v, 0, std::max(0, static_cast<int>(s.choices.size()) - 1));
    return s.choices.empty() ? std::string() : s.choices[static_cast<std::size_t>(i)];
  }
  if (s.kind == Kind::Toggle) return v >= 0.5 ? "On" : "Off";
  if (s.id == "intensity") {  // the gesture effects say what the strength means
    if (m == FxModule::Stutter || m == FxModule::Roll) return divisions()[static_cast<std::size_t>(idx(v / 100.0 * (kStutterDivisions - 1), 0, kStutterDivisions - 1))];
    if (m == FxModule::Echo) {
      const int n = idx(v / 20.0, 1, kEchoMaxRepeats);
      return std::to_string(n) + (n == 1 ? " repeat" : " repeats");
    }
    if (m == FxModule::Glide) return fmtNumber(v / 100.0 * limits::kMaxGlideSemitones, 0.1) + " st";
  }
  std::string text = fmtNumber(v, s.step);
  if (!s.unit.empty()) text += " " + s.unit;
  return text;
}

FxRackView buildFxRackView(const pattern::Pattern& p, const std::vector<EventId>& ids) {
  FxRackView v;
  std::vector<const Event*> hits;
  for (const auto id : ids)
    if (const Event* e = pattern::findEvent(p, id)) hits.push_back(e);
  v.hitCount = static_cast<int>(hits.size());
  if (hits.empty()) {
    v.title = "No hit selected";
    v.refusal = "Select a hit to edit its effects.";
  } else if (hits.size() == 1) {
    const auto loc = pattern::locate(p, hits[0]->id);
    v.title = loc ? "Hit " + std::to_string(loc->bar + 1) + "." + std::to_string(loc->beat + 1) : "Hit";
  } else {
    v.title = std::to_string(hits.size()) + " hits";
  }
  std::size_t editable = 0;
  for (const auto id : ids)
    if (pattern::findEvent(p, id) && !pattern::isLocked(p, id)) ++editable;
  v.editable = editable > 0;
  if (!hits.empty() && !v.editable) v.refusal = "Those hits are locked. Unlock them first.";
  std::vector<HitFx> states;
  for (const Event* e : hits) states.push_back(hitFxOf(*e));
  for (int mi = 0; mi < kFxModuleCount; ++mi) {
    const FxModule m = static_cast<FxModule>(mi);
    FxModuleView mv;
    mv.module = m;
    mv.name = fxModuleName(m);
    mv.tooltip = fxModuleTooltip(m);
    mv.total = static_cast<int>(states.size());
    for (const auto& s : states) mv.onCount += fxModuleOn(s, m) ? 1 : 0;
    mv.on = mv.total > 0 && mv.onCount == mv.total;
    mv.mixed = mv.onCount > 0 && mv.onCount < mv.total;
    for (const auto& spec : fxControlSpecs(m)) {
      FxControlView cv;
      cv.spec = spec;
      cv.value = spec.def;
      bool first = true;
      for (const auto& s : states) {
        if (!fxModuleOn(s, m)) continue;
        const double val = fxControlValue(s, m, spec.id);
        if (first) {
          cv.value = val;
          first = false;
        } else if (std::fabs(val - cv.value) > 1e-6) {
          cv.mixed = true;
        }
      }
      cv.text = cv.mixed ? "mixed" : fxControlText(m, spec, cv.value);
      mv.controls.push_back(std::move(cv));
    }
    v.modules.push_back(std::move(mv));
  }
  bool first = true;
  for (const Event* e : hits) {
    if (first) {
      v.probability = e->probability;
      first = false;
    } else if (std::fabs(e->probability - v.probability) > 1e-6) {
      v.probabilityMixed = true;
    }
  }
  return v;
}

}  // namespace chopfractal::ui
