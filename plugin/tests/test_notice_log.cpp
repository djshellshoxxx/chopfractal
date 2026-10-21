// F05 notice log: ring of 100, duplicate collapse, clear, and the severity of the status line.
#include "shell_test_support.hpp"

using namespace shelltest;

CHOP_TEST(ring_keeps_the_newest_100_entries_and_bumps_the_revision) {
  NoticeLog log;
  const auto r0 = log.revision();
  for (int i = 0; i < 130; ++i) log.add(Severity::Info, "entry " + juce::String(i));
  CHECK_EQ(log.size(), 100u);
  const auto all = log.snapshot();
  CHECK(all.front().text == "entry 30");
  CHECK(all.back().text == "entry 129");
  CHECK(log.revision() > r0);
  LogEntry latest;
  CHECK(log.latest(latest) && latest.text == "entry 129");
  log.add(Severity::Info, "");  // empty text is never logged
  CHECK_EQ(log.size(), 100u);
}

CHOP_TEST(duplicates_within_two_seconds_collapse_with_a_counter) {
  NoticeLog log;
  juce::int64 now = 1000;
  log.setClock([&now] { return now; });
  log.add(Severity::Warning, "Rule 1 blocked a change");
  now += 500;
  log.add(Severity::Warning, "Rule 1 blocked a change");
  now += 500;
  log.add(Severity::Warning, "Rule 1 blocked a change");
  CHECK_EQ(log.size(), 1u);
  CHECK(log.snapshot().front().shown() == "Rule 1 blocked a change (x3)");
  now += 2500;  // past two seconds: a new entry
  log.add(Severity::Warning, "Rule 1 blocked a change");
  CHECK_EQ(log.size(), 2u);
  log.add(Severity::Error, "Rule 1 blocked a change");  // another severity is another entry
  CHECK_EQ(log.size(), 3u);
}

CHOP_TEST(clear_empties_and_bumps_the_revision) {
  NoticeLog log;
  log.add(Severity::Info, "a");
  const auto r = log.revision();
  log.clear();
  CHECK_EQ(log.size(), 0u);
  CHECK(log.revision() > r);
  LogEntry e;
  CHECK(!log.latest(e));
}

CHOP_TEST(status_messages_carry_a_severity_and_reach_the_log_but_progress_does_not) {
  ChopFractalProcessor p;
  p.setStatusMessage("fine");
  CHECK(p.statusSeverity() == Severity::Info);
  p.setStatusMessage("careful", Severity::Warning);
  CHECK(p.statusSeverity() == Severity::Warning);
  p.setStatusMessage("broken", Severity::Error);
  CHECK(p.statusSeverity() == Severity::Error && p.statusMessage() == "broken");
  const auto before = p.noticeLog().size();
  p.setProgressMessage("Exporting x: 10%");
  p.setProgressMessage("Exporting x: 20%");
  CHECK_EQ(p.noticeLog().size(), before);
  CHECK(p.statusSeverity() == Severity::Info);
  const auto all = p.noticeLog().snapshot();
  CHECK(all.size() >= 3u && all[all.size() - 1].text == "broken" && all[all.size() - 1].severity == Severity::Error);
}
