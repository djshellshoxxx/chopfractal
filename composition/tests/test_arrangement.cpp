// F18: the arrangement model (totals, tag 16 state, fill helpers) and the session list (rows, deleted scenes, messages).
#include <chopfractal/composition/project_session.hpp>

#include <initializer_list>

#include "session_fixture.hpp"

using namespace chopfractal;
using namespace chopfractal::composition;

namespace {

Arrangement make(std::initializer_list<std::pair<int, int>> barsRepeats) {
  Arrangement a;
  std::uint32_t id = 1;
  for (const auto& br : barsRepeats) {
    ArrangementSection s;
    s.sceneId = id++;
    s.bars = static_cast<std::uint8_t>(br.first);
    s.repeats = static_cast<std::uint8_t>(br.second);
    a.sections.push_back(s);
  }
  return a;
}

scenes::SceneList sceneListOf(int n, int bars = 2) {
  scenes::SceneList l;
  for (int i = 0; i < n; ++i) {
    scenes::Scene s;
    s.name = "Scene " + std::to_string(i + 1);
    s.bars = bars;
    s.loops = 2;
    s.pattern.assign(4, 0);
    CHECK(scenes::add(l, s).ok());
  }
  return l;
}

std::vector<scenes::SceneId> capture(ProjectSession& s, int n, int bars = 2) {
  std::vector<scenes::SceneId> ids;
  for (int i = 0; i < n; ++i) {
    CHECK(s.generate(fixture::settings(11 + static_cast<std::uint64_t>(i), bars)).ok());
    auto r = s.captureScene();
    CHECK(r.ok());
    if (r.ok()) ids.push_back(r.value());
  }
  return ids;
}

}  // namespace

CHOP_TEST(total_bars_and_validity_8_to_32) {
  struct Case {
    std::initializer_list<std::pair<int, int>> sections;
    int total;
    bool valid;
  };
  const Case cases[] = {{{{4, 1}, {2, 1}, {1, 1}}, 7, false},  {{{4, 1}, {4, 1}}, 8, true},           {{{8, 2}, {8, 2}}, 32, true},
                        {{{8, 2}, {8, 2}, {1, 1}}, 33, false}, {{{8, 1}, {4, 2}, {2, 4}}, 24, true}, {{{1, 8}}, 8, true}};
  for (const Case& c : cases) {
    const Arrangement a = make(c.sections);
    CHECK_EQ(a.totalBars(), c.total);
    CHECK_EQ(a.valid(), c.valid);
  }
  CHECK(!Arrangement{}.valid());
  Arrangement many;  // 33 sections of 1 bar: the section cap, not the bar total, decides
  for (int i = 0; i < 33; ++i) many.sections.push_back({1, 1, 1});
  CHECK(!many.valid());
  CHECK(!validateStored(many).ok());
}

CHOP_TEST(stored_arrangements_are_validated_field_by_field) {
  Arrangement a = make({{4, 2}});
  CHECK(validateStored(a).ok());
  a.sections[0].bars = 3;
  CHECK(!validateStored(a).ok());
  a.sections[0].bars = 4;
  a.sections[0].repeats = 0;
  CHECK(!validateStored(a).ok());
  a.sections[0].repeats = 9;
  CHECK(!validateStored(a).ok());
  a.sections[0].repeats = 2;
  a.transition = static_cast<Transition>(7);
  CHECK(!validateStored(a).ok());
}

