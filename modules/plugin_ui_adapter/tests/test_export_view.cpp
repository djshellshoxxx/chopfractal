#include <chopfractal/plugin_ui_adapter/export_view.hpp>

#include "chop_test.hpp"

using namespace chopfractal::ui;

CHOP_TEST(export_choices_map_to_rates_loops_and_word_sizes) {
  CHECK_EQ(exportFormatItems().size(), 3u);
  CHECK_EQ(exportRateHz(0), 44100);
  CHECK_EQ(exportRateHz(3), 96000);
  CHECK_EQ(exportRateHz(9), 96000);
  CHECK_EQ(exportRateHz(-2), 44100);
  CHECK_EQ(exportLoopCount(4), 16);
  CHECK_EQ(exportBitsPerSample(2), 32);
  CHECK_EQ(exportRateItems().size(), 4u);
  CHECK_EQ(exportLoopItems().size(), 5u);
}

CHOP_TEST(export_estimate_text_matches_the_arithmetic) {
  CHECK_NEAR(exportSeconds(2, 4, 120.0, 1), 4.5, 1e-9);   // 2 bars of 2 s plus the tail
  CHECK_NEAR(exportSeconds(4, 4, 60.0, 4), 64.5, 1e-9);
  CHECK(exportSeconds(0, 4, 120.0, 1) == 0.0 && exportSeconds(2, 4, 0.0, 1) == 0.0);
  CHECK(exportEstimateText(4.5, 44100, 16) == "About 4.5 s, 775.2 KB");
  CHECK(exportEstimateText(64.5, 96000, 32) == "About 64.5 s, 47.2 MB");
  CHECK(exportEstimateText(0.0, 44100, 16).empty());
}

CHOP_TEST(export_view_disables_with_a_reason) {
  ExportViewInput in;
  ExportView v = buildExportView(in);
  CHECK(!v.wavEnabled && !v.kitEnabled && !v.dragEnabled && v.wavTooltip == "Load a loop first" && v.estimate.empty());
  in.hasSource = true;
  CHECK(buildExportView(in).wavTooltip == "Generate a pattern first");
  in.hasPattern = true;
  v = buildExportView(in);
  CHECK(v.wavEnabled && v.kitEnabled && v.dragEnabled && v.formatEnabled && !v.cancelVisible && !v.estimate.empty());
  in.sourceMissing = true;
  v = buildExportView(in);
  CHECK(!v.wavEnabled && !v.dragEnabled && v.wavTooltip == "Locate the source audio first");
  in.sourceMissing = false;
  in.exporting = true;
  v = buildExportView(in);
  CHECK(!v.wavEnabled && !v.kitEnabled && v.dragEnabled && !v.formatEnabled && v.cancelVisible && v.wavTooltip == "An export is already running");
  in.exporting = false;
  in.arrangementRunning = true;
  CHECK(buildExportView(in).cancelVisible);
}
