#pragma once
// Internal: the armed loop-boundary switch (F09). The session arms the next scene's Playback through a
// second mailbox; the audio thread flips `current` exactly on the first frame of the next loop of the
// playing pattern. Everything here runs on the audio thread except `applied()`, which any thread may read.
// No allocation, no locks.
#include <atomic>
#include <chopfractal/audio_renderer/renderer.hpp>
#include <cmath>
#include <cstdint>

namespace chopfractal::render::armed {

struct Split {
  int offset = -1;          // frame in the block where the new Playback starts; -1 = no switch in this block
  double ppqAdvance = 0.0;  // quarter notes from the block start to `offset`
};

class State {
 public:
  Mailbox<ArmedSwitch> mailbox;

  void reset() {
    armed_ = nullptr;  // seenToken_ stays: an old arm still sitting in the mailbox must not come back
  }

  // Once per host block, after the main mailbox was adopted. A new token replaces the pending switch
  // (a null playback cancels it); an arm for the playback that already plays is dropped.
  void poll(const Playback* current) {
    const ArmedSwitch* item = mailbox.acquire();
    if (item && item->token != seenToken_) {
      seenToken_ = item->token;
      token_ = item->token;
      armed_ = item->playback.get();
    }
    if (armed_ == current) armed_ = nullptr;
  }

  bool pending() const { return armed_ != nullptr; }

  // Where, if anywhere, inside [0, frames) the pending switch lands. Drops an arm that can never apply
  // (different source buffer, pass-through, empty pattern). `pos0`/`tpf` are in ticks; `continuing` = the previous block played.
  Split locate(const Playback* current, const SourceData* currentSource, bool playing, bool continuing, double pos0, double tpf, int frames) {
    Split s;
    if (!armed_) return s;
    if (armed_->passThrough || armed_->lengthTicks <= 0 || !armed_->source || armed_->source.get() != currentSource) {
      armed_ = nullptr;
      return s;
    }
    if (!playing || !current || current->passThrough || current->lengthTicks <= 0) return s;
    const double len = static_cast<double>(current->lengthTicks);
    const double boundary = std::ceil(pos0 / len - 1e-9) * len;
    const double off = std::ceil((boundary - pos0) / tpf - 1e-6);
    if (!(off >= 0.0) || off >= static_cast<double>(frames)) return s;
    if (!continuing && off == 0.0) return s;  // playback just (re)started exactly on a loop point: that is the start, not a boundary crossed
    s.offset = static_cast<int>(off);
    s.ppqAdvance = off * tpf / static_cast<double>(kTicksPerQuarter);
    return s;
  }

  // Applies the switch located above; returns the new current playback.
  const Playback* apply() {
    const Playback* next = armed_;
    armed_ = nullptr;
    applied_.store(token_, std::memory_order_release);
    return next;
  }

  std::uint64_t applied() const { return applied_.load(std::memory_order_acquire); }

 private:
  const Playback* armed_ = nullptr;
  std::uint64_t seenToken_ = 0;
  std::uint64_t token_ = 0;
  std::atomic<std::uint64_t> applied_{0};
};

}  // namespace chopfractal::render::armed