CHOP_TEST(serialize_roundtrip_tag_16_and_absent_when_empty) {
  CHECK(serialize(Arrangement{}).empty());
  Arrangement a = make({{4, 2}, {2, 1}, {8, 1}});
  a.transition = Transition::Crossfade50;
  a.fadeOut = true;
  a.tail = false;
  a.writeMidi = false;
  a.writeMarkers = false;
  a.sections[1].sceneId = 77;
  const auto bytes = serialize(a);
  CHECK_EQ(bytes.size(), 4u + 3u * 6u);
  CHECK_EQ(bytes[0], 1);
  CHECK_EQ(bytes[1], 1);  // fadeOut only
  CHECK_EQ(bytes[2], 2);
  CHECK_EQ(bytes[3], 3);
  auto back = deserialize(bytes.data(), bytes.size());
  CHECK(back.ok() && back.value() == a);
  // options alone (no sections) are not the default and are kept
  Arrangement opt;
  opt.tail = false;
  CHECK(!serialize(opt).empty());
  auto optBack = deserialize(serialize(opt).data(), serialize(opt).size());
  CHECK(optBack.ok() && optBack.value() == opt);
  // corrupt input is refused, never partly read
  auto bad = bytes;
  bad[0] = 2;
  CHECK(!deserialize(bad.data(), bad.size()).ok());
  bad = bytes;
  bad[3] = 4;  // count says four sections, bytes hold three
  CHECK(!deserialize(bad.data(), bad.size()).ok());
  bad = bytes;
  bad[1] = 0x10;
  CHECK(!deserialize(bad.data(), bad.size()).ok());
  bad = bytes;
  bad.resize(bad.size() - 1);
  CHECK(!deserialize(bad.data(), bad.size()).ok());
  bad = bytes;
  bad[8] = 3;  // bars of the first section
  CHECK(!deserialize(bad.data(), bad.size()).ok());
}

CHOP_TEST(session_stores_section_16_only_when_not_default_and_restores_it) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 2));
  const auto ids = capture(s, 2);
  CHECK(s.section(kTagArrangement) == nullptr);
  const auto baseline = s.saveState(false);
  CHECK(baseline.ok());
  Arrangement a = make({{4, 1}, {4, 1}});
  a.sections[0].sceneId = ids[0];
  a.sections[1].sceneId = ids[1];
  a.transition = Transition::Cut;
  CHECK(s.setArrangement(a).ok());
  CHECK(s.section(kTagArrangement) != nullptr && s.section(kTagArrangement)->schema == kArrangementSchema);
  CHECK(s.arrangement() == a);
  auto saved = s.saveState(true);
  CHECK(saved.ok());
  ProjectSession r;
  CHECK(r.loadState(saved.value().data(), saved.value().size(), nullptr).ok());
  CHECK(r.arrangement() == a);
  // back to the default: the section disappears and so does the extra block
  CHECK(s.setArrangement(Arrangement{}).ok());
  CHECK(s.section(kTagArrangement) == nullptr);
  const auto again = s.saveState(false);
  CHECK(again.ok() && again.value() == baseline.value());
}

CHOP_TEST(a_bad_section_16_drops_only_itself_with_the_warning) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 2));
  const auto ids = capture(s, 1);
  Arrangement a = make({{4, 1}, {4, 1}});
  a.sections[0].sceneId = ids[0];
  a.sections[1].sceneId = ids[0];
  CHECK(s.setArrangement(a).ok());
  StateSection junk;
  junk.tag = kTagArrangement;
  junk.schema = kArrangementSchema;
  junk.bytes = {9, 9, 9};
  CHECK(s.setSection(junk).ok());  // the section API stores it; the decoder refuses it on load
  auto saved = s.saveState(true);
  CHECK(saved.ok());
  ProjectSession r;
  CHECK(r.loadState(saved.value().data(), saved.value().size(), nullptr).ok());
  CHECK(r.arrangement().sections.empty());
  CHECK(r.section(kTagArrangement) == nullptr);
  bool warned = false;
  for (const Notice& n : r.takeNotices()) warned = warned || n.text == "Some arrangement settings could not be restored.";
  CHECK(warned);
  CHECK(!r.sceneList().scenes.empty());  // the scenes section is untouched
}

