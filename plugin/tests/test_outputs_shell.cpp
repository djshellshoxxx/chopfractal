// Multiple outputs (F11) through the real processor: the bus layout rules, the aux channels of the process buffer and the
// save and load of the routing. The editor's outputs panel is wired by the GUI work; these tests drive the same calls.
#include <cmath>
#include <vector>

#include "PluginProcessor.h"
#include "chop_test.hpp"
#include "shell_test_support.hpp"

namespace cf = chopfractal;
using namespace shelltest;

namespace {
// Enables the first `n` aux outputs the way a host does, then prepares.
bool enableAux(ChopFractalProcessor& p, int n) {
  auto layout = p.getBusesLayout();
  for (int i = 1; i <= n; ++i) layout.outputBuses.getReference(i) = juce::AudioChannelSet::stereo();
  if (!p.setBusesLayout(layout)) return false;
  p.prepareToPlay(48000.0, 512);  // not prepare(): setPlayConfigDetails would reset the bus layout
  return true;
}

struct Run {
  std::vector<float> main;                 // left channel
  std::vector<std::vector<float>> aux;     // left channel of aux bus 1..n
};

Run play(ChopFractalProcessor& p, int auxBuses, int blocks) {
  FakePlayHead ph;
  p.setPlayHead(&ph);
  Run r;
  r.aux.resize(static_cast<std::size_t>(auxBuses));
  for (int b = 0; b < blocks; ++b) {
    ph.ppq = static_cast<double>(b) * 512.0 / 24000.0;
    juce::AudioBuffer<float> buf(2 + 2 * auxBuses, 512);
    buf.clear();
    juce::MidiBuffer midi;
    p.processBlock(buf, midi);
    for (int i = 0; i < 512; ++i) r.main.push_back(buf.getSample(0, i));
    for (int a = 0; a < auxBuses; ++a)
      for (int i = 0; i < 512; ++i) r.aux[static_cast<std::size_t>(a)].push_back(buf.getSample(2 + 2 * a, i));
  }
  p.setPlayHead(nullptr);
  return r;
}

float peak(const std::vector<float>& v) {
  float m = 0.f;
  for (float x : v) m = std::max(m, std::fabs(x));
  return m;
}
}  // namespace

CHOP_TEST(the_processor_offers_eight_disabled_aux_outputs) {
  ChopFractalProcessor p;
  CHECK(p.getBusCount(false) == 9);
  CHECK(p.getBus(false, 0)->isEnabled());
  for (int i = 1; i <= 8; ++i) CHECK(!p.getBus(false, i)->isEnabled());
  prepare(p);
  CHECK(p.activeAuxBuses() == 0);
}

CHOP_TEST(aux_output_layouts_must_be_stereo_or_disabled) {
  ChopFractalProcessor p;
  auto layout = p.getBusesLayout();
  CHECK(p.isBusesLayoutSupported(layout));
  layout.outputBuses.getReference(1) = juce::AudioChannelSet::stereo();
  CHECK(p.isBusesLayoutSupported(layout));
  layout.outputBuses.getReference(1) = juce::AudioChannelSet::mono();
  CHECK(!p.isBusesLayoutSupported(layout));
  layout.outputBuses.getReference(1) = juce::AudioChannelSet::stereo();
  layout.outputBuses.getReference(8) = juce::AudioChannelSet::stereo();
  CHECK(p.isBusesLayoutSupported(layout));
}

CHOP_TEST(enabling_an_aux_output_is_reported_and_main_only_routing_leaves_main_unchanged) {
  ChopFractalProcessor ref;
  prepare(ref);
  makeSession(ref);
  const Run base = play(ref, 0, 8);

  ChopFractalProcessor p;
  CHECK(enableAux(p, 2));
  CHECK(p.activeAuxBuses() == 2);
  CHECK(p.auxBusEnabled(1) && p.auxBusEnabled(2) && !p.auxBusEnabled(3));
  makeSession(p);
  const Run r = play(p, 2, 8);
  CHECK(peak(base.main) > 0.01f);
  CHECK(r.main == base.main);  // aux outputs enabled but nothing routed: main is bit-identical
  CHECK(peak(r.aux[0]) == 0.f && peak(r.aux[1]) == 0.f);
}

CHOP_TEST(routed_chops_play_on_their_aux_output_and_leave_main) {
  ChopFractalProcessor p;
  CHECK(enableAux(p, 2));
  makeSession(p);
  p.withSession([](cf::composition::ProjectSession& s) { CHECK(s.autoRouteByChop() > 0); });
  const Run r = play(p, 2, 16);
  CHECK(peak(r.aux[0]) > 0.01f || peak(r.aux[1]) > 0.01f);
  CHECK(peak(r.main) > 0.01f);  // chops routed to a bus the host did not enable fold into main
}

CHOP_TEST(routing_survives_a_save_and_load_through_the_processor) {
  ChopFractalProcessor a;
  CHECK(enableAux(a, 1));
  makeSession(a);
  cf::composition::OutputRouting saved;
  a.withSession([&](cf::composition::ProjectSession& s) {
    CHECK(s.autoRouteByChop() > 0);
    saved = s.outputRouting();
  });
  juce::MemoryBlock state;
  a.getStateInformation(state);

  ChopFractalProcessor b;
  CHECK(enableAux(b, 1));
  b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  waitForRestore(b);
  cf::composition::OutputRouting loaded;
  b.withSession([&](cf::composition::ProjectSession& s) { loaded = s.outputRouting(); });
  CHECK(loaded == saved);
  CHECK(b.activeAuxBuses() == 1);
}

