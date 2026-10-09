#include <chopfractal/chop_contracts/rng.hpp>
#include <chopfractal/wav_export/wav.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <atomic>
#include <fstream>
#include <functional>
#include <thread>

namespace chopfractal::wav {
namespace {

void put16(std::vector<std::uint8_t>& o, std::uint32_t v) {
  o.push_back(static_cast<std::uint8_t>(v));
  o.push_back(static_cast<std::uint8_t>(v >> 8));
}
void put32(std::vector<std::uint8_t>& o, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) o.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void tag(std::vector<std::uint8_t>& o, const char* t) {
  for (int i = 0; i < 4; ++i) o.push_back(static_cast<std::uint8_t>(t[i]));
}
std::uint32_t get16(const std::uint8_t* p) { return static_cast<std::uint32_t>(p[0] | (p[1] << 8)); }
std::uint32_t get32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}
int bytesPer(Format f) { return f == Format::Pcm16 ? 2 : f == Format::Pcm24 ? 3 : 4; }

}  // namespace

Result<std::vector<std::uint8_t>> encode(const std::vector<std::vector<float>>& planar, const Options& o, EncodeReport* report) {
  const std::size_t ch = planar.size();
  if (ch < 1 || ch > 2) return makeError(ErrorCode::InvalidArgument, "WAV export supports mono or stereo");
  const std::size_t frames = planar[0].size();
  for (const auto& c : planar)
    if (c.size() != frames) return makeError(ErrorCode::InvalidArgument, "channels differ in length");
  if (o.sampleRate < 8000 || o.sampleRate > 384000) return makeError(ErrorCode::OutOfRange, "sample rate out of range");
  if (!(o.ceilingDb >= -60.f && o.ceilingDb <= 0.f)) return makeError(ErrorCode::OutOfRange, "ceiling must lie in [-60, 0] dB");
  const std::uint64_t bpf = static_cast<std::uint64_t>(bytesPer(o.format)) * ch;
  const std::uint64_t dataBytes = static_cast<std::uint64_t>(frames) * bpf;
  if (dataBytes > kMaxDataBytes) return makeError(ErrorCode::LimitExceeded, "the file would exceed the 4 GiB WAV limit");

  float peak = 0.f;
  for (const auto& c : planar)
    for (float x : c)
      if (std::isfinite(x)) peak = std::max(peak, std::fabs(x));
  float gain = 1.f;
  if (o.normalize && peak > 0.f) gain = static_cast<float>(std::pow(10.0, static_cast<double>(o.ceilingDb) / 20.0)) / peak;

  std::vector<std::uint8_t> out;
  out.reserve(static_cast<std::size_t>(dataBytes) + 64);
  const std::uint32_t pad = static_cast<std::uint32_t>(dataBytes & 1);
  tag(out, "RIFF");
  put32(out, static_cast<std::uint32_t>(36 + dataBytes + pad));
  tag(out, "WAVE");
  tag(out, "fmt ");
  put32(out, 16);
  put16(out, o.format == Format::Float32 ? 3 : 1);
  put16(out, static_cast<std::uint32_t>(ch));
  put32(out, static_cast<std::uint32_t>(o.sampleRate));
  put32(out, static_cast<std::uint32_t>(o.sampleRate * bpf));
  put16(out, static_cast<std::uint32_t>(bpf));
  put16(out, static_cast<std::uint32_t>(bytesPer(o.format) * 8));
  tag(out, "data");
  put32(out, static_cast<std::uint32_t>(dataBytes));

  std::uint64_t clipped = 0;
  Rng dither0(deriveKey(o.ditherSeed, 0)), dither1(deriveKey(o.ditherSeed, 1));
  for (std::size_t i = 0; i < frames; ++i) {
    for (std::size_t c = 0; c < ch; ++c) {
      float x = planar[c][i];
      if (!std::isfinite(x)) x = 0.f;
      x *= gain;
      switch (o.format) {
        case Format::Float32: {
          std::uint32_t bits;
          std::memcpy(&bits, &x, 4);
          put32(out, bits);
          break;
        }
        case Format::Pcm16: {
          double v = static_cast<double>(x) * 32768.0;
          if (o.dither) {
            Rng& r = c == 0 ? dither0 : dither1;
            v += r.uniform01() - r.uniform01();
          }
          std::int64_t q = std::llround(v);
          if (q > 32767) {
            q = 32767;
            if (x > 1.0f) ++clipped;  // exactly full scale is representable enough: not a clip
          } else if (q < -32768) {
            q = -32768;
            if (x < -1.0f) ++clipped;
          }
          put16(out, static_cast<std::uint32_t>(static_cast<std::int16_t>(q)) & 0xFFFFu);
          break;
        }
        case Format::Pcm24: {
          std::int64_t q = std::llround(static_cast<double>(x) * 8388608.0);
          if (q > 8388607) {
            q = 8388607;
            if (x > 1.0f) ++clipped;
          } else if (q < -8388608) {
            q = -8388608;
            if (x < -1.0f) ++clipped;
          }
          const std::uint32_t u = static_cast<std::uint32_t>(q) & 0xFFFFFFu;
          out.push_back(static_cast<std::uint8_t>(u));
          out.push_back(static_cast<std::uint8_t>(u >> 8));
          out.push_back(static_cast<std::uint8_t>(u >> 16));
          break;
        }
      }
    }
  }
  if (pad) out.push_back(0);
  if (report) {
    report->peak = peak;
    report->gain = gain;
    report->clipped = clipped;
  }
  return out;
}

