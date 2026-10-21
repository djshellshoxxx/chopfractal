#include "FxRackPanel.h"

#include "CdlLookAndFeel.h"

namespace cf = chopfractal;
using cf::ui::FxControlKind;
using cf::ui::FxModule;

namespace {
constexpr int kHeaderH = 30, kRowH = 28, kPad = 10;
}

juce::Component* FxRackPanel::Control::component() const {
  if (slider) return slider.get();
  if (choice) return choice.get();
  return toggle.get();
}

FxRackPanel::FxRackPanel(EditorModel& model, EditorCommands& commands)
    : EditorPanelBase(model, commands, "FxRackPanel"),
      chance_("Chance", "How often the hit plays when the loop comes round", 0.0, 1.0, 0.01, [](double v) { return juce::String(static_cast<int>(std::lround(v * 100.0))) + " %"; }) {
  addAndMakeVisible(viewport_);
  viewport_.setViewedComponent(&body_, false);
  viewport_.setScrollBarsShown(true, false);
  viewport_.setComponentID("FxRackViewport");
  title_.setInterceptsMouseClicks(false, false);
  refusal_.setInterceptsMouseClicks(false, false);
  body_.addAndMakeVisible(title_);
  body_.addAndMakeVisible(refusal_);

  for (int m = 0; m < cf::ui::kFxModuleCount; ++m) {
    auto& sec = sections_[static_cast<std::size_t>(m)];
    sec.module = static_cast<FxModule>(m);
    sec.header = std::make_unique<juce::ToggleButton>(cf::ui::fxModuleName(sec.module));
    sec.header->setTitle(cf::ui::fxModuleName(sec.module));
    sec.header->setTooltip(cf::ui::fxModuleTooltip(sec.module));
    sec.header->onClick = [this, m] {
      if (!syncing_) setModule(m, sections_[static_cast<std::size_t>(m)].header->getToggleState());
    };
    body_.addAndMakeVisible(*sec.header);
    for (const auto& spec : cf::ui::fxControlSpecs(sec.module)) {
      Control c;
      c.spec = spec;
      const std::string id = spec.id;
      if (spec.kind == FxControlKind::Slider) {
        const FxModule mod = sec.module;
        c.slider = std::make_unique<ParamRow>(spec.label, spec.tooltip, spec.min, spec.max, spec.step, [mod, spec](double v) { return juce::String(cf::ui::fxControlText(mod, spec, v)); });
        c.slider->setDefault(spec.def);
        c.slider->onCommit = [this, m, id](double v) { setControl(m, id, v); };
        c.slider->setLabelWidth(84);
      } else if (spec.kind == FxControlKind::Choice) {
        c.label = std::make_unique<juce::Label>();
        c.label->setText(spec.label, juce::dontSendNotification);
        c.label->setTooltip(spec.tooltip);
        c.label->setInterceptsMouseClicks(false, false);
        c.choice = std::make_unique<juce::ComboBox>();
        c.choice->setTitle(sec.header->getButtonText() + " " + spec.label);
        c.choice->setTooltip(spec.tooltip);
        for (std::size_t i = 0; i < spec.choices.size(); ++i) c.choice->addItem(spec.choices[i], static_cast<int>(i) + 1);
        auto* cb = c.choice.get();
        cb->onChange = [this, m, id, cb] {
          if (!syncing_ && cb->getSelectedId() > 0) setControl(m, id, cb->getSelectedId() - 1);
        };
        body_.addAndMakeVisible(*c.label);
      } else {
        c.toggle = std::make_unique<juce::ToggleButton>(spec.label);
        c.toggle->setTitle(sec.header->getButtonText() + " " + spec.label);
        c.toggle->setTooltip(spec.tooltip);
        auto* tb = c.toggle.get();
        tb->onClick = [this, m, id, tb] {
          if (!syncing_) setControl(m, id, tb->getToggleState() ? 1.0 : 0.0);
        };
      }
      body_.addAndMakeVisible(*c.component());
      sec.controls.push_back(std::move(c));
    }
  }
  chance_.setDefault(1.0);
  chance_.onCommit = [this](double v) { cmd_.setHitsProbability(model_.targetHits(), static_cast<float>(v)); };
  body_.addAndMakeVisible(chance_);
  copy_.setTooltip("Remember the effects of the first selected hit");
  copy_.onClick = [this] {
    haveClipboard_ = model_.firstHitFx(clipboard_);
    paste_.setEnabled(haveClipboard_ && view_.editable);
    cmd_.say(haveClipboard_ ? "Effects copied." : "Select a hit first.");
  };
  paste_.setTooltip("Give the selected hits the copied effects");
  paste_.onClick = [this] {
    if (!haveClipboard_) return;
    const auto fx = clipboard_;
    cmd_.setHitsFxState(model_.targetHits(), [fx](cf::ui::HitFx& h) { h = fx; });
  };
  reset_.setTooltip("Switch every effect off on the selected hits");
  reset_.onClick = [this] {
    cmd_.setHitsFxState(model_.targetHits(), [](cf::ui::HitFx& h) { h = cf::ui::HitFx{}; });
  };
  for (juce::TextButton* b : {&copy_, &paste_, &reset_}) {
    b->setTitle(b->getButtonText());
    body_.addAndMakeVisible(*b);
  }
  paste_.setEnabled(false);
  refresh();
}

