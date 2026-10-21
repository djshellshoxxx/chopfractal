// EditorModel and EditorCommands without a window: view building, selection rules, change notifications and the
// refusal messages. The processor is the real one; no display is needed.
#include <algorithm>
#include <map>

#include "EditorModel.h"
#include "PluginProcessor.h"
#include "shell_test_support.hpp"

using namespace shelltest;

namespace {
struct CountingListener : EditorModel::Listener {
  std::map<EditorModel::Change, int> counts;
  void modelChanged(EditorModel::Change c) override { ++counts[c]; }
  int count(EditorModel::Change c) const {
    const auto it = counts.find(c);
    return it == counts.end() ? 0 : it->second;
  }
};
}  // namespace

CHOP_TEST(model_refresh_builds_views_only_when_the_session_changes) {
  ChopFractalProcessor p;
  prepare(p);
  EditorModel m(p);
  CHECK(!m.view().hasSource);
  CHECK(!m.view().hasPattern);
  CHECK(m.refresh(true, 920));
  CHECK(!m.view().hasSource);
  CHECK(!m.refresh(false, 920));  // nothing changed
  CHECK(m.view().revision == 1);

  makeSession(p);
  CHECK(m.refresh(false, 920));
  CHECK(m.view().revision == 2);
  const auto& v = m.view();
  CHECK(v.hasSource && v.hasPattern);
  CHECK(v.sourceFrames == 96000);
  CHECK(!v.peaks.min.empty() && static_cast<int>(v.peaks.min.size()) == 920);
  std::size_t markers = 0, history = 0;
  std::size_t rectsFromAdapter = 0, arcsFromAdapter = 0;
  p.withSession([&](cf::composition::ProjectSession& s) {
    markers = s.chopMap()->markers().size();
    history = s.history().size();
    const auto pv = cf::ui::buildPatternView(*s.pattern(), *s.chops());
    rectsFromAdapter = pv.rects.size();
    arcsFromAdapter = cf::ui::buildOrbitView(*s.pattern(), *s.chops()).arcs.size();
  });
  CHECK(v.markers.size() == markers);
  CHECK(!v.pattern.rects.empty());
  CHECK(v.pattern.rects.size() == rectsFromAdapter);
  CHECK(v.orbit.arcs.size() == arcsFromAdapter);
  CHECK(v.tree.size() == history);
  CHECK(v.chops.size() >= 2);
  CHECK(v.gridTicks > 0 && v.barTicks > 0 && v.barCount == 2);
  CHECK(!m.refresh(false, 920));
}

CHOP_TEST(model_selection_survives_rebuild_and_clears_when_the_hit_is_gone) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  EditorModel m(p);
  m.refresh(true, 920);
  CHECK(!m.hasSelection());
  CHECK(m.selectedChopIndex() == -1 && m.selectedBar() == 0);
  const auto first = m.view().pattern.rects.front();
  m.select(first.id);
  CHECK(m.hasSelection() && m.selected() == first.id);
  CHECK(m.selectedChopIndex() == first.chopIndex);
  CHECK(m.selectedBar() == first.bar);

  // Mutating with amount 0 rebuilds the view and keeps every hit: the selection stays.
  p.withSession([&](cf::composition::ProjectSession& s) { cf::pattern::MutateOptions o; o.seed = 5; o.amount = 0.0; CHECK(s.mutate(o).ok()); });
  CHECK(m.refresh(false, 920));
  CHECK(m.hasSelection());

  // A smaller pattern no longer contains the last hit: the selection is dropped by the rebuild.
  const auto last = m.view().pattern.rects.back();
  m.select(last.id);
  p.withSession([&](cf::composition::ProjectSession& s) {
    cf::pattern::Settings st;
    st.bars = 1;
    st.density = 0.15;
    st.seed = 9;
    CHECK(s.generate(st).ok());
  });
  CHECK(m.refresh(false, 920));
  const bool stillThere = std::any_of(m.view().pattern.rects.begin(), m.view().pattern.rects.end(), [&](const auto& r) { return r.id == last.id; });
  CHECK(!stillThere);
  CHECK(!m.hasSelection());
  m.select(m.view().pattern.rects.front().id);
  m.clearSelection();
  CHECK(!m.hasSelection() && m.selectedHits().empty() && m.selectedChops().empty());
}

CHOP_TEST(model_emits_changes_to_listeners_once_per_cause) {
  using Change = EditorModel::Change;
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  EditorModel m(p);
  CountingListener l;
  m.addListener(&l);
  m.refresh(true, 920);
  CHECK(l.count(Change::View) == 1);
  CHECK(l.count(Change::Evolve) == 1);  // the Evolve sync point is reached by every refresh
  CHECK(l.count(Change::Status) == 1);  // first publication of the status line
  CHECK(l.count(Change::Flags) == 1);
  m.refresh(false, 920);
  CHECK(l.count(Change::View) == 1);  // no rebuild
  CHECK(l.count(Change::Evolve) == 2);
  CHECK(l.count(Change::Status) == 1 && l.count(Change::Flags) == 1);

  const auto id = m.view().pattern.rects.front().id;
  m.select(id);
  m.select(id);  // not a change
  CHECK(l.count(Change::Selection) == 1);
  m.clearSelection();
  CHECK(l.count(Change::Selection) == 2);
  m.clearSelection();
  CHECK(l.count(Change::Selection) == 2);

  m.setSeed(7);
  CHECK(m.seed() == 7 && l.count(Change::Seed) == 1);
  m.setStatus("hello");
  CHECK(l.count(Change::Status) == 2);
  CHECK(m.statusLine().startsWith("hello"));

  p.withSession([&](cf::composition::ProjectSession& s) {
    auto es = s.evolveSettings();
    es.enabled = true;
    es.everyLoops = 2;
    CHECK(s.setEvolve(es).ok());
  });
  const int before = l.count(Change::Evolve);
  m.refresh(false, 920);
  CHECK(l.count(Change::Evolve) == before + 1);
  CHECK(m.evolveRunning());
  CHECK(m.evolve().everyLoops == 2);

  p.embedSource = true;
  m.refresh(false, 920);
  CHECK(l.count(Change::Flags) == 2);
  const int statusBefore = l.count(Change::Status);
  m.removeListener(&l);
  m.setStatus("unheard");
  CHECK(l.count(Change::Status) == statusBefore);
}

CHOP_TEST(model_clears_the_smart_setup_suggestion_when_the_views_are_rebuilt) {
  ChopFractalProcessor p;
  prepare(p);
  makeSession(p);
  EditorModel m(p);
  m.refresh(true, 920);
  CHECK(!m.haveSetup());
  m.setSetup({});
  CHECK(m.haveSetup());
  m.refresh(true, 920);
  CHECK(!m.haveSetup());
}

CHOP_TEST(model_status_line_adds_the_host_fallback_hints) {
  ChopFractalProcessor p;
  EditorModel m(p);
  p.setStatusMessage("Loaded.");
  p.usedFallbackTempo = true;
  p.usedFallbackMeter = true;
  p.manualBpm = 97.0;
  CHECK(m.statusLine() == "Loaded.\nUsing local tempo: 97.0 BPM   |   Assuming 4/4");
  p.usedFallbackTempo = false;
  p.usedFallbackMeter = false;
  CHECK(m.statusLine() == "Loaded.");
  p.setStatusMessage("");
  p.usedFallbackTempo = true;
  CHECK(m.statusLine() == "Using local tempo: 97.0 BPM");
}
