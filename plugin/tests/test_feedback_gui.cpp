// F05 feedback on screen: severity prefix, clip chip, idle repaints, notice log popup, drop hover and the status bar layout.
#include <functional>
#include <utility>

#include "CdlLookAndFeel.h"
#include "workflow_test_support.hpp"

using namespace shelltest;

namespace {
void loudBlocks(ChopFractalProcessor& p, int blocks, float level) {
  FakePlayHead ph;
  ph.playing = false;
  p.setPlayHead(&ph);
  juce::MidiBuffer midi;
  for (int b = 0; b < blocks; ++b) {
    juce::AudioBuffer<float> buf(2, 512);
    for (int c = 0; c < 2; ++c)
      for (int i = 0; i < 512; ++i) buf.setSample(c, i, level * std::sin(0.05f * static_cast<float>(i)));
    p.processBlock(buf, midi);
  }
  p.setPlayHead(nullptr);
}
void setParam(ChopFractalProcessor& p, const char* id, float plain) {
  auto* prm = p.apvts.getParameter(id);
  prm->setValueNotifyingHost(prm->convertTo0to1(plain));
}
}  // namespace

CHOP_TEST(statusbar_shows_a_severity_prefix_and_stays_inside_its_bar) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  rig.proc.setStatusMessage("a");
  pump(150);
  CHECK(rig.ed->statusBar().displayedText() == "a");
  rig.proc.setStatusMessage("a", Severity::Warning);
  pump(150);
  CHECK(rig.ed->statusBar().displayedText() == "[!] a");
  rig.proc.setStatusMessage("a", Severity::Error);
  pump(150);
  CHECK(rig.ed->statusBar().displayedText() == "[x] a");
  // Every child stays inside the bar: no overlap with the footer or each other.
  auto& bar = rig.ed->statusBar();
  const auto barBounds = bar.getLocalBounds();
  juce::Array<juce::Rectangle<int>> rects;
  for (auto* c : bar.getChildren())
    if (c->isVisible()) {
      CHECK(barBounds.contains(c->getBounds()));
      for (const auto& other : rects) CHECK(!other.intersects(c->getBounds()));
      rects.add(c->getBounds());
    }
}

CHOP_TEST(clip_chip_latches_and_a_click_resets_it) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  auto& meter = rig.ed->statusBar().meter();
  CHECK(!meter.clipLatched());
  setParam(rig.proc, "dry_mix", 1.0f);
  setParam(rig.proc, "output_gain_db", 12.0f);
  loudBlocks(rig.proc, 8, 0.9f);
  pump(150);
  CHECK(meter.clipLatched());
  CHECK(meter.state().db[0] > -3.f);
  CHECK(meter.clipChip().getTitle() == "Clear clip indicator");
  CHECK(meter.getTooltip().contains("CLIP lights when the output exceeds 0 dBFS"));
  rig.ed->statusBar().meter().clipChip().triggerClick();
  pump(80);
  CHECK(!meter.clipLatched());
  loudBlocks(rig.proc, 4, 0.9f);
  pump(150);
  CHECK(meter.clipLatched());
  snapshot(*rig.ed, "statusbar_clip");
}

CHOP_TEST(meter_strip_does_not_repaint_when_idle) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  auto& meter = rig.ed->statusBar().meter();
  pump(800);  // let any decay from the rig setup finish
  const int before = meter.repaintCount();
  pump(500);
  CHECK_EQ(meter.repaintCount(), before);
}

CHOP_TEST(notice_log_popup_lists_entries_newest_first_and_clear_empties) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  rig.proc.noticeLog().clear();
  rig.proc.setStatusMessage("first");
  rig.proc.setStatusMessage("second", Severity::Warning);
  rig.proc.setStatusMessage("third", Severity::Error);
  auto* panel = rig.ed->statusBar().showLog();
  CHECK(panel != nullptr);
  if (!panel) return;
  pump(80);
  CHECK_EQ(panel->rowCount(), 3);
  CHECK(panel->rowText(0).endsWith("[x] third"));
  CHECK(panel->rowText(1).endsWith("[!] second"));
  CHECK(panel->rowText(2).endsWith("  first"));
  CHECK(panel->allText().contains("[x] third"));
  snapshotNow(*panel, "notice_log");
  panel = rig.ed->statusBar().showLog();
  CHECK(panel != nullptr);
  if (!panel) return;
  pump(40);
  panel->copyButton().triggerClick();
  pump(40);
  CHECK(juce::SystemClipboard::getTextFromClipboard().contains("[!] second"));
  panel->clearButton().triggerClick();
  pump(40);
  CHECK_EQ(panel->rowCount(), 0);
  CHECK_EQ(rig.proc.noticeLog().size(), 0u);
}

