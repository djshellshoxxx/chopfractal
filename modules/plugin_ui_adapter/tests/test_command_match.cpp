#include <chopfractal/plugin_ui_adapter/access_text.hpp>
#include <chopfractal/plugin_ui_adapter/command_match.hpp>
#include <string_view>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::ui;

CHOP_TEST(subsequence_is_required_and_case_does_not_matter) {
  CHECK(fuzzyScore("gnr", "Generate new pattern").matched);
  CHECK(fuzzyScore("GENERATE", "generate new pattern").matched);
  CHECK(!fuzzyScore("xyz", "Generate").matched);
  CHECK(!fuzzyScore("etareneg", "Generate").matched);  // order matters
  const auto m = fuzzyScore("sw", "Swing");
  CHECK(m.matched && m.positions.size() == 2 && m.positions[0] == 0 && m.positions[1] == 1);
}

CHOP_TEST(prefix_equal_word_start_and_consecutive_bonuses_rank_as_expected) {
  CHECK(fuzzyScore("swing", "Swing").score > fuzzyScore("swing", "Swing amount").score);   // equals beats prefix
  CHECK(fuzzyScore("sw", "Swing amount").score > fuzzyScore("sw", "Song wing").score);    // prefix and word start beat scattered
  CHECK(fuzzyScore("amo", "Swing amount").score > fuzzyScore("amo", "A m o").score);      // consecutive
  CHECK(fuzzyScore("Gen", "Generate").score > fuzzyScore("gen", "Generate").score);       // exact case bonus
}

CHOP_TEST(multi_token_queries_use_and_semantics_in_any_order) {
  CHECK(fuzzyScore("new gen", "Generate new pattern").matched);
  CHECK(!fuzzyScore("new foo", "Generate new pattern").matched);
  CHECK(fuzzyScore("", "anything").matched && fuzzyScore("  ", "anything").score == 0);
}

CHOP_TEST(ranking_is_stable_deterministic_and_limited) {
  const std::vector<std::string_view> c = {"Song wing", "Swing", "Swing amount", "Reverse", "Export WAV", "Export Kit", "Export MIDI"};
  const auto a = fuzzyRank("swing", c, 8);
  CHECK(a.size() == 3u && a[0].first == 1 && a[1].first == 2 && a[2].first == 0);
  CHECK(a == fuzzyRank("swing", c, 8));
  const auto e = fuzzyRank("export", c, 2);
  CHECK_EQ(e.size(), 2u);
  CHECK(e[0].first == 4 && e[1].first == 5);  // equal scores and lengths keep the registry order
  CHECK(fuzzyRank("zzz", c, 8).empty());
  CHECK_EQ(fuzzyRank("", c, 100).size(), c.size());
}

CHOP_TEST(non_ascii_bytes_do_not_crash_and_positions_stay_in_bounds) {
  const std::string_view cand = "Caf\xC3\xA9 loop";
  const auto m = fuzzyScore("loop", cand);
  CHECK(m.matched);
  for (auto p : m.positions) CHECK(p < cand.size());
  CHECK(fuzzyScore("\xC3\xA9", cand).matched);
  CHECK(!fuzzyScore("\xFF", cand).matched);
}

CHOP_TEST(number_arguments_parse_clamp_or_report_the_range) {
  double v = 0;
  std::string err;
  CHECK(parseNumberArg("0.6", 0, 1, v, err) && v == 0.6);
  CHECK(parseNumberArg(" -3 ", -60, 12, v, err) && v == -3.0);
  CHECK(!parseNumberArg("2", 0, 1, v, err) && err == "0 to 1 expected.");
  CHECK(!parseNumberArg("abc", 0, 1, v, err) && err == "0 to 1 expected.");
  CHECK(!parseNumberArg("", 0, 1, v, err));
  CHECK(!parseNumberArg("nan", 0, 1, v, err));
  CHECK(parseNumberArg("5", 0, 1, v, err, true) && v == 1.0);
  CHECK(rangeErrorText(-60, 12) == "-60 to 12 expected.");
}

namespace {
EventRect rect(std::uint64_t id, double x0, int lane, int depth = 0) {
  EventRect r;
  r.id = EventId{id};
  r.x0 = x0;
  r.x1 = x0 + 0.05;
  r.chopIndex = lane;
  r.depth = depth;
  return r;
}
}  // namespace

CHOP_TEST(neighbor_hit_walks_time_order_and_lanes) {
  PatternView v;
  v.rects = {rect(1, 0.0, 0), rect(2, 0.25, 2), rect(3, 0.25, 0), rect(4, 0.5, 2), rect(5, 0.75, 0), rect(6, 0.3, 0, 1)};
  // time order by (x0, lane, id): 1, 3, 2, 4, 5 (the nested 6 is ignored)
  EventId cur;
  std::vector<std::uint64_t> seen;
  for (int i = 0; i < 5; ++i) {
    cur = neighborHit(v, cur, HitDirection::Next);
    seen.push_back(cur.value);
  }
  CHECK((seen == std::vector<std::uint64_t>{1, 3, 2, 4, 5}));
  CHECK(neighborHit(v, EventId{5}, HitDirection::Next) == EventId{5});      // no wrap
  CHECK(neighborHit(v, EventId{1}, HitDirection::Previous) == EventId{1});
  CHECK(neighborHit(v, EventId{}, HitDirection::Previous) == EventId{5});   // none selected: the last
  CHECK(neighborHit(v, EventId{999}, HitDirection::Next) == EventId{1});    // unknown counts as none
  // lanes: 0 and 2 have hits, lane 1 is empty and skipped
  CHECK(neighborHit(v, EventId{3}, HitDirection::LaneDown) == EventId{2});  // same time, lane 2
  CHECK(neighborHit(v, EventId{5}, HitDirection::LaneDown) == EventId{4});  // nearest in time in lane 2
  CHECK(neighborHit(v, EventId{4}, HitDirection::LaneUp) == EventId{3} || neighborHit(v, EventId{4}, HitDirection::LaneUp) == EventId{5});
  CHECK(neighborHit(v, EventId{1}, HitDirection::LaneUp) == EventId{1});    // nothing above
  PatternView empty;
  CHECK(neighborHit(empty, EventId{}, HitDirection::Next) == EventId{});
}


CHOP_TEST(access_texts_pluralize_and_cover_the_empty_states) {
  CHECK(describeWave(false, 0, 0) == "No audio loaded.");
  CHECK(describeWave(true, 1, 0) == "1 chop, 0 markers.");
  CHECK(describeWave(true, 8, 7) == "8 chops, 7 markers.");
  CHECK(describePattern(false, 0, 0, "") == "No pattern.");
  CHECK(describePattern(true, 1, 1, "") == "1 bar, 1 hit. No hit selected.");
  CHECK(describePattern(true, 4, 12, "Bar 2, beat 3") == "4 bars, 12 hits. Selected: Bar 2, beat 3");
  CHECK(describeHistory(5, "Root") == "5 variations. Active: Root.");
  CHECK(describeHistory(1, "") == "1 variation. Active: none.");
}
