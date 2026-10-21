#pragma once
// A procedurally generated, labelled drum "kit loop": twelve one-shots laid end to end (two kicks, two snares, a ghost
// snare, three hats, an open hat, a crash and a tom) with their true roles and onsets known. It stands in for a real
// break wherever a deterministic source with ground truth is needed: the break-quality metrics, the audition tool and
// the examples. No file, no random state beyond a fixed seed: the same call always returns the same samples.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/chop_contracts/chop.hpp>

#include <memory>
#include <string>
#include <vector>

namespace chopfractal::composition {
class ProjectSession;

namespace synth {

struct KitLoop {
  std::shared_ptr<const render::SourceData> data;
  std::vector<std::int64_t> starts;   // the first sample of every one-shot, ascending (chop starts)
  std::vector<DrumRole> roles;        // the true role of each one-shot
  double bpm = 100.0;
};

// `sampleRate` 8000..192000. Mono, peak below 0.9.
KitLoop makeKitLoop(int sampleRate = 48000);

// Loads the loop into the session and places one marker per one-shot, so chop n is one-shot n. With `assignRoles` the
// kicks, snares, hats, crash and tom get their role through the role map (the ghost and open hat are always left to the
// automatic detection). Returns false on any error.
bool loadKitInto(ProjectSession& session, const KitLoop& loop, bool assignRoles = false, const std::string& name = "kit loop");

}  // namespace synth
}  // namespace chopfractal::composition
