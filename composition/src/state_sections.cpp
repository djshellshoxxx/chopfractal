// The StateSection API (F01 spec section 4.4) and the project extras accessors. The wire format lives in
// state_extras.cpp; saveState/loadState (project_session.cpp) call into it.
#include <chopfractal/composition/project_session.hpp>

#include "state_wire.hpp"

namespace chopfractal::composition {

Status ProjectSession::setExtras(const ProjectExtras& e) {
  Status s = validate(e);
  if (!s.ok()) return s;
  extras_ = e;
  return {};
}

const StateSection* ProjectSession::section(std::uint8_t tag) const {
  auto it = sections_.find(tag);
  return it == sections_.end() ? nullptr : &it->second;
}

Status ProjectSession::setSection(StateSection s) {
  if (s.tag < kFirstFeatureTag || s.tag > kLastFeatureTag || s.tag == kTagFractalTune)
    return makeError(ErrorCode::InvalidArgument, "state section tags 9 to 31 (except 13) are available to features");
  if (s.bytes.empty()) {
    sections_.erase(s.tag);
    return {};
  }
  if (s.bytes.size() > sectionByteCap(s.tag))
    return makeError(ErrorCode::OutOfRange, "the state section is over its size cap", static_cast<std::int64_t>(sectionByteCap(s.tag)));
  std::size_t total = s.bytes.size();
  for (const auto& kv : sections_)
    if (kv.first != s.tag) total += kv.second.bytes.size() + 2;
  if (total > kMaxExtrasBytes) return makeError(ErrorCode::OutOfRange, "the state sections are over the 40 MiB total", static_cast<std::int64_t>(kMaxExtrasBytes));
  if (sections_.find(s.tag) == sections_.end() && sections_.size() + 10 > kMaxExtrasBlocks)
    return makeError(ErrorCode::LimitExceeded, "too many state sections", static_cast<std::int64_t>(kMaxExtrasBlocks));
  sections_[s.tag] = std::move(s);
  return {};
}

void ProjectSession::registerSectionDecoder(std::uint8_t tag, std::string feature, SectionDecoder decoder) {
  if (decoder)
    sectionDecoders_[tag] = SectionDecoderEntry{std::move(feature), std::move(decoder)};
  else
    sectionDecoders_.erase(tag);
}

// Everything saveState would add after the 38 v1 bytes. Empty means "write schema 1, the exact old bytes".
std::map<std::uint8_t, std::vector<std::uint8_t>> ProjectSession::extrasBlocks() const {
  statewire::Blocks blocks;
  statewire::encodeExtras(extras_, blocks);
  std::array<std::vector<std::uint8_t>, 2> slots;
  for (std::size_t i = 0; i < 2; ++i)
    if (auto p = patterns_.slot(i)) {
      auto bytes = pattern::serialize(*p);
      if (bytes.size() <= statewire::kMaxSlotBytes) slots[i] = std::move(bytes);  // a snapshot we could not read back again is not saved
    }
  statewire::encodeAB(slots, blocks);
  for (const auto& [tag, sec] : sections_) {
    std::vector<std::uint8_t> payload;
    bytes::Writer w(payload);
    w.u16(sec.schema);
    w.raw(sec.bytes.data(), sec.bytes.size());
    blocks[tag] = std::move(payload);
  }
  return blocks;
}

bool ProjectSession::stateUsesSchemaV2() const { return !extrasBlocks().empty(); }

}  // namespace chopfractal::composition
