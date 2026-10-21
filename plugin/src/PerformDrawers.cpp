#include "PerformDrawers.h"

PerformDrawers::PerformDrawers(ChopFractalProcessor& proc, EditorModel& model, EditorCommands& commands)
    : perform_(proc, model, commands), capture_(model, commands, perform_), tempo_(model, commands, perform_), pads_(model, commands, perform_, proc.apvts), scenes_(model, commands, perform_), evolve_(model, commands), outputs_(model, commands, perform_), arrangement_(model, commands, perform_) {
  startTimerHz(10);
}

void PerformDrawers::registerAll(const Register& reg) {
  reg("capture", "Capture", capture_, 340);
  reg("tempo", "Tempo", tempo_, 340);
  reg("pads", "Pads", pads_, 380);
  reg("scenes", "Scenes", scenes_, 380);
  reg("evolve", "Evolve", evolve_, 360);
  reg("outputs", "Outputs", outputs_, 340);
  reg("arrange", "Arrange", arrangement_, 380);
}
