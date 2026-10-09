#pragma once
// Private copy of the bounds-checked byte codec (kept private so this module has zero dependencies).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace chopfractal::history::detail {

class Writer {
 public:
  explicit Writer(std::vector<std::uint8_t>& out) : out_(out) {}
  void u8(std::uint8_t v) { out_.push_back(v); }
  void u16(std::uint16_t v) { le(v, 2); }
  void u32(std::uint32_t v) { le(v, 4); }
  void u64(std::uint64_t v) { le(v, 8); }
  void i32(std::int32_t v) { le(static_cast<std::uint32_t>(v), 4); }
  void i64(std::int64_t v) { le(static_cast<std::uint64_t>(v), 8); }
  void boolean(bool v) { u8(v ? 1 : 0); }
  void f32(float v) {
    std::uint32_t b;
    std::memcpy(&b, &v, 4);
    u32(b);
  }
  void str(const std::string& s) {
    u32(static_cast<std::uint32_t>(s.size()));
    out_.insert(out_.end(), s.begin(), s.end());
  }
  void raw(const void* p, std::size_t n) {
    const auto* b = static_cast<const std::uint8_t*>(p);
    out_.insert(out_.end(), b, b + n);
  }

 private:
  void le(std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i) out_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
  }
  std::vector<std::uint8_t>& out_;
};

class Reader {
 public:
  Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
  bool ok() const { return ok_; }
  void fail() { ok_ = false; }
  std::size_t remaining() const { return size_ - pos_; }

  std::uint8_t u8() { return static_cast<std::uint8_t>(le(1)); }
  std::uint16_t u16() { return static_cast<std::uint16_t>(le(2)); }
  std::uint32_t u32() { return static_cast<std::uint32_t>(le(4)); }
  std::uint64_t u64() { return le(8); }
  std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
  std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
  bool boolean() { return u8() != 0; }
  float f32() {
    const std::uint32_t b = u32();
    float f;
    std::memcpy(&f, &b, 4);
    if (!std::isfinite(f)) {
      ok_ = false;
      return 0.f;
    }
    return f;
  }
  std::string str(std::size_t maxLen) {
    const std::uint32_t n = u32();
    if (!ok_ || n > maxLen || n > remaining()) {
      ok_ = false;
      return {};
    }
    const std::uint8_t* p = take(n);
    return p ? std::string(reinterpret_cast<const char*>(p), n) : std::string();
  }
  // Element count that is bounded by `maxCount` and by what the remaining input could possibly hold.
  std::uint32_t count(std::uint32_t maxCount, std::size_t minElementBytes) {
    const std::uint32_t n = u32();
    if (!ok_) return 0;
    if (n > maxCount || (minElementBytes != 0 && n > remaining() / minElementBytes)) {
      ok_ = false;
      return 0;
    }
    return n;
  }
  // Returns a pointer to n bytes inside the input, or nullptr (and fails) when out of range.
  const std::uint8_t* take(std::size_t n) {
    if (!ok_ || n > size_ - pos_) {
      ok_ = false;
      return nullptr;
    }
    const std::uint8_t* p = data_ + pos_;
    pos_ += n;
    return p;
  }

 private:
  std::uint64_t le(int n) {
    const std::uint8_t* p = take(static_cast<std::size_t>(n));
    if (!p) return 0;
    std::uint64_t v = 0;
    for (int i = n - 1; i >= 0; --i) v = (v << 8) | p[i];
    return v;
  }
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
  bool ok_ = true;
};

}  // namespace chopfractal::history::detail