CHOP_TEST(deleted_scene_is_flagged_not_dropped) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 2));
  const auto ids = capture(s, 3);
  Arrangement a = make({{4, 1}, {4, 1}, {4, 1}});
  for (std::size_t i = 0; i < 3; ++i) a.sections[i].sceneId = ids[i];
  CHECK(s.setArrangement(a).ok());
  auto rows = s.arrangementRows();
  CHECK_EQ(rows.size(), 3u);
  CHECK(!rows[1].deleted && rows[1].sceneNumber == 2 && rows[1].sceneName == "Scene 2" && rows[1].patternBars == 2 && rows[1].fits);
  CHECK(s.removeScene(ids[1]).ok());
  rows = s.arrangementRows();
  CHECK_EQ(rows.size(), 3u);  // kept
  CHECK(rows[1].deleted && rows[1].sceneNumber == 0 && rows[1].sceneName.empty() && !rows[1].fits);
  CHECK(rows[2].sceneNumber == 2);  // numbers close the gap
  CHECK_EQ(s.arrangement().sections.size(), 3u);
  // survives save and load, still flagged
  auto saved = s.saveState(true);
  CHECK(saved.ok());
  ProjectSession r;
  CHECK(r.loadState(saved.value().data(), saved.value().size(), nullptr).ok());
  CHECK_EQ(r.arrangementRows().size(), 3u);
  CHECK(r.arrangementRows()[1].deleted);
  // and the export is blocked with a message that names the section
  ArrangementExportSettings st;
  st.root = fixture::tmpDir("chop_arr", "deleted");
  auto started = s.startArrangementExport(s.arrangement(), "x", st);
  CHECK(!started.ok());
  CHECK(started.error().message == "Section 2 uses a scene that was deleted.");
  CHECK(s.arrangementJob() == nullptr);
}

CHOP_TEST(a_new_source_or_a_loaded_project_never_keeps_a_stale_arrangement) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 2));
  const auto ids = capture(s, 1);
  Arrangement a = make({{4, 2}});
  a.sections[0].sceneId = ids[0];
  CHECK(s.setArrangement(a).ok());
  CHECK(s.loadSource(fixture::loopAudio(), "other", "/music/other.wav").ok());
  CHECK(s.arrangement().sections.empty() && s.section(kTagArrangement) == nullptr);
  CHECK(s.sceneList().scenes.empty());
}

