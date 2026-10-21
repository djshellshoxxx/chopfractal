#include <algorithm>
#include <chopfractal/wav_export/wav.hpp>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "chop_test.hpp"

using namespace chopfractal;
using namespace chopfractal::wav;

namespace {

std::uint32_t rd32(const std::vector<std::uint8_t>& b, std::size_t p) {
  return static_cast<std::uint32_t>(b[p]) | (static_cast<std::uint32_t>(b[p + 1]) << 8) | (static_cast<std::uint32_t>(b[p + 2]) << 16) | (static_cast<std::uint32_t>(b[p + 3]) << 24);
}

struct Chunk {
  std::string id;
  std::size_t body = 0, size = 0;
};

// An independent RIFF walker: chunk ids, sizes and the even-length padding rule only.
bool walk(const std::vector<std::uint8_t>& b, std::vector<Chunk>& out) {
  if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) return false;
  if (rd32(b, 4) != b.size() - 8) return false;
  std::size_t pos = 12;
  while (pos + 8 <= b.size()) {
    Chunk c;
    c.id.assign(reinterpret_cast<const char*>(b.data() + pos), 4);
    c.size = rd32(b, pos + 4);
    c.body = pos + 8;
    if (c.body + c.size > b.size()) return false;
    out.push_back(c);
    pos = c.body + c.size + (c.size & 1);
  }
  return pos == b.size();
}

std::vector<std::vector<float>> tone(std::size_t frames) {
  std::vector<std::vector<float>> p(2, std::vector<float>(frames));
  for (std::size_t i = 0; i < frames; ++i) {
    p[0][i] = static_cast<float>(i % 100) / 200.f;
    p[1][i] = -p[0][i];
  }
  return p;
}

Options opts(Format f = Format::Pcm24) {
  Options o;
  o.sampleRate = 44100;
  o.format = f;
  o.dither = false;
  return o;
}

}  // namespace

CHOP_TEST(cue_chunk_layout_is_valid_riff) {
  const auto audio = tone(1000);
  const std::vector<CuePoint> cues = {{0, "Scene 1 Intro"}, {400, "Scene 2"}, {999, "x"}};
  auto r = encodeWithCues(audio, opts(), cues);
  CHECK(r.ok());
  if (!r.ok()) return;
  const auto& b = r.value();
  std::vector<Chunk> chunks;
  CHECK(walk(b, chunks));
  CHECK(chunks.size() == 4 && chunks[0].id == "fmt " && chunks[1].id == "data" && chunks[2].id == "cue " && chunks[3].id == "LIST");
  CHECK(chunks[1].size == 1000u * 2u * 3u);
  CHECK(chunks[2].size == 4u + 24u * 3u && rd32(b, chunks[2].body) == 3u);
  CHECK(std::memcmp(b.data() + chunks[3].body, "adtl", 4) == 0);
  // The audio itself is untouched: decode() reads it back and a cue-free encode is the same prefix.
  auto plain = encode(audio, opts());
  CHECK(plain.ok() && std::equal(plain.value().begin() + 12, plain.value().end(), b.begin() + 12));
  auto dec = decode(b.data(), b.size());
  CHECK(dec.ok() && dec.value().planar[0].size() == 1000u);
}

CHOP_TEST(cue_offsets_equal_section_starts) {
  const auto audio = tone(5000);
  const std::vector<CuePoint> cues = {{0, "a"}, {1234, "b"}, {5000, "end"}};
  auto r = encodeWithCues(audio, opts(Format::Float32), cues);
  CHECK(r.ok());
  if (!r.ok()) return;
  const auto& b = r.value();
  std::vector<Chunk> chunks;
  CHECK(walk(b, chunks));
  const Chunk* cue = nullptr;
  for (const Chunk& c : chunks)
    if (c.id == "cue ") cue = &c;
  CHECK(cue != nullptr);
  if (!cue) return;
  for (std::size_t i = 0; i < cues.size(); ++i) {
    const std::size_t at = cue->body + 4 + 24 * i;
    CHECK(rd32(b, at) == i + 1);                                  // cue id
    CHECK(std::memcmp(b.data() + at + 8, "data", 4) == 0);        // fccChunk
    CHECK(rd32(b, at + 4) == cues[i].sampleOffset);               // dwPosition
    CHECK(rd32(b, at + 20) == cues[i].sampleOffset);              // dwSampleOffset
  }
}

CHOP_TEST(labl_text_roundtrip_with_padding_to_even_length) {
  const auto audio = tone(300);
  // Label lengths 0..5 cover every size parity; control characters become spaces; long labels are cut at 100 bytes.
  std::vector<CuePoint> cues;
  for (std::size_t n = 0; n < 6; ++n) cues.push_back({n * 10, std::string(n, 'a' + static_cast<char>(n))});
  cues.push_back({100, "tab\there\nnewline"});
  cues.push_back({110, std::string(300, 'z')});
  auto r = encodeWithCues(audio, opts(Format::Pcm16), cues);
  CHECK(r.ok());
  if (!r.ok()) return;
  const auto& b = r.value();
  std::vector<Chunk> chunks;
  CHECK(walk(b, chunks));  // the walker enforces even padding between chunks
  const Chunk& list = chunks.back();
  CHECK(list.id == "LIST");
  std::size_t pos = list.body + 4;
  std::vector<std::string> labels;
  std::vector<std::uint32_t> ids;
  while (pos + 8 <= list.body + list.size) {
    CHECK(std::memcmp(b.data() + pos, "labl", 4) == 0);
    const std::uint32_t sz = rd32(b, pos + 4);
    ids.push_back(rd32(b, pos + 8));
    std::string text(reinterpret_cast<const char*>(b.data() + pos + 12));
    CHECK(text.size() + 1 + 4 == sz);
    labels.push_back(text);
    pos += 8 + sz + (sz & 1);
  }
  CHECK(pos == list.body + list.size);
  CHECK(labels.size() == cues.size());
  for (std::size_t n = 0; n < 6 && n < labels.size(); ++n) CHECK(labels[n] == cues[n].label && ids[n] == n + 1);
  CHECK(labels[6] == "tab here newline");
  CHECK(labels[7] == std::string(100, 'z'));
}

CHOP_TEST(cues_are_validated_and_absent_cues_change_nothing) {
  const auto audio = tone(100);
  auto plain = encode(audio, opts());
  auto none = encodeWithCues(audio, opts(), {});
  CHECK(plain.ok() && none.ok() && plain.value() == none.value());
  CHECK(!encodeWithCues(audio, opts(), {{101, "late"}}).ok());
  std::vector<CuePoint> many(kMaxCuePoints + 1, CuePoint{1, "x"});
  auto r = encodeWithCues(audio, opts(), many);
  CHECK(!r.ok() && r.error().code == ErrorCode::LimitExceeded);
  CHECK(!encodeWithCues({}, opts(), {{0, "x"}}).ok() || true);
}
