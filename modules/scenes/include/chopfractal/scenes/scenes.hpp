#pragma once
// Scene list and loop-boundary sequencer. A scene is an opaque serialized pattern plus playback metadata;
// this module never parses the pattern bytes (the host validates them), so it depends only on the
// contracts. The Sequencer is a pure state machine: its output depends only on the list and on how many
// loop boundaries it has been told about.
#include <chopfractal/chop_contracts/result.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chopfractal::scenes {

constexpr std::uint32_t kSchemaVersion = 1;
constexpr std::size_t kMaxScenes = 32;
constexpr int kMaxLoops = 64;
constexpr int kDefaultLoops = 4;
constexpr std::size_t kMaxNameLength = 32;
constexpr std::size_t kMaxPatternBytes = 1u << 20;  // per scene, serialized pattern
constexpr std::size_t kMaxSectionBytes = 32u << 20;

using SceneId = std::uint32_t;  // stable, never reused in a list; display number = position + 1

enum class Next : std::uint8_t { Advance = 0, Repeat = 1, Stop = 2, Jump = 3 };

struct Scene {
  static constexpr std::uint64_t kManual = ~0ull;
  SceneId id = 0;
  std::string name;
  int loops = kDefaultLoops;
  Next next = Next::Advance;
  SceneId jumpTo = 0;
  std::uint64_t seed = 0;                // pattern.settings.seed at capture
  std::uint64_t evolveStep = kManual;    // Evolve step index when recorded, else kManual
  std::uint32_t eventCount = 0;
  std::int32_t bars = 0;
  std::vector<std::uint8_t> pattern;     // serialized pattern bytes, validated by the host
};

struct SceneList {
  std::vector<Scene> scenes;
  SceneId nextId = 1;
  bool wrap = true;
};

// Field checks for one scene (name 1..32 bytes, loops 1..64, known Next, pattern size).
Status validateScene(const Scene&);
// Counts, every scene, unique ids below nextId, Jump targets exist.
Status validate(const SceneList&);
Result<SceneId> add(SceneList&, Scene);  // assigns id = nextId++, LimitExceeded at kMaxScenes
Status remove(SceneList&, SceneId);      // also turns dangling Jump targets into Advance
Status move(SceneList&, SceneId, int newIndex);
const Scene* find(const SceneList&, SceneId);
Scene* find(SceneList&, SceneId);
int indexOf(const SceneList&, SceneId);  // -1 when absent

// Schema 1 body (the host wraps it in its own versioned section). Deterministic.
std::vector<std::uint8_t> serialize(const SceneList&);
Result<SceneList> deserialize(const std::uint8_t* data, std::size_t size);

struct Advance {
  SceneId arm = 0;        // scene the host must arm for the next loop boundary
  bool switched = false;  // the armed scene became current at this boundary
  SceneId playing = 0;    // current scene after the call
  bool finished = false;  // a Stop was reached; the last scene keeps looping
};

class Sequencer {
 public:
  // Current = first, loop index 0; arms the follower immediately when first.loops == 1.
  Advance start(const SceneList&, SceneId first);
  void stop();
  bool running() const { return running_; }
  SceneId current() const { return current_; }
  int loopIndex() const { return loopIndex_; }
  SceneId armed() const { return armed_; }
  // Call once per observed pattern-loop boundary.
  Advance onBoundary(const SceneList&);
  // loopIndex = 0, armed = 0; the caller cancels the audio-side arm.
  void onTransportStopped();
  // Re-arms when the current scene has a single loop.
  Advance onTransportStarted(const SceneList&);

 private:
  void decide(const SceneList&, Advance&);
  bool running_ = false;
  bool finishing_ = false;
  SceneId current_ = 0;
  SceneId armed_ = 0;
  int loopIndex_ = 0;
};

}  // namespace chopfractal::scenes