CHOP_TEST(drag_and_drop_highlights_then_loads_after_confirmation) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  const juce::File dir = user.root.getChildFile("drop");
  dir.createDirectory();
  const juce::File wav = dir.getChildFile("newloop.wav");
  {
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::OutputStream> stream = wav.createOutputStream();
    auto writer = fmt.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(44100.0).withNumChannels(1).withBitsPerSample(16));
    CHECK(writer != nullptr);
    if (!writer) return;
    juce::AudioBuffer<float> b(1, 22050);
    for (int i = 0; i < 22050; ++i) b.setSample(0, i, 0.4f * std::sin(0.03f * static_cast<float>(i)));
    writer->writeFromAudioSampleBuffer(b, 0, 22050);
  }
  CHECK(rig.ed->isInterestedInFileDrag({wav.getFullPathName()}));
  CHECK(!rig.ed->isInterestedInFileDrag({"a.txt"}));
  CHECK(rig.ed->isInterestedInFileDrag({"A.AIFF"}));
  rig.ed->fileDragEnter({wav.getFullPathName()}, 10, 10);
  pump(40);
  CHECK(rig.ed->wavePanel().isDropHighlighted());
  snapshot(*rig.ed, "drop_hover");
  rig.ed->fileDragExit({wav.getFullPathName()});
  pump(40);
  CHECK(!rig.ed->wavePanel().isDropHighlighted());
  rig.ed->fileDragEnter({"a.txt"}, 10, 10);
  CHECK(!rig.ed->wavePanel().isDropHighlighted());

  const auto before = patternBytes(rig.proc);
  bool answer = false;
  int asked = 0;
  rig.ed->commands().confirmReplace = [&](std::function<void(bool)> done) {
    ++asked;
    done(answer);
  };
  rig.ed->filesDropped({wav.getFullPathName()}, 5, 5);
  CHECK_EQ(asked, 1);
  CHECK(patternBytes(rig.proc) == before);  // Cancel: untouched
  answer = true;
  rig.ed->filesDropped({wav.getFullPathName(), wav.getFullPathName()}, 5, 5);
  CHECK_EQ(asked, 2);
  CHECK(waitFor([&] { return rig.proc.statusMessage().startsWith("Loaded newloop.wav"); }));
  bool twice = false;
  for (const auto& e : rig.proc.noticeLog().snapshot())
    if (e.text == "Only the first file was loaded.") twice = true;
  CHECK(twice);
  rig.proc.withSession([&](cf::composition::ProjectSession& s) { CHECK_EQ(s.history().size(), 0u); });
}

CHOP_TEST(status_bar_is_laid_out_at_both_sizes) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  for (auto size : {std::pair<int, int>{1100, 720}, std::pair<int, int>{880, 560}}) {
    EditorRig rig(true, size.first, size.second);
    if (!rig.ed) return;
    rig.proc.setStatusMessage("Exported loop-bounce.wav, 4 bars, 8.5 s. A very long message follows so that the line has to be elided at the end of its area", Severity::Warning);
    pump(200);
    auto& bar = rig.ed->statusBar();
    CHECK_EQ(bar.compact(), size.first < 900);
    CHECK(bar.syncBox().text().startsWith("120.0"));
    CHECK(bar.voiceBox().text().startsWith("V "));
    CHECK(bar.getBottom() <= rig.ed->getHeight());
    snapshot(*rig.ed, "statusbar");
  }
}

CHOP_TEST(macro_toggle_labels_are_not_truncated_at_any_size_or_scale) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  for (auto size : {std::pair<int, int>{1100, 720}, std::pair<int, int>{880, 560}, std::pair<int, int>{1500, 960}})
    for (int percent : {90, 100, 125}) {
      EditorRig rig(true, size.first, size.second);
      if (!rig.ed) return;
      rig.ed->setUiScalePercent(percent);
      pump(60);
      for (const char* label : {"Match", "Limit"}) {
        auto* b = findButton(*rig.ed, label);
        CHECK(b != nullptr);
        if (!b) continue;
        const float scale = cdl::uiScale();
        const int text = juce::GlyphArrangement::getStringWidthInt(cdl::font(cdl::FontRole::Body, scale), label);
        CHECK(b->getWidth() >= text + 14 + 10);  // box, gap and the whole word
      }
    }
  snapshot(*EditorRig(true, 880, 560).ed, "macro_toggles");
}

// F16: the safety limiter's gain reduction is shown as text on the meter, from the renderer's telemetry.
CHOP_TEST(meter_shows_the_limiter_gain_reduction_as_text) {
  if (!haveDisplay()) return;
  IsolatedUser user;
  EditorRig rig;
  if (!rig.ed) return;
  auto& meter = rig.ed->statusBar().meter();
  setParam(rig.proc, "dry_mix", 1.0f);
  setParam(rig.proc, "limiter_enable", 1.0f);
  setParam(rig.proc, "limiter_ceiling_db", -6.0f);
  loudBlocks(rig.proc, 40, 0.9f);  // well above -6 dBFS: the limiter has to work
  meter.tickOnce(0.033);
  CHECK(meter.state().grDb < -0.5f);
  CHECK(meter.getTooltip().contains("Limiter gain reduction") || meter.state().grDb < 0.f);
  CHECK(chopfractal::ui::grText(meter.state().grDb).rfind("GR ", 0) == 0);
  setParam(rig.proc, "limiter_enable", 0.0f);
  loudBlocks(rig.proc, 120, 0.9f);
  for (int i = 0; i < 40; ++i) meter.tickOnce(0.05);
  CHECK(meter.state().grDb > -0.05f);  // off: the readout falls back to nothing
}
