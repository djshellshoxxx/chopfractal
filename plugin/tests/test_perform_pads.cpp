// Pads drawer (F07 section 12): the 4x4 grid, velocity from the press position, release on mouse up and on close, the settings
// boxes, host parameter attachments, record / discard, MIDI learn, all through the real processor and synthesized mouse events.
#include <cmath>

#include "perform_test_support.hpp"

using namespace shelltest;
namespace cf = chopfractal;

namespace {
juce::Point<int> padPoint(ChopFractalEditor& ed, PadCellComponent& pad, float fy = 0.5f) {
  const auto b = pad.getLocalBounds();
  return ed.getLocalPoint(&pad, juce::Point<int>(b.getCentreX(), static_cast<int>(static_cast<float>(b.getHeight()) * fy)));
}
float peakOf(ChopFractalProcessor& p, int blocks = 3) {
  float m = 0.f;
  for (int i = 0; i < blocks; ++i) {
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    juce::MidiBuffer midi;
    p.processBlock(buf, midi);
    for (int c = 0; c < 2; ++c) m = std::max(m, buf.getMagnitude(c, 0, 512));
  }
  return m;
}
void silence(ChopFractalProcessor& p, int blocks = 40) { peakOf(p, blocks); }
void emptyPattern(ChopFractalProcessor& p) {
  p.withSession([](cf::composition::ProjectSession& s) {
    CHECK(s.edit([](const cf::pattern::Pattern& pat, const cf::ChopSnapshot&) -> cf::Result<cf::pattern::Pattern> {
                cf::pattern::Pattern q = pat;
                for (auto& bar : q.bars)
                  for (auto& beat : bar.beats) beat.events.clear();
                return q;
              })
              .ok());
  });
}
}  // namespace

CHOP_TEST(pads_drawer_shows_sixteen_described_pads_and_an_empty_state) {
  if (!haveDisplay()) return;
  {
    PerformFixture f;  // no source yet
    f.editor->openDrawer("pads");
    auto& panel = f.editor->perform().pads();
    panel.refreshNow();
    CHECK(!panel.view().padsEnabled && !panel.view().emptyText.empty());
    for (int i = 0; i < 16; ++i) CHECK(!panel.grid().pad(i).cell().enabled);
    CHECK(!findButton(panel, "Record Take")->isEnabled());
  }
  PerformFixture f(true);
  f.editor->openDrawer("pads");
  auto& panel = f.editor->perform().pads();
  panel.refreshNow();
  checkDescribed(panel, "pads");
  int chops = 0;
  f.proc.withSession([&](cf::composition::ProjectSession& s) { chops = static_cast<int>(s.chops()->chops.size()); });
  CHECK(chops > 0 && chops <= 16);
  for (int i = 0; i < 16; ++i) {
    auto& pad = panel.grid().pad(i);
    CHECK(pad.getName() == "Pad " + juce::String(i + 1));
    CHECK(pad.getTitle().isNotEmpty() && pad.getTooltip().isNotEmpty());
    CHECK(pad.cell().enabled == (i < chops));
  }
  CHECK(panel.grid().pad(0).cell().noteName == "C1" && panel.grid().pad(0).cell().note == 36);
  CHECK(!findCombo(panel, "Pad page")->isVisible());  // one page only
}

CHOP_TEST(pad_press_plays_with_velocity_from_the_position_and_release_stops_it) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  emptyPattern(f.proc);
  f.editor->openDrawer("pads");
  auto& panel = f.editor->perform().pads();
  panel.refreshNow();
  silence(f.proc);
  MouseSim sim(*f.editor);
  auto& pad = panel.grid().pad(0);
  sim.down(padPoint(*f.editor, pad, 0.05f));  // top edge: loud
  CHECK(sim.target() == &pad && pad.held());
  const float loud = peakOf(f.proc);
  sim.up(padPoint(*f.editor, pad, 0.05f));
  CHECK(!pad.held());
  silence(f.proc);
  sim.down(padPoint(*f.editor, pad, 0.95f));  // bottom edge: soft
  const float soft = peakOf(f.proc);
  sim.up(padPoint(*f.editor, pad, 0.95f));
  CHECK(loud > 0.f && soft > 0.f && loud > soft * 1.5f);
  // An empty pad does nothing.
  silence(f.proc);
  auto& empty = panel.grid().pad(15);
  CHECK(!empty.cell().enabled);
  sim.down(padPoint(*f.editor, empty));
  CHECK(!empty.held() && peakOf(f.proc) == 0.f);
  sim.up(padPoint(*f.editor, empty));
}

CHOP_TEST(closing_the_drawer_releases_held_pads) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  emptyPattern(f.proc);
  f.editor->openDrawer("pads");
  auto& panel = f.editor->perform().pads();
  panel.refreshNow();
  selectItem(*findCombo(panel, "Pad mode"), "Gate");
  silence(f.proc);
  MouseSim sim(*f.editor);
  auto& pad = panel.grid().pad(1);
  sim.down(padPoint(*f.editor, pad));
  peakOf(f.proc, 2);
  CHECK(f.proc.activePadVoices() > 0);
  f.editor->closeDrawer();  // the pad is still "held" by the mouse: closing must release it
  silence(f.proc, 60);
  CHECK(f.proc.activePadVoices() == 0);
  sim.up(padPoint(*f.editor, pad));
}

