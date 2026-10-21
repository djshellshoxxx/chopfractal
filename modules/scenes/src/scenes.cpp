#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/scenes/scenes.hpp>
#include <algorithm>

namespace chopfractal::scenes {

Status validateScene(const Scene& s) {
  if (s.name.empty() || s.name.size() > kMaxNameLength) return makeError(ErrorCode::InvalidArgument, "Scene names must be 1 to 32 characters.");
  if (s.loops < 1 || s.loops > kMaxLoops) return makeError(ErrorCode::OutOfRange, "A scene plays 1 to 64 loops.", s.loops < 1 ? 1 : kMaxLoops);
  if (static_cast<unsigned>(s.next) > static_cast<unsigned>(Next::Jump)) return makeError(ErrorCode::InvalidArgument, "Unknown scene sequencing mode.");
  if (s.pattern.size() > kMaxPatternBytes) return makeError(ErrorCode::LimitExceeded, "A scene pattern is too large.", static_cast<std::int64_t>(kMaxPatternBytes));
  if (s.pattern.empty()) return makeError(ErrorCode::InvalidArgument, "A scene needs a pattern.");
  return {};
}

const Scene* find(const SceneList& l, SceneId id) {
  for (const Scene& s : l.scenes)
    if (s.id == id) return &s;
  return nullptr;
}
Scene* find(SceneList& l, SceneId id) {
  for (Scene& s : l.scenes)
    if (s.id == id) return &s;
  return nullptr;
}
int indexOf(const SceneList& l, SceneId id) {
  for (std::size_t i = 0; i < l.scenes.size(); ++i)
    if (l.scenes[i].id == id) return static_cast<int>(i);
  return -1;
}

Status validate(const SceneList& l) {
  if (l.scenes.size() > kMaxScenes) return makeError(ErrorCode::LimitExceeded, "Too many scenes.", static_cast<std::int64_t>(kMaxScenes));
  for (std::size_t i = 0; i < l.scenes.size(); ++i) {
    const Scene& s = l.scenes[i];
    if (Status st = validateScene(s); !st) return st;
    if (s.id == 0 || s.id >= l.nextId) return makeError(ErrorCode::Corrupt, "Scene id out of range.");
    for (std::size_t j = 0; j < i; ++j)
      if (l.scenes[j].id == s.id) return makeError(ErrorCode::Corrupt, "Duplicate scene id.");
  }
  for (const Scene& s : l.scenes)
    if (s.next == Next::Jump && !find(l, s.jumpTo)) return makeError(ErrorCode::NotFound, "Jump target does not exist.");
  return {};
}

Result<SceneId> add(SceneList& l, Scene s) {
  if (l.scenes.size() >= kMaxScenes) return makeError(ErrorCode::LimitExceeded, "The scene list is full.", static_cast<std::int64_t>(kMaxScenes));
  if (l.nextId == 0 || l.nextId == ~SceneId{0}) return makeError(ErrorCode::LimitExceeded, "Scene ids are exhausted.");
  if (Status st = validateScene(s); !st) return st.error();
  if (s.next == Next::Jump && !find(l, s.jumpTo)) return makeError(ErrorCode::NotFound, "Jump target does not exist.");
  if (s.next != Next::Jump) s.jumpTo = 0;
  s.id = l.nextId++;
  l.scenes.push_back(std::move(s));
  return l.scenes.back().id;
}

Status remove(SceneList& l, SceneId id) {
  const int idx = indexOf(l, id);
  if (idx < 0) return makeError(ErrorCode::NotFound, "No such scene.");
  l.scenes.erase(l.scenes.begin() + idx);
  for (Scene& s : l.scenes)
    if (s.next == Next::Jump && s.jumpTo == id) {
      s.next = Next::Advance;
      s.jumpTo = 0;
    }
  return {};
}

Status move(SceneList& l, SceneId id, int newIndex) {
  const int idx = indexOf(l, id);
  if (idx < 0) return makeError(ErrorCode::NotFound, "No such scene.");
  if (newIndex < 0 || newIndex >= static_cast<int>(l.scenes.size())) return makeError(ErrorCode::OutOfRange, "Scene position out of range.", static_cast<std::int64_t>(l.scenes.size()) - 1);
  Scene tmp = std::move(l.scenes[static_cast<std::size_t>(idx)]);
  l.scenes.erase(l.scenes.begin() + idx);
  l.scenes.insert(l.scenes.begin() + newIndex, std::move(tmp));
  return {};
}

std::vector<std::uint8_t> serialize(const SceneList& l) {
  std::vector<std::uint8_t> out;
  bytes::Writer w(out);
  w.u8(l.wrap ? 1 : 0);
  w.u32(l.nextId);
  w.u32(static_cast<std::uint32_t>(l.scenes.size()));
  for (const Scene& s : l.scenes) {
    w.u32(s.id);
    w.str(s.name);
    w.i32(s.loops);
    w.u8(static_cast<std::uint8_t>(s.next));
    w.u32(s.jumpTo);
    w.u64(s.seed);
    w.u64(s.evolveStep);
    w.u32(s.eventCount);
    w.i32(s.bars);
    w.u32(static_cast<std::uint32_t>(s.pattern.size()));
    w.raw(s.pattern.data(), s.pattern.size());
  }
  return out;
}

Result<SceneList> deserialize(const std::uint8_t* data, std::size_t size) {
  if (size > kMaxSectionBytes) return makeError(ErrorCode::LimitExceeded, "Scene section too large.");
  bytes::Reader r(data, size);
  SceneList l;
  const std::uint8_t flags = r.u8();
  l.wrap = (flags & 1) != 0;
  l.nextId = r.u32();
  const std::uint32_t n = r.count(static_cast<std::uint32_t>(kMaxScenes), 4);
  for (std::uint32_t i = 0; i < n && r.ok(); ++i) {
    Scene s;
    s.id = r.u32();
    s.name = r.str(kMaxNameLength);
    s.loops = r.i32();
    const std::uint8_t nx = r.u8();
    if (nx > static_cast<std::uint8_t>(Next::Jump)) r.fail();
    s.next = static_cast<Next>(nx);
    s.jumpTo = r.u32();
    s.seed = r.u64();
    s.evolveStep = r.u64();
    s.eventCount = r.u32();
    s.bars = r.i32();
    const std::uint32_t psz = r.u32();
    if (!r.ok() || psz > kMaxPatternBytes || psz > r.remaining()) {
      r.fail();
      break;
    }
    const std::uint8_t* p = r.take(psz);
    if (p) s.pattern.assign(p, p + psz);
    l.scenes.push_back(std::move(s));
  }
  if (!r.ok() || r.remaining() != 0) return makeError(ErrorCode::Corrupt, "Scene list is damaged.");
  if (Status st = validate(l); !st) return makeError(ErrorCode::Corrupt, st.error().message);
  return l;
}

// ---- Sequencer ------------------------------------------------------------------------------------------

void Sequencer::decide(const SceneList& l, Advance& out) {
  const Scene* cur = find(l, current_);
  if (!cur) {
    running_ = false;
    out.finished = true;
    return;
  }
  if (armed_ != 0 || finishing_ || loopIndex_ != cur->loops - 1) return;
  const int idx = indexOf(l, current_);
  SceneId target = 0;
  switch (cur->next) {
    case Next::Repeat: return;
    case Next::Stop: finishing_ = true; return;
    case Next::Jump:
      target = find(l, cur->jumpTo) ? cur->jumpTo : 0;
      break;
    case Next::Advance:
      if (idx + 1 < static_cast<int>(l.scenes.size())) target = l.scenes[static_cast<std::size_t>(idx) + 1].id;
      else if (l.wrap) target = l.scenes.front().id;
      break;
  }
  if (target == 0) {
    finishing_ = true;
    return;
  }
  armed_ = target;
  out.arm = target;
}

Advance Sequencer::start(const SceneList& l, SceneId first) {
  Advance out;
  running_ = false;
  finishing_ = false;
  armed_ = 0;
  loopIndex_ = 0;
  current_ = 0;
  if (!find(l, first)) return out;
  current_ = first;
  running_ = true;
  out.playing = current_;
  decide(l, out);
  return out;
}

void Sequencer::stop() {
  running_ = false;
  finishing_ = false;
  armed_ = 0;
  loopIndex_ = 0;
}

Advance Sequencer::onBoundary(const SceneList& l) {
  Advance out;
  out.playing = current_;
  if (!running_) return out;
  const Scene* cur = find(l, current_);
  if (!cur) {
    running_ = false;
    out.finished = true;
    return out;
  }
  ++loopIndex_;
  if (armed_ != 0 && loopIndex_ >= cur->loops) {
    if (find(l, armed_)) {
      current_ = armed_;
      loopIndex_ = 0;
      armed_ = 0;
      out.switched = true;
      out.playing = current_;
      cur = find(l, current_);
    } else {
      armed_ = 0;
      finishing_ = true;
    }
  } else if (armed_ == 0 && cur->next == Next::Repeat && loopIndex_ >= cur->loops) {
    loopIndex_ = 0;
  }
  if (armed_ == 0 && finishing_ && loopIndex_ >= cur->loops) {
    running_ = false;
    out.finished = true;
    return out;
  }
  decide(l, out);
  return out;
}

void Sequencer::onTransportStopped() {
  loopIndex_ = 0;
  armed_ = 0;
  finishing_ = false;
}

Advance Sequencer::onTransportStarted(const SceneList& l) {
  Advance out;
  out.playing = current_;
  if (running_) decide(l, out);
  return out;
}

}  // namespace chopfractal::scenes
