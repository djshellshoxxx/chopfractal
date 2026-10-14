#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/scenes/scenes.hpp>

#include <algorithm>

namespace chopfractal::scenes {
namespace {
Error err(ErrorCode c, std::string m, std::int64_t hint = 0) { return makeError(c, std::move(m), hint); }
constexpr std::size_t kMaxSectionBytes = 32u * 1024u * 1024u;
}  // namespace

const Scene* find(const SceneList& list, SceneId id) {
  for (const Scene& s : list.scenes)
    if (s.id == id) return &s;
  return nullptr;
}

int indexOf(const SceneList& list, SceneId id) {
  for (std::size_t i = 0; i < list.scenes.size(); ++i)
    if (list.scenes[i].id == id) return static_cast<int>(i);
  return -1;
}

static Status validateScene(const Scene& s) {
  if (s.name.empty() || s.name.size() > kMaxNameLength) return err(ErrorCode::OutOfRange, "A scene name must be 1 to 32 characters.");
  if (s.loops < 1 || s.loops > kMaxLoops) return err(ErrorCode::OutOfRange, "A scene plays 1 to 64 loops.", kMaxLoops);
  if (static_cast<std::uint8_t>(s.next) > static_cast<std::uint8_t>(Next::Jump)) return err(ErrorCode::OutOfRange, "Unknown scene sequence action.");
  if (s.pattern.size() > kMaxPatternBytes) return err(ErrorCode::LimitExceeded, "A scene pattern is limited to 1 MiB.", static_cast<std::int64_t>(kMaxPatternBytes));
  return {};
}

Status validate(const SceneList& list) {
  if (list.scenes.size() > kMaxScenes) return err(ErrorCode::LimitExceeded, "The scene list is full (32). Delete a scene to capture more.", kMaxScenes);
  for (std::size_t i = 0; i < list.scenes.size(); ++i) {
    const Scene& s = list.scenes[i];
    Status v = validateScene(s);
    if (!v.ok()) return v;
    if (s.id == 0 || s.id >= list.nextId) return err(ErrorCode::Corrupt, "A scene has an invalid id.");
    for (std::size_t j = 0; j < i; ++j)
      if (list.scenes[j].id == s.id) return err(ErrorCode::Corrupt, "Two scenes share an id.");
    if (s.next == Next::Jump && !find(list, s.jumpTo)) return err(ErrorCode::NotFound, "A scene jumps to a scene that does not exist.");
  }
  return {};
}

Result<SceneId> add(SceneList& list, Scene scene) {
  if (list.scenes.size() >= kMaxScenes) return err(ErrorCode::LimitExceeded, "The scene list is full (32). Delete a scene to capture more.", kMaxScenes);
  if (scene.name.size() > kMaxNameLength) scene.name.resize(kMaxNameLength);
  Status v = validateScene(scene);
  if (!v.ok()) return v.error();
  scene.id = list.nextId++;
  list.scenes.push_back(std::move(scene));
  return list.scenes.back().id;
}

Status remove(SceneList& list, SceneId id) {
  const int idx = indexOf(list, id);
  if (idx < 0) return err(ErrorCode::NotFound, "That scene does not exist.");
  list.scenes.erase(list.scenes.begin() + idx);
  for (Scene& s : list.scenes)
    if (s.next == Next::Jump && s.jumpTo == id) {
      s.next = Next::Advance;
      s.jumpTo = 0;
    }
  return {};
}

Status move(SceneList& list, SceneId id, int newIndex) {
  const int idx = indexOf(list, id);
  if (idx < 0) return err(ErrorCode::NotFound, "That scene does not exist.");
  newIndex = std::clamp(newIndex, 0, static_cast<int>(list.scenes.size()) - 1);
  Scene s = std::move(list.scenes[static_cast<std::size_t>(idx)]);
  list.scenes.erase(list.scenes.begin() + idx);
  list.scenes.insert(list.scenes.begin() + newIndex, std::move(s));
  return {};
}

std::vector<std::uint8_t> serialize(const SceneList& list) {
  std::vector<std::uint8_t> out;
  bytes::Writer w(out);
  w.u8(list.wrap ? 1 : 0);
  w.u32(list.nextId);
  w.u32(static_cast<std::uint32_t>(list.scenes.size()));
  for (const Scene& s : list.scenes) {
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
  if (size > kMaxSectionBytes) return err(ErrorCode::LimitExceeded, "The scene section is too large.");
  bytes::Reader r(data, size);
  SceneList list;
  list.wrap = (r.u8() & 1) != 0;
  list.nextId = r.u32();
  const std::uint32_t count = r.u32();
  if (!r.ok() || count > kMaxScenes) return err(ErrorCode::Corrupt, "The scene section is malformed.");
  for (std::uint32_t i = 0; i < count; ++i) {
    Scene s;
    s.id = r.u32();
    s.name = r.str(kMaxNameLength);
    s.loops = r.i32();
    s.next = static_cast<Next>(r.u8());
    s.jumpTo = r.u32();
    s.seed = r.u64();
    s.evolveStep = r.u64();
    s.eventCount = r.u32();
    s.bars = r.i32();
    const std::uint32_t n = r.u32();
    if (!r.ok() || n > kMaxPatternBytes || n > r.remaining()) return err(ErrorCode::Corrupt, "The scene section is malformed.");
    s.pattern.resize(n);
    for (std::uint32_t k = 0; k < n; ++k) s.pattern[k] = r.u8();
    list.scenes.push_back(std::move(s));
  }
  if (!r.ok() || r.remaining() != 0) return err(ErrorCode::Corrupt, "The scene section is malformed.");
  Status v = validate(list);
  if (!v.ok()) return err(ErrorCode::Corrupt, "The scene section failed validation: " + v.error().message);
  return list;
}

// ---- sequencer ----

Advance Sequencer::decide(const SceneList& list) {
  Advance out;
  out.playing = current_;
  const Scene* cur = find(list, current_);
  if (!cur || armed_ != 0 || finishing_ || loopIndex_ != cur->loops - 1) return out;
  SceneId target = 0;
  bool stop = false;
  switch (cur->next) {
    case Next::Repeat: return out;  // the same scene simply continues
    case Next::Stop: stop = true; break;
    case Next::Jump:
      target = find(list, cur->jumpTo) ? cur->jumpTo : 0;
      if (target) break;
      [[fallthrough]];
    case Next::Advance: {
      const int idx = indexOf(list, current_);
      if (idx + 1 < static_cast<int>(list.scenes.size())) target = list.scenes[static_cast<std::size_t>(idx) + 1].id;
      else if (list.wrap) target = list.scenes.front().id;
      else stop = true;
      break;
    }
  }
  if (stop || target == 0) {
    finishing_ = true;
    return out;
  }
  armed_ = target;
  out.arm = target;
  return out;
}

Advance Sequencer::start(const SceneList& list, SceneId first) {
  Advance out;
  if (!find(list, first)) {
    running_ = false;
    return out;
  }
  running_ = true;
  finishing_ = false;
  current_ = first;
  armed_ = 0;
  loopIndex_ = 0;
  out = decide(list);
  out.playing = current_;
  return out;
}

void Sequencer::stop() {
  running_ = false;
  finishing_ = false;
  armed_ = 0;
  loopIndex_ = 0;
}

Advance Sequencer::onBoundary(const SceneList& list) {
  Advance out;
  out.playing = current_;
  if (!running_) return out;
  const Scene* cur = find(list, current_);
  if (!cur) {
    stop();
    out.finished = true;
    return out;
  }
  ++loopIndex_;
  if (armed_ != 0 && loopIndex_ >= cur->loops) {
    current_ = armed_;
    loopIndex_ = 0;
    armed_ = 0;
    finishing_ = false;
    out.switched = true;
  } else if (cur->next == Next::Repeat && loopIndex_ >= cur->loops) {
    loopIndex_ = 0;
  } else if (loopIndex_ >= cur->loops && armed_ == 0) {
    running_ = false;
    finishing_ = false;
    out.finished = true;
    out.playing = current_;
    return out;
  }
  Advance next = decide(list);
  out.arm = next.arm;
  out.playing = current_;
  return out;
}

void Sequencer::onTransportStopped() {
  loopIndex_ = 0;
  armed_ = 0;
  finishing_ = false;
}

Advance Sequencer::onTransportStarted(const SceneList& list) {
  if (!running_) return {};
  return decide(list);
}

}  // namespace chopfractal::scenes