CHOP_TEST(pad_settings_boxes_write_the_session_and_the_host_parameters) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  f.editor->openDrawer("pads");
  auto& panel = f.editor->perform().pads();
  panel.refreshNow();
  auto* base = [&] {
    juce::Slider* found = nullptr;
    forEachControl(panel, [&](juce::Component& c) {
      if (auto* s = dynamic_cast<juce::Slider*>(&c))
        if (s->getTitle() == "Base note") found = s;
    });
    return found;
  }();
  CHECK(base != nullptr);
  base->setValue(48, juce::sendNotificationSync);
  CHECK(f.proc.padSettings().baseNote == 48);
  panel.refreshNow();
  CHECK(panel.grid().pad(0).cell().noteName == "C2" && panel.grid().pad(0).cell().note == 48);
  CHECK(selectItem(*findCombo(panel, "MIDI channel"), "3"));
  CHECK(f.proc.padSettings().midiChannel == 3);
  CHECK(selectItem(*findCombo(panel, "Quantize"), "1/8"));
  CHECK(f.proc.padSettings().quantizeDivision == 8 && !f.proc.padSettings().quantizeTriplet);
  CHECK(selectItem(*findCombo(panel, "Quantize"), "1/16T"));
  CHECK(f.proc.padSettings().quantizeDivision == 16 && f.proc.padSettings().quantizeTriplet);
  CHECK(selectItem(*findCombo(panel, "Merge"), "Replace slots"));
  CHECK(f.proc.padSettings().merge == 1);
  CHECK(selectItem(*findCombo(panel, "Pad mode"), "Mono"));
  CHECK(std::lround(f.proc.apvts.getRawParameterValue("perf_mode")->load()) == 1);
  CHECK(selectItem(*findCombo(panel, "Velocity curve"), "Hard"));
  CHECK(std::lround(f.proc.apvts.getRawParameterValue("perf_velocity_curve")->load()) == 2);
  // The host moves the parameter: the box follows.
  auto* param = f.proc.apvts.getParameter("perf_mode");
  param->setValueNotifyingHost(param->convertTo0to1(2.f));
  pump(30);
  CHECK(findCombo(panel, "Pad mode")->getText() == "Gate");
  // The choke group menu's command.
  f.editor->perform().commands().setChokeGroup(1, 3);
  panel.refreshNow();
  CHECK(panel.grid().pad(1).cell().chokeGroup == 3);
}

CHOP_TEST(pad_record_take_discard_and_midi_learn) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  emptyPattern(f.proc);
  f.editor->openDrawer("pads");
  auto& panel = f.editor->perform().pads();
  panel.refreshNow();
  auto* record = findButton(panel, "Record Take");
  CHECK(record->isEnabled());
  record->setToggleState(true, juce::sendNotificationSync);
  CHECK(f.proc.recordArmed());
  FakePlayHead ph;
  ph.playing = true;
  ph.ppq = 0.25;
  f.proc.setPlayHead(&ph);
  MouseSim sim(*f.editor);
  auto& pad = panel.grid().pad(0);
  sim.down(padPoint(*f.editor, pad));
  peakOf(f.proc, 2);
  sim.up(padPoint(*f.editor, pad));
  peakOf(f.proc, 2);
  f.proc.pollAudioFlags();
  f.proc.setPlayHead(nullptr);
  CHECK(f.proc.takeHits() >= 1);
  panel.refreshNow();
  CHECK(panel.view().discardEnabled);
  CHECK(findButton(panel, juce::String(panel.view().discardLabel)) != nullptr);
  tap(findButton(panel, juce::String(panel.view().discardLabel)));
  CHECK(f.proc.takeHits() == 0);
  record->setToggleState(false, juce::sendNotificationSync);
  CHECK(!f.proc.recordArmed());

  // MIDI learn: the next controller note becomes the first pad.
  auto* learn = findButton(panel, "Learn");
  learn->setToggleState(true, juce::sendNotificationSync);
  CHECK(panel.learning());
  juce::AudioBuffer<float> buf(2, 512);
  buf.clear();
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 52, static_cast<juce::uint8>(100)), 10);
  f.proc.processBlock(buf, midi);
  panel.refreshNow();
  CHECK(panel.lastMidiNote() == 52);
  CHECK(!panel.learning());
  CHECK(f.proc.padSettings().baseNote == 52);
  panel.refreshNow();
  CHECK(panel.grid().pad(0).cell().note == 52);
  // A key that arrives while Learn is off only updates the display.
  midi.clear();
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 10);
  f.proc.processBlock(buf, midi);
  panel.refreshNow();
  CHECK(panel.lastMidiNote() == 60 && f.proc.padSettings().baseNote == 52);
}
