#pragma once
// Engine v2 role annotation: every chop gets the drum role it plays and an invisible level-match trim.
//
// Explicit assignments (the role map) win; otherwise chop_insight classifies the chop from its audio. Within the
// snare-like chops a clearly quieter one becomes a ghost note source, and within the hats a clearly longer one
// the open hat. The trim moves chops of one role towards that role's median loudness so a velocity means the same
// loudness whichever chop plays it (research section 7.3). Pure and deterministic.
#include <chopfractal/audio_renderer/renderer.hpp>
#include <chopfractal/chop_contracts/chop.hpp>
#include <chopfractal/chop_contracts/event.hpp>
#include <chopfractal/chop_roles_grammar/grammar.hpp>

#include <memory>
#include <vector>

namespace chopfractal::composition {

// Maps a built-in role id ("kick", "snare", "hat", "cymbal", "percussion", "accent", ...) to the engine role.
DrumRole drumRoleFromRoleId(const std::string& id);

// Returns `raw` with ChopInfo::role and ChopInfo::gainTrim filled. `source` may be null (then only explicit
// assignments are used and every trim is 1).
ChopSnapshot annotateChops(const ChopSnapshot& raw, const render::SourceData* source, const roles::RoleMap& roles);

// Fractal Rhythm honouring anchors (engine v2, 4/4 bars of 3840 ticks): the downbeat becomes a kick and beats 2 and 4
// a snare, whatever the motif put there. An existing top-level hit within half a 16th of the anchor is replaced by a
// plain anchor hit (its subdivision is dropped); otherwise one is added. Other events are untouched.
std::vector<Event> overlayAnchors(std::vector<Event> events, const ChopSnapshot& annotated, Ticks barTicks, std::uint64_t seed, int bar);

// Zoom honouring anchors: the first child of an anchor hit starts at 0 on the parent's own chop at the parent's level,
// so the kick or backbeat transient survives subdivision. Adds an event only when `budget` allows.
std::shared_ptr<const NestedPattern> keepTransientFirst(const std::shared_ptr<const NestedPattern>& child, const Event& parent, std::uint64_t seed, std::size_t budget);

}  // namespace chopfractal::composition
