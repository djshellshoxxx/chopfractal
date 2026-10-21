// Tempo drawer (F14 section 3): loop length choices, live preview, confirm, the stretch job and Undo Stretch, driven through
// the real processor.
#include "perform_test_support.hpp"

using namespace shelltest;
namespace cf = chopfractal;

namespace {
juce::String sourceName(ChopFractalProcessor& p) {
  juce::String name;
  p.withSession([&](cf::composition::ProjectSession& s) { name = s.chopMap() ? juce::String(s.chopMap()->info().name) : juce::String(); });
  return name;
}
}  // namespace

CHOP_TEST(tempo_drawer_offers_loop_lengths_and_a_live_preview) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  CHECK(f.editor->openDrawer("tempo"));
  auto& panel = f.editor->perform().tempo();
  panel.refreshNow();
  checkDescribed(panel, "tempo");
  auto* loop = findCombo(panel, "Loop length");
  CHECK(loop != nullptr && loop->getNumItems() >= 6);
  CHECK(loop->getItemText(loop->getNumItems() - 1) == "Custom BPM");
  CHECK(selectItem(*loop, "1 bar"));  // 2 s loop: 1 bar = 120 BPM, the manual tempo
  CHECK(!findTextEditor(panel, "Source BPM")->isReadOnly() == false);
  CHECK(panel.preview().text == "Already at the target tempo." && !findButton(panel, "Match to host")->isEnabled());
  CHECK(selectItem(*loop, "4 bars"));  // 480 BPM at 120: a 4x change
  CHECK(panel.preview().text.rfind("Out of range", 0) == 0 && !findButton(panel, "Match to host")->isEnabled());
  CHECK(selectItem(*loop, "Custom BPM"));
  CHECK(!findTextEditor(panel, "Source BPM")->isReadOnly());
  findTextEditor(panel, "Source BPM")->setText("96", true); pump(5);
  findTextEditor(panel, "Target BPM")->setText("120", true); pump(5);
  CHECK(panel.preview().text == "96.0 -> 120.0 BPM: 125% speed, length x0.800 (2.0 s -> 1.6 s)");
  CHECK(findButton(panel, "Match to host")->isEnabled());
  findTextEditor(panel, "Source BPM")->setText("60", true); pump(5);  // 0.5x: allowed but large
  CHECK(panel.preview().level == cf::composition::StretchPreview::Level::Warning && panel.preview().advice.rfind("Large change", 0) == 0);
  findTextEditor(panel, "Source BPM")->setText("20", true); pump(5);  // beyond 2x
  CHECK(!findButton(panel, "Match to host")->isEnabled());
  // Use host resets the target to the tempo in use.
  findTextEditor(panel, "Target BPM")->setText("90", true); pump(5);
  click(panel, "Use host");
  CHECK(findTextEditor(panel, "Target BPM")->getText() == "120.0");
  // Quality choices.
  auto* q = findCombo(panel, "Quality");
  CHECK(q->getNumItems() == 3 && q->getSelectedId() == 2 && q->getItemText(0) == "Draft" && q->getItemText(2) == "High");
}

CHOP_TEST(tempo_match_asks_runs_the_job_keeps_the_chops_and_undo_stretch_goes_back) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  f.editor->openDrawer("tempo");
  auto& panel = f.editor->perform().tempo();
  panel.refreshNow();
  selectItem(*findCombo(panel, "Loop length"), "Custom BPM");
  findTextEditor(panel, "Source BPM")->setText("96", true); pump(5);
  findTextEditor(panel, "Target BPM")->setText("120", true); pump(5);
  const auto before = patternBytes(f.proc);
  int chopsBefore = 0;
  f.proc.withSession([&](cf::composition::ProjectSession& s) { chopsBefore = static_cast<int>(s.chops()->chops.size()); });
  CHECK(!findButton(panel, "Undo Stretch")->isVisible());
  f.answer = false;
  click(panel, "Match to host");
  CHECK(f.asked == 1 && !f.editor->perform().commands().stretch().running());  // Cancel at the confirm: nothing starts
  f.answer = true;
  click(panel, "Match to host");
  CHECK(f.asked == 2);
  CHECK(waitFor([&] { return sourceName(f.proc).contains("@120.0 BPM"); }, 20000));
  panel.refreshNow();
  CHECK(findButton(panel, "Undo Stretch")->isVisible());
  CHECK(!f.editor->perform().commands().stretch().running());
  int chopsAfter = 0;
  f.proc.withSession([&](cf::composition::ProjectSession& s) {
    chopsAfter = static_cast<int>(s.chops()->chops.size());
    CHECK(s.chopMap()->info().frames == 76800);  // 96000 * 96 / 120
    CHECK(s.sourceNeedsEmbed());
  });
  CHECK(chopsAfter == chopsBefore);
  CHECK(f.proc.statusMessage().contains("saved inside the project") || f.proc.statusMessage().contains("Stretched to 120.0 BPM"));
  click(panel, "Undo Stretch");
  panel.refreshNow();
  CHECK(!sourceName(f.proc).contains("@120.0 BPM"));
  CHECK(!findButton(panel, "Undo Stretch")->isVisible());
  f.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK(s.chopMap()->info().frames == 96000); });
  CHECK(patternBytes(f.proc) == before);
}

CHOP_TEST(tempo_undo_stretch_ends_when_the_project_moves_on_and_cancel_is_safe) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  f.editor->openDrawer("tempo");
  auto& panel = f.editor->perform().tempo();
  panel.refreshNow();
  selectItem(*findCombo(panel, "Loop length"), "Custom BPM");
  findTextEditor(panel, "Source BPM")->setText("100", true); pump(5);
  findTextEditor(panel, "Target BPM")->setText("120", true); pump(5);
  selectItem(*findCombo(panel, "Quality"), "Draft");
  click(panel, "Match to host");
  CHECK(waitFor([&] { return sourceName(f.proc).contains("@120.0 BPM"); }, 20000));
  panel.refreshNow();
  CHECK(findButton(panel, "Undo Stretch")->isVisible());
  f.editor->commands().mutate();  // a new variation: the undo window closes
  panel.refreshNow();
  CHECK(!findButton(panel, "Undo Stretch")->isVisible());
  // Cancel straight after the start: either outcome leaves a consistent project and no running job.
  findTextEditor(panel, "Source BPM")->setText("120", true); pump(5);
  findTextEditor(panel, "Target BPM")->setText("100", true); pump(5);
  panel.refreshNow();
  click(panel, "Match to host");
  f.editor->perform().commands().cancelStretch();
  CHECK(waitFor([&] { return !f.editor->perform().commands().stretch().running(); }, 20000));
  CHECK(sourceName(f.proc).isNotEmpty());
}

CHOP_TEST(tempo_detect_runs_smart_setup_and_selects_the_top_suggestion) {
  if (!haveDisplay()) return;
  PerformFixture f(true);
  f.editor->openDrawer("tempo");
  auto& panel = f.editor->perform().tempo();
  panel.refreshNow();
  selectItem(*findCombo(panel, "Loop length"), "Custom BPM");
  click(panel, "Detect Tempo");
  panel.refreshNow();
  CHECK(f.editor->model().haveSetup() && !f.editor->model().setup().loops.empty());
  auto* loop = findCombo(panel, "Loop length");
  const juce::String top = juce::String(f.editor->model().setup().loops.front().note);
  CHECK(loop->getText() == top);
  CHECK(findTextEditor(panel, "Source BPM")->isReadOnly());
  CHECK(findTextEditor(panel, "Source BPM")->getText() == juce::String(f.editor->model().setup().loops.front().bpm, 1));
}
