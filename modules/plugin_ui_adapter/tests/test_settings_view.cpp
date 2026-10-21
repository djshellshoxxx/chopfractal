#include <chopfractal/plugin_ui_adapter/settings_view.hpp>

#include "chop_test.hpp"

using namespace chopfractal::ui;

CHOP_TEST(ui_scale_choices_map_to_the_nearest_index_and_clamp) {
  CHECK_EQ(uiScaleItems().size(), uiScalePercents().size());
  CHECK_EQ(uiScaleIndex(100), 1);
  CHECK_EQ(uiScaleIndex(105), 1);
  CHECK_EQ(uiScaleIndex(300), 3);
  CHECK_EQ(uiScaleIndex(10), 0);
  CHECK_EQ(uiScalePercentAt(-4), 90);
  CHECK_EQ(uiScalePercentAt(2), 110);
  CHECK_EQ(uiScalePercentAt(99), 125);
  CHECK(limiterCeilingText(-1.0) == "\xE2\x88\x92" "1.0 dB");
  CHECK(limiterCeilingText(0.0) == "0.0 dB");
}

CHOP_TEST(diagnostics_report_lists_the_state_and_keeps_paths_out) {
  DiagnosticsInput d;
  d.version = "0.2.1";
  d.format = "VST3";
  d.host = "Reaper";
  d.sampleRate = 48000;
  d.blockSize = 512;
  d.theme = "Night";
  d.windowWidth = 1100;
  d.windowHeight = 720;
  d.hasSource = true;
  d.sourceName = "amen.wav";
  d.sourceSeconds = 6.85;
  d.chops = 8;
  d.events = 16;
  d.bars = 2;
  d.bpm = 136.0;
  d.localTempo = false;
  d.notices = {"Warning: Export cancelled"};
  const std::string t = buildDiagnosticsText(d);
  CHECK(t.find("Version: 0.2.1") != std::string::npos);
  CHECK(t.find("VST3, host Reaper") != std::string::npos);
  CHECK(t.find("48000 Hz, block 512") != std::string::npos);
  CHECK(t.find("amen.wav, 6.85 s, 8 chops, 16 hits, 2 bars") != std::string::npos);
  CHECK(t.find("136.0 BPM (host), 4/4") != std::string::npos);
  CHECK(t.find("Warning: Export cancelled") != std::string::npos);
  CHECK(t.find("/home") == std::string::npos);
  d.hasSource = false;
  d.notices.clear();
  const std::string u = buildDiagnosticsText(d);
  CHECK(u.find("Source: none loaded") != std::string::npos && u.find("Notices: none") != std::string::npos);
}
