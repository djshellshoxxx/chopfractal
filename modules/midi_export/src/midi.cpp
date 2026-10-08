#include <chopfractal/midi_export/midi.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

namespace chopfractal::midi {
namespace {

void be16(std::vector<std::uint8_t>& o, std::uint32_t v) {
  o.push_back(static_cast<std::uint8_t>(v >> 8));
  o.push_back(static_cast<std::uint8_t>(v));
}
void be32(std::vector<std::uint8_t>& o, std::uint32_t v) {
  for (int i = 3; i >= 0; --i) o.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void vlq(std::vector<std::uint8_t>& o, std::uint32_t v) {
  std::uint8_t b[5];
  int n = 0;
  b[n++] = v & 0x7F;
  while ((v >>= 7) != 0) b[n++] = static_cast<std::uint8_t>((v & 0x7F) | 0x80);
  while (n > 0) o.push_back(b[--n]);
}

struct Ev {
  Ticks tick;
  int order;  // 0 = meta, 1 = note off, 2 = note on: offs precede ons at the same tick
  int note;
  int vel;
  int channel;
};

int log2Den(int d) {
  switch (d) {
    case 1: return 0;
    case 2: return 1;
    case 4: return 2;
    case 8: return 3;
    case 16: return 4;
    case 32: return 5;
    default: return -1;
  }
}

}  // namespace

Result<std::vector<std::uint8_t>> encode(const FileSpec& spec) {
  if (!(spec.bpm >= 20.0 && spec.bpm <= 999.0)) return makeError(ErrorCode::OutOfRange, "tempo out of range");
  const int dd = log2Den(spec.denominator);
  if (dd < 0 || spec.numerator < 1 || spec.numerator > 32) return makeError(ErrorCode::OutOfRange, "invalid time signature");
  if (spec.notes.size() > kMaxNotes) return makeError(ErrorCode::LimitExceeded, "too many notes", static_cast<std::int64_t>(kMaxNotes));
  for (const Note& n : spec.notes) {
    if (n.note < 0 || n.note > 127 || n.velocity < 1 || n.velocity > 127 || n.channel < 0 || n.channel > 15)
      return makeError(ErrorCode::OutOfRange, "note, velocity or channel out of range");
    if (n.start < 0 || n.duration <= 0 || n.start > 0x0FFFFFFF || n.duration > 0x0FFFFFFF) return makeError(ErrorCode::OutOfRange, "invalid note timing");
  }
  std::vector<Note> notes = spec.notes;
  std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
    if (a.start != b.start) return a.start < b.start;
    if (a.channel != b.channel) return a.channel < b.channel;
    return a.note < b.note;
  });
  // Clip same-pitch overlaps (a later note on the same pitch cuts the earlier one).
  std::vector<Ticks> end(notes.size());
  for (std::size_t i = 0; i < notes.size(); ++i) end[i] = notes[i].start + notes[i].duration;
  std::map<std::pair<int, int>, std::size_t> last;
  for (std::size_t i = 0; i < notes.size(); ++i) {
    const auto key = std::make_pair(notes[i].channel, notes[i].note);
    auto it = last.find(key);
    if (it != last.end()) end[it->second] = std::min(end[it->second], notes[i].start);
    last[key] = i;
  }
  std::vector<Ev> evs;
  for (std::size_t i = 0; i < notes.size(); ++i) {
    if (end[i] <= notes[i].start) continue;
    evs.push_back({notes[i].start, 2, notes[i].note, notes[i].velocity, notes[i].channel});
    evs.push_back({end[i], 1, notes[i].note, 0, notes[i].channel});
  }
  std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) {
    if (a.tick != b.tick) return a.tick < b.tick;
    if (a.order != b.order) return a.order < b.order;
    if (a.channel != b.channel) return a.channel < b.channel;
    return a.note < b.note;
  });

  std::vector<std::uint8_t> trk;
  if (!spec.trackName.empty()) {
    trk.insert(trk.end(), {0x00, 0xFF, 0x03});
    const std::string name = spec.trackName.substr(0, 100);
    vlq(trk, static_cast<std::uint32_t>(name.size()));
    trk.insert(trk.end(), name.begin(), name.end());
  }
  const std::uint32_t mpq = static_cast<std::uint32_t>(std::llround(60000000.0 / spec.bpm));
  trk.insert(trk.end(), {0x00, 0xFF, 0x51, 0x03, static_cast<std::uint8_t>(mpq >> 16), static_cast<std::uint8_t>(mpq >> 8), static_cast<std::uint8_t>(mpq)});
  trk.insert(trk.end(), {0x00, 0xFF, 0x58, 0x04, static_cast<std::uint8_t>(spec.numerator), static_cast<std::uint8_t>(dd), 24, 8});
  Ticks now = 0;
  for (const Ev& e : evs) {
    vlq(trk, static_cast<std::uint32_t>(e.tick - now));
    now = e.tick;
    trk.push_back(static_cast<std::uint8_t>((e.order == 2 ? 0x90 : 0x80) | e.channel));
    trk.push_back(static_cast<std::uint8_t>(e.note));
    trk.push_back(static_cast<std::uint8_t>(e.order == 2 ? e.vel : 0));
  }
  trk.insert(trk.end(), {0x00, 0xFF, 0x2F, 0x00});

  std::vector<std::uint8_t> out{'M', 'T', 'h', 'd'};
  be32(out, 6);
  be16(out, 0);
  be16(out, 1);
  be16(out, static_cast<std::uint32_t>(kTicksPerQuarter));
  out.insert(out.end(), {'M', 'T', 'r', 'k'});
  be32(out, static_cast<std::uint32_t>(trk.size()));
  out.insert(out.end(), trk.begin(), trk.end());
  return out;
}