Result<Decoded> decode(const std::uint8_t* d, std::size_t n) {
  if (!d || n < 44 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0)
    return makeError(ErrorCode::Corrupt, "not a WAV file");
  std::size_t pos = 12;
  bool haveFmt = false;
  std::uint32_t tagv = 0, ch = 0, rate = 0, bits = 0;
  while (pos + 8 <= n) {
    const std::uint32_t len = get32(d + pos + 4);
    const std::size_t body = pos + 8;
    if (len > n - body) return makeError(ErrorCode::Corrupt, "chunk runs past the end of the file");
    if (std::memcmp(d + pos, "fmt ", 4) == 0) {
      if (len < 16) return makeError(ErrorCode::Corrupt, "fmt chunk is too short");
      tagv = get16(d + body);
      ch = get16(d + body + 2);
      rate = get32(d + body + 4);
      bits = get16(d + body + 14);
      haveFmt = true;
    } else if (std::memcmp(d + pos, "data", 4) == 0) {
      if (!haveFmt) return makeError(ErrorCode::Corrupt, "data chunk precedes fmt chunk");
      Decoded out;
      if (ch < 1 || ch > 2 || rate < 8000 || rate > 384000) return makeError(ErrorCode::UnsupportedVersion, "unsupported channel count or rate");
      if (tagv == 1 && bits == 16) out.format = Format::Pcm16;
      else if (tagv == 1 && bits == 24) out.format = Format::Pcm24;
      else if (tagv == 3 && bits == 32) out.format = Format::Float32;
      else return makeError(ErrorCode::UnsupportedVersion, "unsupported sample format");
      const std::size_t bps = bits / 8, frames = len / (bps * ch);
      out.sampleRate = static_cast<int>(rate);
      out.planar.assign(ch, std::vector<float>(frames));
      const std::uint8_t* p = d + body;
      for (std::size_t i = 0; i < frames; ++i)
        for (std::size_t c = 0; c < ch; ++c, p += bps) {
          float v;
          if (out.format == Format::Pcm16) v = static_cast<float>(static_cast<std::int16_t>(get16(p))) / 32768.f;
          else if (out.format == Format::Pcm24) {
            std::int32_t s = static_cast<std::int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
            if (s & 0x800000) s -= 0x1000000;
            v = static_cast<float>(s) / 8388608.f;
          } else {
            const std::uint32_t b = get32(p);
            std::memcpy(&v, &b, 4);
          }
          out.planar[c][i] = v;
        }
      return out;
    }
    pos = body + len + (len & 1);
  }
  return makeError(ErrorCode::Corrupt, "no data chunk");
}

Status writeFileAtomic(const std::string& path, const std::vector<std::uint8_t>& bytes, bool overwrite) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (path.empty()) return makeError(ErrorCode::InvalidArgument, "empty path");
  const fs::path target(path);
  if (fs::exists(target, ec) && !overwrite) return makeError(ErrorCode::Conflict, "a file already exists at " + path);
  static std::atomic<std::uint64_t> counter{0};
  fs::path tmp = target;
  tmp += ".cf-tmp" + std::to_string(counter.fetch_add(1)) + "-" + std::to_string(static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())) % 100000);
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return makeError(ErrorCode::InvalidArgument, "cannot create " + tmp.string());
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    f.flush();
    if (!f) {
      f.close();
      fs::remove(tmp, ec);
      return makeError(ErrorCode::InvalidArgument, "write failed for " + path);
    }
  }
  fs::rename(tmp, target, ec);  // replaces an existing file atomically on POSIX
  if (ec && overwrite && fs::exists(target)) {
    // Windows-style filesystems refuse to rename over an existing file: keep a backup so a failure cannot lose it.
    fs::path backup = tmp;
    backup += ".old";
    std::error_code ec2;
    fs::rename(target, backup, ec2);
    if (!ec2) {
      fs::rename(tmp, target, ec);
      if (ec) fs::rename(backup, target, ec2);  // put the original back
      else fs::remove(backup, ec2);
    }
  }
  if (ec) {
    fs::remove(tmp, ec);
    return makeError(ErrorCode::InvalidArgument, "could not move the finished file into place");
  }
  return {};
}

std::string sanitizeFileName(const std::string& text) {
  std::string s;
  bool lastUnderscore = true;  // also trims leading separators
  for (char ch : text) {
    const unsigned char c = static_cast<unsigned char>(ch);
    char o = '_';
    if (c >= 'a' && c <= 'z') o = static_cast<char>(c);
    else if (c >= 'A' && c <= 'Z') o = static_cast<char>(c - 'A' + 'a');
    else if ((c >= '0' && c <= '9') || c == '-') o = static_cast<char>(c);
    if (o == '_') {
      if (lastUnderscore) continue;
      lastUnderscore = true;
    } else {
      lastUnderscore = false;
    }
    s.push_back(o);
    if (s.size() >= 48) break;
  }
  while (!s.empty() && s.back() == '_') s.pop_back();
  return s.empty() ? "untitled" : s;
}

}  // namespace chopfractal::wav