void FxRackPanel::setModule(int index, bool on) {
  const auto module = static_cast<FxModule>(index);
  const auto ids = model_.targetHits();
  auto& memory = remembered_;
  cmd_.setHitsFxState(ids, [&memory, index, module, on](cf::ui::HitFx& h) {
    if (!on && memory.find(index) == memory.end()) memory[index] = h;  // the first hit's state is what a toggle back restores
    const cf::ui::HitFx* restore = on && memory.count(index) ? &memory[index] : nullptr;
    cf::ui::fxSetModuleOn(h, module, on, restore);
  });
  if (!on) {
    // keep the remembered state only until the next time the module is switched on
  } else {
    memory.erase(index);
  }
}

void FxRackPanel::setControl(int index, const std::string& id, double value) {
  const auto module = static_cast<FxModule>(index);
  cmd_.setHitsFxState(model_.targetHits(), [module, id, value](cf::ui::HitFx& h) { cf::ui::fxSetControl(h, module, id, value); });
}

void FxRackPanel::modelChanged(Change c) {
  if (c == Change::View || c == Change::Selection) refresh();
}

void FxRackPanel::refresh() {
  for (const auto& sec : sections_)
    for (const auto& c : sec.controls)
      if (c.slider && c.slider->dragging()) return;
  if (chance_.dragging()) return;
  view_ = model_.fxRack();
  syncing_ = true;
  title_.setText(juce::String(view_.title), juce::dontSendNotification);
  refusal_.setText(juce::String(view_.refusal), juce::dontSendNotification);
  const bool edit = view_.hitCount > 0 && view_.editable;
  for (std::size_t m = 0; m < sections_.size() && m < view_.modules.size(); ++m) {
    auto& sec = sections_[m];
    const auto& mv = view_.modules[m];
    sec.header->setButtonText(mv.mixed ? juce::String(mv.name) + " (" + juce::String(mv.onCount) + " of " + juce::String(mv.total) + ")" : juce::String(mv.name));
    sec.header->setToggleState(mv.on, juce::dontSendNotification);
    sec.header->setEnabled(edit);
    sec.expanded = mv.on || mv.mixed;
    for (std::size_t i = 0; i < sec.controls.size() && i < mv.controls.size(); ++i) {
      auto& c = sec.controls[i];
      const auto& cv = mv.controls[i];
      if (c.slider) c.slider->setValue(cv.value, cv.mixed);
      if (c.choice) {
        if (cv.mixed) c.choice->setSelectedId(0, juce::dontSendNotification);
        else c.choice->setSelectedId(static_cast<int>(std::lround(cv.value)) + 1, juce::dontSendNotification);
        c.choice->setTextWhenNothingSelected(cv.mixed ? "mixed" : "");
      }
      if (c.toggle) c.toggle->setToggleState(cv.value >= 0.5 && !cv.mixed, juce::dontSendNotification);
      c.component()->setEnabled(edit);
    }
  }
  chance_.setValue(view_.probability, view_.probabilityMixed);
  chance_.setEnabled(edit);
  copy_.setEnabled(view_.hitCount > 0);
  paste_.setEnabled(edit && haveClipboard_);
  reset_.setEnabled(edit);
  syncing_ = false;
  layoutBody();
  repaint();
}

void FxRackPanel::layoutBody() {
  const int w = std::max(220, viewport_.getMaximumVisibleWidth());
  int y = 8;
  auto row = [&](int h) { return juce::Rectangle<int>(kPad, y, w - 2 * kPad, h); };
  title_.setBounds(row(22));
  y += 22;
  const int refusalH = view_.refusal.empty() ? 4 : 34;
  refusal_.setBounds(row(refusalH));
  y += refusalH + 4;
  for (auto& sec : sections_) {
    sec.header->setBounds(row(kHeaderH));
    y += kHeaderH;
    for (auto& c : sec.controls) {
      auto r = row(kRowH);
      c.component()->setVisible(sec.expanded);
      if (c.label) c.label->setVisible(sec.expanded);
      if (sec.expanded) {
        r.removeFromLeft(14);
        if (c.label) {
          c.label->setBounds(r.removeFromLeft(84 + 14 - 14));
          c.choice->setBounds(r);
        } else {
          c.component()->setBounds(r);
        }
        y += kRowH;
      }
    }
    y += 4;
  }
  y += 6;
  chance_.setBounds(row(30));
  y += 34;
  auto r = row(30);
  copy_.setBounds(r.removeFromLeft(r.getWidth() / 3 - 4));
  r.removeFromLeft(6);
  paste_.setBounds(r.removeFromLeft(r.getWidth() / 2 - 3));
  r.removeFromLeft(6);
  reset_.setBounds(r);
  y += 38;
  body_.setSize(w, y);
}

void FxRackPanel::paint(juce::Graphics& g) { g.fillAll(cdl::pal().panel); }

void FxRackPanel::resized() {
  viewport_.setBounds(getLocalBounds());
  layoutBody();
}
