#pragma once
// Owns the wave-two drawer panels and their shared command object, registers them through the shell's drawer API, and runs
// the background tick that has to work while no drawer is open (a finished stretch is applied, boundary notices reach the
// status line).
#include <functional>

#include "ArrangementPanel.h"
#include "CapturePanel.h"
#include "EvolvePanel.h"
#include "EditorCommands.h"
#include "EditorModel.h"
#include "OutputsPanel.h"
#include "PadsPanel.h"
#include "PerformCommands.h"
#include "ScenesPanel.h"
#include "TempoPanel.h"

class PerformDrawers : private juce::Timer {
 public:
  using Register = std::function<void(const juce::String& id, const juce::String& title, juce::Component& content, int preferredWidth)>;
  PerformDrawers(ChopFractalProcessor&, EditorModel&, EditorCommands&);
  ~PerformDrawers() override { stopTimer(); }
  void registerAll(const Register&);

  PerformCommands& commands() { return perform_; }
  CapturePanel& capture() { return capture_; }
  TempoPanel& tempo() { return tempo_; }
  PadsPanel& pads() { return pads_; }
  ScenesPanel& scenes() { return scenes_; }
  EvolvePanel& evolve() { return evolve_; }
  OutputsPanel& outputs() { return outputs_; }
  ArrangementPanel& arrangement() { return arrangement_; }

 private:
  void timerCallback() override { perform_.background(); }

  PerformCommands perform_;
  CapturePanel capture_;
  TempoPanel tempo_;
  PadsPanel pads_;
  ScenesPanel scenes_;
  EvolvePanel evolve_;
  OutputsPanel outputs_;
  ArrangementPanel arrangement_;
};
