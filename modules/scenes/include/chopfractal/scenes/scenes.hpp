#pragma once
// Scene list and sequencer for Evolve scenes: ordered pattern snapshots (each with its seed), how many loops each
// plays, and what follows it. The sequencer is a pure state machine driven by observed loop boundaries; it never
// touches audio, patterns or time. Threading: one thread only (the plugin uses the message thread).
#include <chopfractal/chop_contracts/result.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::scenes {

constexpr std::uint32_t kSchemaVersion = 1;
constexpr std::size_t kMaxScenes = 32;
constexpr int kMaxLoops = 64;
constexpr std::size_t kMaxNameLength = 32;
constexpr std::size_t kMaxPatternBytes = 1u << 20;

using SceneId = std::uint32_t;
enum class Next : std::uint8_t { Advance = 0, Repeat = 1, Stop = 2, Jump = 3 };

struct Scene {
  static constexpr std::uint64_t kManual = ~0ull;
  SceneId id = 0;
  std::string name;
  int loops = 4;
  Next next = Next::Advance;
  SceneId jumpTo = 0;
  std::uint64_t seed = 0;
  std::uint64_t evolveStep = kManual;
  std::uint32_t eventCount = 0;
  std::int32_t bars = 0;
  std::vector<std::uint8_t> pattern;  // pattern::serialize() bytes
};

struct SceneList {
  std::vector<Scene> scenes;
  SceneId nextId = 1;
  bool wrap = true;
};

Status validate(const SceneList& list);
Result<SceneId> add(SceneList& list, Scene scene);  // assigns id = nextId++; LimitExceeded at kMaxScenes
Status remove(SceneList& list, SceneId id);         // dangling Jump targets become Advance
Status move(SceneList& list, SceneId id, int newIndex);
const Scene* find(const SceneList& list, SceneId id);
int indexOf(const SceneList& list, SceneId id);  // -1 when absent

std::vector<std::uint8_t> serialize(const SceneList& list);
Result<SceneList> deserialize(const std::uint8_t* data, std::size_t size);

struct Advance {
  SceneId arm = 0;       // scene the audio side should switch to at the next loop boundary
  bool switched = false; // the armed scene became the playing scene at this boundary
  SceneId playing = 0;
  bool finished = false; // the sequence ended (Stop decided); the last scene keeps looping
};

class Sequencer {
 public:
  Advance start(const SceneList& list, SceneId first);
  void stop();
  bool running() const { return running_; }
  SceneId current() const { return current_; }
  SceneId armed() const { return armed_; }
  int loopIndex() const { return loopIndex_; }
  Advance onBoundary(const SceneList& list);
  void onTransportStopped();
  Advance onTransportStarted(const SceneList& list);

 private:
  Advance decide(const SceneList& list);

  bool running_ = false;
  bool finishing_ = false;
  SceneId current_ = 0;
  SceneId armed_ = 0;
  int loopIndex_ = 0;
};

}  // namespace chopfractal::scenes