Result<FileSpec> decode(const std::uint8_t* d, std::size_t n) {
  auto bad = [](const char* m) { return makeError(ErrorCode::Corrupt, m); };
  if (!d || n < 22 || std::memcmp(d, "MThd", 4) != 0) return bad("not a MIDI file");
  auto r16 = [&](std::size_t p) { return static_cast<std::uint32_t>((d[p] << 8) | d[p + 1]); };
  auto r32 = [&](std::size_t p) { return (r16(p) << 16) | r16(p + 2); };
  if (r32(4) != 6 || r16(8) != 0 || r16(10) != 1) return makeError(ErrorCode::UnsupportedVersion, "only single-track format 0 files are supported");
  if (r16(12) != static_cast<std::uint32_t>(kTicksPerQuarter)) return makeError(ErrorCode::UnsupportedVersion, "unsupported tick resolution");
  if (std::memcmp(d + 14, "MTrk", 4) != 0) return bad("missing track");
  const std::size_t len = r32(18);
  if (len > n - 22) return bad("track runs past the end of the file");
  std::size_t p = 22;
  const std::size_t endp = 22 + len;
  FileSpec spec;
  Ticks now = 0;
  std::uint8_t running = 0;
  std::map<std::pair<int, int>, std::pair<Ticks, int>> open;
  bool ended = false;
  auto readVlq = [&](std::uint32_t& v) {
    v = 0;
    for (int i = 0; i < 4; ++i) {
      if (p >= endp) return false;
      const std::uint8_t b = d[p++];
      v = (v << 7) | (b & 0x7F);
      if (!(b & 0x80)) return true;
    }
    return false;
  };
  while (p < endp && !ended) {
    std::uint32_t delta;
    if (!readVlq(delta)) return bad("bad delta time");
    now += delta;
    if (p >= endp) return bad("truncated event");
    std::uint8_t st = d[p];
    if (st & 0x80) {
      ++p;
      if (st < 0xF0) running = st;
    } else {
      if (!running) return bad("data byte without status");
      st = running;
    }
    if (st == 0xFF) {
      if (p + 1 > endp) return bad("truncated meta event");
      const std::uint8_t type = d[p++];
      std::uint32_t ml;
      if (!readVlq(ml) || ml > endp - p) return bad("truncated meta event");
      if (type == 0x2F) ended = true;
      else if (type == 0x03) spec.trackName.assign(reinterpret_cast<const char*>(d + p), ml);
      else if (type == 0x51 && ml == 3) spec.bpm = 60000000.0 / static_cast<double>((d[p] << 16) | (d[p + 1] << 8) | d[p + 2]);
      else if (type == 0x58 && ml >= 2) {
        if (d[p + 1] > 5) return bad("invalid time signature");
        spec.numerator = d[p];
        spec.denominator = 1 << d[p + 1];
      }
      p += ml;
    } else if (st == 0xF0 || st == 0xF7) {
      std::uint32_t sl;
      if (!readVlq(sl) || sl > endp - p) return bad("truncated sysex");
      p += sl;
    } else {
      const int kind = st & 0xF0, ch = st & 0x0F;
      const std::size_t need = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
      if (p + need > endp) return bad("truncated channel event");
      const int a = d[p], b = need == 2 ? d[p + 1] : 0;
      p += need;
      if (a > 127 || b > 127) return bad("invalid data byte");
      if (kind == 0x90 && b > 0) {
        if (open.count({ch, a})) return bad("note started twice");
        open[{ch, a}] = {now, b};
      } else if (kind == 0x80 || kind == 0x90) {
        auto it = open.find({ch, a});
        if (it == open.end()) return bad("note ended without a start");
        spec.notes.push_back({a, it->second.second, it->second.first, now - it->second.first, ch});
        open.erase(it);
      }
    }
  }
  if (!ended || !open.empty()) return bad("track is not terminated cleanly");
  std::stable_sort(spec.notes.begin(), spec.notes.end(), [](const Note& x, const Note& y) {
    if (x.start != y.start) return x.start < y.start;
    return x.note < y.note;
  });
  return spec;
}

}  // namespace chopfractal::midi
