#include <chopfractal/plugin_ui_adapter/status_text.hpp>
#include <cmath>
#include <limits>

#include "chop_test.hpp"

using namespace chopfractal::ui;

CHOP_TEST(describe_sync_covers_every_combination) {
  CHECK(describeSync(120.0, 4, 4, false, false, true, false) == "120.0 BPM  4/4  Playing");
  CHECK(describeSync(98.0, 4, 4, true, true, false, false) == "98.0 BPM (manual)  4/4 (assumed)  Stopped");
  CHECK(describeSync(98.0, 3, 4, true, false, true, false) == "98.0 BPM (manual)  3/4  Playing");
  CHECK(describeSync(133.26, 7, 8, false, true, false, false) == "133.3 BPM  7/8 (assumed)  Stopped");
  CHECK(describeSync(120.0, 4, 4, false, false, true, true) == "120.0 | 4/4 | Playing");
  CHECK(describeSync(98.0, 4, 4, true, true, false, true) == "98.0 | 4/4 | Stopped");
  CHECK(syncTooltip(98.0, true, false) == "No host tempo: using 98.0 BPM");
  CHECK(syncTooltip(98.0, false, true) == "Assuming 4/4");
  CHECK(syncTooltip(98.0, true, true) == "No host tempo: using 98.0 BPM, Assuming 4/4");
  CHECK(syncTooltip(120.0, false, false).find("Following the host") == 0);
}

CHOP_TEST(severity_prefix_values) {
  CHECK(severityPrefix(0).empty());
  CHECK(severityPrefix(1) == "[!] ");
  CHECK(severityPrefix(2) == "[x] ");
  CHECK(severityName(0) == "Info" && severityName(1) == "Warning" && severityName(2) == "Error");
}

CHOP_TEST(peak_to_db_and_fraction) {
  CHECK_NEAR(peakToDb(0.5f), -6.0206, 1e-3);
  CHECK_NEAR(peakToDb(0.f), -60.0, 0.0);
  CHECK_NEAR(peakToDb(2.0f), 6.0, 0.03);
  CHECK_NEAR(peakToDb(100.f), 6.0, 0.0);
  CHECK_NEAR(peakToDb(std::numeric_limits<float>::quiet_NaN()), -60.0, 0.0);
  CHECK_NEAR(dbToFraction(-60.f), 0.0, 0.0);
  CHECK_NEAR(dbToFraction(6.f), 1.0, 0.0);
  CHECK_NEAR(dbToFraction(0.f), 60.0 / 66.0, 1e-6);
  CHECK_NEAR(dbToFraction(-90.f), 0.0, 0.0);
  CHECK(dbText(-12.34f) == "\xE2\x88\x92" "12.3 dB");
  CHECK(dbText(0.0f) == "0.0 dB");
  CHECK(dbText(-60.f) == "-inf dB");
  CHECK(voiceText(3, 8) == "V 3/8");
}

CHOP_TEST(meter_ballistics_attack_release_hold_and_latch) {
  MeterState s;
  MeterInput in;
  in.peak[0] = in.peak[1] = 1.0f;  // 0 dB
  CHECK(updateMeter(s, in, 0.033));
  CHECK_NEAR(s.db[0], 0.0, 0.01);  // instant attack
  in.peak[0] = in.peak[1] = 0.f;
  for (int i = 0; i < 30; ++i) updateMeter(s, in, 1.0 / 30.0);  // 1.0 s of silence
  CHECK_NEAR(s.db[0], -20.0, 0.5);
  CHECK_NEAR(s.holdDb[0], 0.0, 0.01);  // the hold line stays for 1.5 s
  for (int i = 0; i < 18; ++i) updateMeter(s, in, 1.0 / 30.0);  // 1.6 s in total
  CHECK(s.holdDb[0] < 0.0f);
  // the latch stays until the owner clears it
  in.clipSamples = 3;
  CHECK(updateMeter(s, in, 0.033));
  CHECK(s.clipLatched);
  in.clipSamples = 0;
  updateMeter(s, in, 0.033);
  CHECK(s.clipLatched);
  s.clipLatched = false;
  // once decayed to the floor, silence reports no visible change
  for (int i = 0; i < 400; ++i) updateMeter(s, in, 1.0 / 30.0);
  int changes = 0;
  for (int i = 0; i < 100; ++i) changes += updateMeter(s, in, 1.0 / 30.0) ? 1 : 0;
  CHECK_EQ(changes, 0);
  // reduced motion drops the easing
  MeterState r;
  in.peak[0] = in.peak[1] = 1.0f;
  updateMeter(r, in, 0.033, true);
  in.peak[0] = in.peak[1] = 0.1f;
  updateMeter(r, in, 0.033, true);
  CHECK_NEAR(r.db[0], -20.0, 0.01);
  CHECK(meterTooltip(r).find("CLIP lights when the output exceeds 0 dBFS") != std::string::npos);
}

CHOP_TEST(meter_shows_limiter_gain_reduction_as_text_with_instant_attack_and_a_20_db_per_second_release) {
  MeterState s;
  MeterInput in;
  CHECK(grText(s.grDb).empty());
  CHECK(!updateMeter(s, in, 0.033));  // nothing moved
  in.grDb = -4.0f;
  CHECK(updateMeter(s, in, 0.033));
  CHECK_NEAR(s.grDb, -4.0, 1e-4);  // instant attack
  CHECK(grText(s.grDb) == "GR " + dbText(-4.0f));
  CHECK(meterTooltip(s).find("Limiter gain reduction") != std::string::npos);
  in.grDb = 0.f;
  for (int i = 0; i < 6; ++i) updateMeter(s, in, 1.0 / 30.0);  // 0.2 s: 4 dB of release
  CHECK_NEAR(s.grDb, 0.0, 1e-4);
  CHECK(grText(s.grDb).empty());
  CHECK(meterTooltip(s).find("Limiter") == std::string::npos);
  in.grDb = -6.0f;
  updateMeter(s, in, 0.033);
  in.grDb = 0.f;
  updateMeter(s, in, 0.033, true);  // reduced motion: no easing
  CHECK_NEAR(s.grDb, 0.0, 1e-4);
  in.grDb = std::numeric_limits<float>::quiet_NaN();
  updateMeter(s, in, 0.033);
  CHECK_NEAR(s.grDb, 0.0, 1e-4);  // NaN never shows as a reduction
  in.grDb = 3.0f;  // positive is not a reduction
  updateMeter(s, in, 0.033);
  CHECK_NEAR(s.grDb, 0.0, 1e-4);
}