CHOP_TEST(auto_fill_and_use_chain_trim_to_32) {
  // auto fill: round-robin, about 4 bars each, 16 bars in total
  const scenes::SceneList three = sceneListOf(3);
  Arrangement f = autoFill16(three);
  CHECK_EQ(f.totalBars(), 16);
  CHECK_EQ(f.sections.size(), 4u);
  CHECK(f.sections[0].sceneId == three.scenes[0].id && f.sections[3].sceneId == three.scenes[0].id);  // round-robin wraps
  CHECK(f.sections[1].sceneId == three.scenes[1].id && f.sections[2].sceneId == three.scenes[2].id);
  CHECK(f.valid());
  CHECK(autoFill16(scenes::SceneList{}).sections.empty());
  Arrangement keep;
  keep.transition = Transition::Cut;
  keep.fadeOut = true;
  CHECK(autoFill16(three, keep).transition == Transition::Cut && autoFill16(three, keep).fadeOut);
  // a pattern that does not divide into the choices still gets a valid length
  CHECK(defaultBars(2) == 4 && defaultBars(1) == 4 && defaultBars(4) == 4 && defaultBars(8) == 8 && defaultBars(3) == 4 && defaultBars(16) == 4);
  CHECK((allowedBars(2) == std::vector<int>{2, 4, 8}) && (allowedBars(1) == std::vector<int>{1, 2, 4, 8}) && (allowedBars(4) == std::vector<int>{4, 8}));
  CHECK((allowedBars(3) == std::vector<int>{1, 2, 4, 8}));
  CHECK(truncationNote(2, 4, 2) == "Scene 2 has 4 bars; using the first 2" && truncationNote(2, 2, 4).empty() && truncationNote(2, 4, 4).empty());

  // chain: loops become repeats, the total is trimmed to 32 bars
  scenes::SceneList chain = sceneListOf(6, 4);
  for (scenes::Scene& s : chain.scenes) s.loops = 8;  // each wants 4 x 8 = 32 bars
  Arrangement c = fromSceneChain(chain);
  CHECK(c.totalBars() <= 32 && c.valid());
  CHECK_EQ(c.sections.size(), 1u);
  CHECK_EQ(static_cast<int>(c.sections[0].repeats), 8);
  chain = sceneListOf(6, 4);
  for (scenes::Scene& s : chain.scenes) s.loops = 3;  // 12 bars each: two fit, the third is trimmed to 8 and stops the chain
  c = fromSceneChain(chain);
  CHECK_EQ(c.totalBars(), 32);
  CHECK(c.sections.size() == 3u && c.sections[2].repeats == 2);
  // Stop / Repeat end the chain; Jump follows its target once
  chain = sceneListOf(4, 4);
  chain.scenes[1].next = scenes::Next::Stop;
  c = fromSceneChain(chain);
  CHECK_EQ(c.sections.size(), 2u);
  chain = sceneListOf(4, 4);
  chain.scenes[1].next = scenes::Next::Jump;
  chain.scenes[1].jumpTo = chain.scenes[3].id;
  chain.scenes[3].next = scenes::Next::Jump;
  chain.scenes[3].jumpTo = chain.scenes[0].id;  // loops back: each scene once
  c = fromSceneChain(chain);
  CHECK(c.sections.size() == 3u && c.sections[0].sceneId == chain.scenes[0].id && c.sections[1].sceneId == chain.scenes[1].id &&
        c.sections[2].sceneId == chain.scenes[3].id);
}

CHOP_TEST(session_fill_commands_store_the_result_and_need_scenes) {
  ProjectSession s;
  CHECK(fixture::ready(s, true, 5, 2));
  CHECK(!s.autoFillArrangement().ok());
  CHECK(!s.fillArrangementFromChain().ok());
  capture(s, 3);
  CHECK(s.autoFillArrangement().ok());
  CHECK_EQ(s.arrangement().totalBars(), 16);
  CHECK(s.section(kTagArrangement) != nullptr);
  CHECK(s.fillArrangementFromChain().ok());
  CHECK(s.arrangement().valid());
}

CHOP_TEST(starting_an_export_validates_in_the_documented_order) {
  ProjectSession s;
  ArrangementExportSettings st;
  st.root = fixture::tmpDir("chop_arr", "validate");
  Arrangement six = make({{4, 1}, {2, 1}});
  auto r = s.startArrangementExport(six, "x", st);
  CHECK(!r.ok() && r.error().message == "Arrangement needs 8 to 32 bars (now 6).");
  CHECK(fixture::ready(s, true, 5, 2));
  const auto ids = capture(s, 2);
  Arrangement ok = make({{4, 1}, {4, 1}});
  ok.sections[0].sceneId = ids[0];
  ok.sections[1].sceneId = ids[1];
  ArrangementExportSettings bad = st;
  bad.bpm = 5;
  CHECK(!s.startArrangementExport(ok, "x", bad).ok());
  bad = st;
  bad.root.clear();
  CHECK(!s.startArrangementExport(ok, "x", bad).ok());
  bad = st;
  bad.bpm = 20;
  bad.wavOptions.sampleRate = 96000;
  Arrangement longOne = ok;
  longOne.sections = {{ids[0], 8, 2}, {ids[1], 8, 2}};  // 32 bars at 20 BPM is 384 s
  auto tooLong = s.startArrangementExport(longOne, "x", bad);
  CHECK(!tooLong.ok() && tooLong.error().message == "Arrangement is too long for this sample rate.");
  CHECK(s.arrangementJob() == nullptr);
}
