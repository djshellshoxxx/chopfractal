#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/composition/engine_roles.hpp>
#include <chopfractal/composition/project_session.hpp>

#include "state_wire.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace chopfractal::composition {
namespace {

// The dependency-free modules (state_codec, variation_history) own private Error types with the same
// shape; the composition root is where they are translated into the shared one.
// convert() casts the private enums of state_codec and variation_history by value. They never produce
// ErrorCode::Cancelled (appended last in the shared enum), but every value they do have must keep its meaning.
#define CF_SAME_ERROR_CODE(name)                                                                                                         \
  static_assert(static_cast<int>(codec::ErrorCode::name) == static_cast<int>(ErrorCode::name) &&                                       \
                    static_cast<int>(history::ErrorCode::name) == static_cast<int>(ErrorCode::name),                                   \
                "private error codes must match the shared ErrorCode")
CF_SAME_ERROR_CODE(InvalidArgument);
CF_SAME_ERROR_CODE(OutOfRange);
CF_SAME_ERROR_CODE(LimitExceeded);
CF_SAME_ERROR_CODE(NotFound);
CF_SAME_ERROR_CODE(Conflict);
CF_SAME_ERROR_CODE(Blocked);
CF_SAME_ERROR_CODE(Corrupt);
CF_SAME_ERROR_CODE(UnsupportedVersion);
#undef CF_SAME_ERROR_CODE

template <class E>
Error convert(const E& e) {
  return Error{static_cast<ErrorCode>(static_cast<int>(e.code)), e.message, e.hint};
}

constexpr char kModSource[] = "source_chop";
constexpr char kModPattern[] = "pattern_engine";
constexpr char kModRoles[] = "chop_roles_grammar";
constexpr char kModHistory[] = "variation_history";
constexpr char kModAudio[] = "source_audio";
constexpr char kModSession[] = "composition";

std::vector<const float*> channelPointers(const render::SourceData& d) {
  std::vector<const float*> v;
  for (int c = 0; c < d.channels; ++c) v.push_back(d.channel(c));
  return v;
}

bool validSource(const std::shared_ptr<const render::SourceData>& d) {
  return d && d->frames > 0 && (d->channels == 1 || d->channels == 2) &&
         d->samples.size() == static_cast<std::size_t>(d->channels) * static_cast<std::size_t>(d->frames);
}

SourceId idOf(const render::SourceData& d) {
  auto ptrs = channelPointers(d);
  return source::computeSourceId(ptrs.data(), d.channels, d.frames, d.sampleRate);
}

Result<source::Proposal> propose(const std::optional<source::ChopMap>& map, const render::SourceData* data, const DetectOptions& o) {
  if (!map) return makeError(ErrorCode::InvalidArgument, "load a source first");
  if (o.mode == DetectMode::EvenGrid) return source::evenGrid(map->info().frames, o.grid);
  if (!data) return makeError(ErrorCode::NotFound, "the source audio is missing, so transients cannot be detected");
  auto ptrs = channelPointers(*data);
  return source::detectTransients(ptrs.data(), data->channels, data->frames, data->sampleRate, o.analysis);
}

// ---- nested lookup for recursive zoom ----
bool findNested(const std::vector<Event>& events, EventId id, int depth, SampleRange ancestor, const ChopSnapshot& chops, const Event*& event,
                int& outDepth, SampleRange& bounds, bool& lockedPath) {
  for (const Event& e : events) {
    const ChopInfo* ci = chops.find(e.chop);
    const SampleRange resolved = e.region.empty() ? (ci ? ci->range : SampleRange{}) : e.region;
    if (e.id == id) {
      event = &e;
      outDepth = depth;
      bounds = resolved;
      return true;
    }
    if (e.child && findNested(e.child->events, id, depth + 1, resolved, chops, event, outDepth, bounds, lockedPath)) {
      if (e.child->locked || e.locked) lockedPath = true;  // a locked ancestor hit protects everything below it
      return true;
    }
  }
  (void)ancestor;
  return false;
}

std::shared_ptr<const NestedPattern> replaceChild(const std::shared_ptr<const NestedPattern>& pat, EventId target,
                                                  const std::shared_ptr<const NestedPattern>& newChild) {
  auto copy = std::make_shared<NestedPattern>(*pat);
  for (Event& e : copy->events) {
    if (e.id == target) {
      e.child = newChild;
      e.childActive = true;
      e.userOwned = true;
    } else if (e.child) {
      e.child = replaceChild(e.child, target, newChild);
    }
  }
  return copy;
}

std::size_t subtreeCount(const Event& e) {
  if (e.child && e.childActive) {
    std::size_t n = 0;
    for (const Event& c : e.child->events)
      if (c.enabled) n += subtreeCount(c);
    return n;
  }
  return 1;
}

std::vector<std::uint8_t> barBytes(const pattern::Pattern& p, std::size_t bar) {
  std::vector<std::uint8_t> out;
  bytes::Writer w(out);
  for (const pattern::Beat& bt : p.bars[bar].beats)
    for (const Event& e : bt.events) writeEvent(w, e);
  return out;
}

std::string wrongAudioText(const source::SourceInfo& info) {
  return "The file for \"" + info.name + "\" is not the audio this project was saved with. Markers and pattern are kept; relink the original file.";
}
std::string notFoundText(const source::SourceInfo& info) {
  return "The source \"" + info.name + "\" was not found" + (info.path.empty() ? "" : " at " + info.path) + ". Markers and pattern are kept; relink the file to hear playback.";
}

}  // namespace

struct ProjectSession::Target {
  const Event* event = nullptr;
  const Event* top = nullptr;
  int depth = 0;
  SampleRange bounds;
  bool lockedPath = false;
};

ProjectSession::ProjectSession() : history_(history::Config{}) {
  // The only registered step so far: a v1 payload is a valid v2 payload with no extras section. Policy for every
  // future bump: raise the module's schema constant, register one step per version, add a fixture test.
  migrations_.add(kModSession, kSessionSchemaV1, [](const std::vector<std::uint8_t>& in) -> codec::Result<std::vector<std::uint8_t>> { return in; });
  registerSectionDecoder(kTagGroove, "Groove", [this](const StateSection& sec) { return decodeGroove(sec); });
}

void ProjectSession::notice(Notice::Level level, std::string text) { notices_.push_back({level, std::move(text)}); }

std::vector<Notice> ProjectSession::takeNotices() {
  std::vector<Notice> out;
  out.swap(notices_);
  return out;
}

void ProjectSession::noteReport(const pattern::GenerationReport& report) {
  int locked = 0;
  bool limited = false;
  for (const pattern::Note& n : report.notes) {
    switch (n.kind) {
      case pattern::Note::Kind::BlockedByRule:
        notice(Notice::Level::Warning, (n.rule ? "Rule " + std::to_string(n.rule) : std::string("A rule")) + " blocked a change in bar " +
                                           std::to_string(n.bar + 1) + ", beat " + std::to_string(n.beat + 1) + "; the previous content was kept.");
        break;
      case pattern::Note::Kind::LockedSkipped: ++locked; break;
      case pattern::Note::Kind::LimitReached: limited = true; break;
      case pattern::Note::Kind::NoCandidate: break;
      case pattern::Note::Kind::EngineFallback:
        notice(Notice::Level::Info, "The break engine needs 4/4 on a 1/8 or finer straight grid, so this pattern used the classic engine.");
        break;
    }
  }
  if (locked) notice(Notice::Level::Info, std::to_string(locked) + " locked or edited section(s) were kept.");
  if (limited) notice(Notice::Level::Info, "The event cap limited the pattern.");
}

void ProjectSession::attachMailbox(render::Mailbox<render::Playback>* mailbox) {
  mailbox_ = mailbox;
  if (mailbox_ && playback_) mailbox_->publish(playback_);
}

void ProjectSession::publish(std::shared_ptr<const render::Playback> pb) {
  playback_ = std::move(pb);
  if (mailbox_) mailbox_->publish(playback_);
}

std::unique_ptr<roles::GrammarPolicy> ProjectSession::makePolicy() const {
  if (!chops_ || (roles_.all().empty() && rules_.rules.empty())) return nullptr;
  std::vector<ChopId> ids;
  for (const ChopInfo& c : chops_->chops) ids.push_back(c.id);
  return std::make_unique<roles::GrammarPolicy>(roles_, rules_, ids);
}

// ---------------------------------------------------------------------------------------------
// playback publication
// ---------------------------------------------------------------------------------------------

Status ProjectSession::installPlayback(const pattern::Pattern& p) {
  if (!chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  if (!sourceData_) {  // source missing: pattern authoring continues without audio
    auto flat = pattern::flatten(p, *chops_);
    return flat.ok() ? Status{} : flat.error();
  }
  auto pb = buildPlayback(p, true);
  if (!pb.ok()) return pb.error();
  publish(pb.value());
  return {};
}

Status ProjectSession::republish() {
  if (!sourceData_) {
    publish(std::make_shared<const render::Playback>());  // no source: the renderer passes input through
    return {};
  }
  if (!patterns_.hasPattern()) {
    auto pt = render::makePassThroughPlayback(sourceData_);
    if (!pt.ok()) return pt.error();
    publish(pt.value());
    return {};
  }
  return installPlayback(patterns_.current());
}

Status ProjectSession::finalize(Result<pattern::Pattern> result, const char* historyLabel, const pattern::GenerationReport* report) {
  if (!result.ok()) return result.error();
  if (fx_.any()) result = pattern::applyFxPass(result.value(), fx_);  // gesture effects follow every new pattern
  Status v = installPlayback(result.value());  // validated before the audio side ever sees it
  if (!v.ok()) return v;
  patterns_.adopt(std::move(result.value()));
  pendingActivation_ = history::kNoNode;  // a queued variation switch must not overwrite what the user just made
  if (report) noteReport(*report);
  if (historyLabel) recordHistory(historyLabel);
  return {};
}

void ProjectSession::recordHistory(const std::string& label) {
  if (!patterns_.hasPattern()) return;
  const pattern::Pattern& p = patterns_.current();
  history::Metadata meta;
  meta.seed = p.settings.seed;
  meta.label = label.size() > 256 ? label.substr(0, 256) : label;  // addSnapshot rejects longer labels
  meta.payloadSchema = pattern::kSchemaVersion;
  auto r = history_.addSnapshot(history_.active(), pattern::serialize(p), meta);
  if (!r.ok()) {
    notice(Notice::Level::Warning, "This variation was applied but not added to the family tree: " + r.error().message);
    return;
  }
  history_.setActive(r.value().id);
  if (!r.value().pruned.empty())
    notice(Notice::Level::Info, "Removed " + std::to_string(r.value().pruned.size()) + " older snapshot(s) to stay within the history limit.");
  if (pendingActivation_ != history::kNoNode && !history_.contains(pendingActivation_)) pendingActivation_ = history::kNoNode;
}

Result<std::vector<std::vector<float>>> ProjectSession::renderOffline(const render::OfflineSettings& settings) const {
  if (!playback_ || playback_->passThrough || !playback_->source) return makeError(ErrorCode::InvalidArgument, "there is no pattern to render");
  return render::renderOffline(playback_, settings);
}

Result<render::PreviewRequest> ProjectSession::auditionRequest(ChopId chop) const {
  if (!chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  const ChopInfo* c = chops_->find(chop);
  if (!c) return makeError(ErrorCode::NotFound, "that chop is not enabled");
  render::PreviewRequest r;
  r.region = c->range;
  return r;
}

// ---------------------------------------------------------------------------------------------
// source and chops
// ---------------------------------------------------------------------------------------------

Status ProjectSession::loadSource(std::shared_ptr<const render::SourceData> data, std::string name, std::string path) {
  if (!validSource(data)) return makeError(ErrorCode::InvalidArgument, "the audio buffer is empty or malformed");
  source::SourceInfo info;
  info.id = idOf(*data);
  if (name.size() > 256) name.resize(256);   // informational only; ChopMap::create rejects longer values
  if (path.size() > 1024) path.clear();
  info.name = std::move(name);
  info.channels = data->channels;
  info.sampleRate = data->sampleRate;
  info.frames = data->frames;
  info.path = std::move(path);
  auto map = source::ChopMap::create(std::move(info));
  if (!map.ok()) return map.error();  // nothing has changed yet
  evolve_.stop();  // there is no pattern for a new source
  sourceData_ = std::move(data);
  chopMap_ = std::move(map.value());
  chops_ = chopMap_->snapshot();
  roles_ = roles::RoleMap{};
  rules_ = roles::RuleSet{};  // rules can name chop ids, which restart at 1 for a new source
  foreignModules_.clear();
  patterns_.reset();
  history_ = history::VariationTree(history_.config());
  pendingActivation_ = history::kNoNode;
  return republish();
}

void ProjectSession::clearSource() {
  sourceData_.reset();
  chopMap_.reset();
  chops_.reset();
  roles_ = roles::RoleMap{};
  rules_ = roles::RuleSet{};
  foreignModules_.clear();
  patterns_.reset();
  history_ = history::VariationTree(history_.config());
  pendingActivation_ = history::kNoNode;
  republish();
}

Result<std::vector<source::Marker>> ProjectSession::previewChops(const DetectOptions& options) const {
  auto proposal = propose(chopMap_, sourceData_.get(), options);
  if (!proposal.ok()) return proposal.error();
  return chopMap_->preview(proposal.value(), options.merge);
}

Status ProjectSession::applyChops(const DetectOptions& options) {
  auto proposal = propose(chopMap_, sourceData_.get(), options);
  if (!proposal.ok()) return proposal.error();
  return editMarkers([&](source::ChopMap& m) { return m.applyProposal(proposal.value(), options.merge); });
}

Status ProjectSession::editMarkers(const std::function<Status(source::ChopMap&)>& edit) {
  if (!chopMap_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  // Transactional: if the edit or the reconcile that follows fails, the map, chops and roles are put back.
  const source::ChopMap mapBackup = *chopMap_;
  const ChopSnapshotPtr chopsBackup = chops_;
  const roles::RoleMap rolesBackup = roles_;
  Status s = edit(*chopMap_);
  if (s.ok()) s = reconcileAfterMarkerChange();
  if (!s.ok()) {
    *chopMap_ = mapBackup;
    chops_ = chopsBackup;
    roles_ = rolesBackup;
    republish();  // best effort: the previous, valid state
  }
  return s;
}

Status ProjectSession::undoMarkers() {
  if (!chopMap_) return makeError(ErrorCode::NotFound, "nothing to undo");
  bool did = false;
  Status s = editMarkers([&](source::ChopMap& m) {
    did = m.undo();
    return did ? Status{} : Status{makeError(ErrorCode::NotFound, "nothing to undo")};
  });
  return s;
}

Status ProjectSession::redoMarkers() {
  if (!chopMap_) return makeError(ErrorCode::NotFound, "nothing to redo");
  return editMarkers([&](source::ChopMap& m) { return m.redo() ? Status{} : Status{makeError(ErrorCode::NotFound, "nothing to redo")}; });
}

Status ProjectSession::reconcileAfterMarkerChange() {
  chops_ = chopMap_->snapshot();
  std::vector<ChopId> ids;  // roles follow the marker, so disabling a chop and enabling it again keeps its role
  for (const source::Marker& m : chopMap_->markers()) ids.push_back(m.id);
  roles_.retainOnly(ids);
  if (!patterns_.hasPattern()) return republish();
  pattern::Pattern cleaned = pattern::sanitize(patterns_.current(), *chops_);
  if (pattern::serialize(cleaned) == pattern::serialize(patterns_.current())) return republish();
  notice(Notice::Level::Info, "Some events were adjusted because their chops changed.");
  return finalize(std::move(cleaned), nullptr);  // one undo step
}

Status ProjectSession::relinkSource(std::shared_ptr<const render::SourceData> data) {
  if (!chopMap_) return makeError(ErrorCode::InvalidArgument, "there is no project source to relink");
  if (!validSource(data)) return makeError(ErrorCode::InvalidArgument, "the audio buffer is empty or malformed");
  if (idOf(*data) != chopMap_->info().id || data->frames != chopMap_->info().frames)
    return makeError(ErrorCode::Conflict, "this audio is not the one the project was saved with; load it as a new source (markers will reset)");
  sourceData_ = std::move(data);
  return republish();
}

// ---------------------------------------------------------------------------------------------
// roles
// ---------------------------------------------------------------------------------------------

Status ProjectSession::assignRole(ChopId chop, const std::string& role) {
  if (!chopMap_ || !chopMap_->find(chop)) return makeError(ErrorCode::NotFound, "unknown chop");
  return roles_.assignRole(chop, role);  // takes effect at the next Generate or Mutate, never retroactively
}

Status ProjectSession::setRules(roles::RuleSet rules) {
  const roles::ValidationReport rep = roles::validateRules(rules, &roles_);
  if (!rep.ok()) {
    for (const roles::Issue& i : rep.issues)
      if (i.severity == roles::Issue::Severity::Error) return makeError(ErrorCode::InvalidArgument, "rule " + std::to_string(i.rule) + ": " + i.message);
  }
  for (const roles::Issue& i : rep.issues) notice(Notice::Level::Warning, "Rule " + std::to_string(i.rule) + ": " + i.message);
  rules_ = std::move(rules);
  return {};
}

// ---------------------------------------------------------------------------------------------
// pattern
// ---------------------------------------------------------------------------------------------

Status ProjectSession::generate(const pattern::Settings& settings, const std::string& label) {
  if (!chopMap_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  auto policy = makePolicy();
  pattern::GenerationReport report;
  const ChopSnapshotPtr view = settings.engineVersion >= pattern::kEngineVersionBreak ? annotatedChops() : chops_;  // v2 reads roles and trims
  auto r = pattern::generate(*view, settings, policy.get(), &report);
  return finalize(std::move(r), (label.empty() ? std::string("Generate") : label).c_str(), &report);
}

Status ProjectSession::mutate(const pattern::MutateOptions& options, const pattern::Settings* live, const std::string& label) {
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  pattern::Pattern base = patterns_.current();
  if (live) {
    pattern::Settings& cur = base.settings;
    if (live->bars != cur.bars || !(live->timeSignature == cur.timeSignature) || !(live->grid == cur.grid))
      return makeError(ErrorCode::Conflict, "changing the pattern length, meter or grid needs Generate");
    if (live->maxEvents < static_cast<int>(pattern::eventCount(base))) return makeError(ErrorCode::Conflict, "the event cap is below the current event count");
    cur.density = live->density;
    cur.variation = live->variation;
    cur.swing = live->swing;
    cur.allowReverse = live->allowReverse;
    cur.allowPitch = live->allowPitch;
    cur.allowRetrigger = live->allowRetrigger;
    cur.allowFilter = live->allowFilter;
    cur.allowGlide = live->allowGlide;
    cur.allowCrunch = live->allowCrunch;
    cur.fxIntensity = live->fxIntensity;
    cur.pitchRange = live->pitchRange;
    cur.maxRetrigger = live->maxRetrigger;
    cur.maxShiftTicks = live->maxShiftTicks;
    cur.maxEvents = live->maxEvents;
    cur.brk = live->brk;  // profile, backbeat, anchors and amounts follow the controls; the engine version stays the pattern's own
    Status vs = pattern::validateSettings(cur);
    if (!vs.ok()) return vs;
  }
  auto policy = makePolicy();
  pattern::GenerationReport report;
  const ChopSnapshotPtr view = base.engineVersion >= pattern::kEngineVersionBreak ? annotatedChops() : chops_;
  auto r = pattern::mutate(base, *view, options, policy.get(), &report);
  return finalize(std::move(r), (label.empty() ? std::string("Mutate") : label).c_str(), &report);
}

Status ProjectSession::edit(const std::function<Result<pattern::Pattern>(const pattern::Pattern&, const ChopSnapshot&)>& fn) {
  if (!patterns_.hasPattern() || !chops_) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  return finalize(fn(patterns_.current(), *chops_), nullptr);
}

Status ProjectSession::commitEdit(const std::string& label) {
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "there is no pattern to snapshot");
  recordHistory(label.empty() ? "Edit" : label);
  return {};
}

// After the pattern history moved, make sure the restored pattern still fits the current chops (markers can
// have changed since). Returns false when it cannot be made to fit; the caller then reverts the move.
bool ProjectSession::settleRestoredPattern() {
  if (!chops_) return true;
  if (!patterns_.hasPattern()) return republish().ok();  // back before the first pattern: pass-through
  if (installPlayback(patterns_.current()).ok()) return true;
  return false;  // the caller cancels the move; the family tree (activate) repairs older versions when they are chosen
}

bool ProjectSession::undo() {
  if (!patterns_.undo()) return false;
  if (!settleRestoredPattern()) {
    patterns_.redo();
    notice(Notice::Level::Warning, "That version no longer fits the current chops, so Undo stopped here. Pick it from the family tree to adapt it.");
    return false;
  }
  pendingActivation_ = history::kNoNode;
  return true;
}

bool ProjectSession::redo() {
  if (!patterns_.redo()) return false;
  if (!settleRestoredPattern()) {
    patterns_.undo();
    notice(Notice::Level::Warning, "That version no longer fits the current chops, so Redo stopped here. Pick it from the family tree to adapt it.");
    return false;
  }
  pendingActivation_ = history::kNoNode;
  return true;
}

Status ProjectSession::recallSnapshot(std::size_t slot) {
  Status s = patterns_.recallSnapshot(slot);
  if (!s.ok()) return s;
  if (!settleRestoredPattern()) {
    patterns_.undo();
    return makeError(ErrorCode::Conflict, "that snapshot no longer fits the current chops");
  }
  pendingActivation_ = history::kNoNode;
  return {};
}

// ---- recursive hit zoom ----

Status ProjectSession::chooseTarget(const pattern::Pattern& p, EventId id, Target& out) const {
  if (!chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  out = Target{};
  for (const pattern::Bar& bar : p.bars)
    for (const pattern::Beat& bt : bar.beats)
      for (const Event& e : bt.events) {
        const ChopInfo* ci = chops_->find(e.chop);
        const SampleRange resolved = e.region.empty() ? (ci ? ci->range : SampleRange{}) : e.region;
        if (e.id == id) {
          out.event = out.top = &e;
          out.depth = 0;
          out.bounds = resolved;
          return {};
        }
        if (e.child) {
          const Event* found = nullptr;
          int depth = 0;
          SampleRange bounds;
          bool lockedPath = false;
          if (findNested(e.child->events, id, 1, resolved, *chops_, found, depth, bounds, lockedPath)) {
            out.event = found;
            out.top = &e;
            out.depth = depth;
            out.bounds = bounds;
            out.lockedPath = lockedPath || e.child->locked || e.locked;
            return {};
          }
        }
      }
  return makeError(ErrorCode::NotFound, "event not found");
}

Result<pattern::Pattern> ProjectSession::withChild(const pattern::Pattern& p, const Target& t, std::shared_ptr<const NestedPattern> child) const {
  if (t.top == t.event) return child ? pattern::setChild(p, t.event->id, std::move(child), *chops_) : pattern::collapse(p, t.event->id);
  return pattern::setChild(p, t.top->id, replaceChild(t.top->child, t.event->id, child), *chops_);
}

Result<zoom::Context> ProjectSession::zoomContext(const pattern::Pattern& p, const Target& t) const {
  zoom::Context ctx;
  ctx.sourceBounds = t.bounds;
  ctx.parentDepth = t.depth;
  const ChopInfo* ci = chops_->find(t.event->chop);
  const std::uint32_t group = ci ? ci->group : 0;
  for (const ChopInfo& c : chops_->chops)
    if (group == 0 || c.group == group) ctx.compatible.push_back(c);
  auto flat = pattern::flatten(p, *chops_);
  if (!flat.ok()) return flat.error();
  const std::size_t others = flat.value().size() - std::min(flat.value().size(), subtreeCount(*t.event));
  ctx.eventBudget = static_cast<std::size_t>(p.settings.maxEvents) > others ? static_cast<std::size_t>(p.settings.maxEvents) - others : 0;
  return ctx;
}

// Engine v2: the first child of an anchor hit (downbeat kick, backbeat snare) keeps the parent's transient.
static std::shared_ptr<const NestedPattern> anchoredChild(const ProjectSession& session, const pattern::Pattern& p, EventId id, bool topLevel, const Event& parent,
                                                          std::shared_ptr<const NestedPattern> child, std::uint64_t seed, std::size_t budget) {
  if (!topLevel || p.engineVersion < pattern::kEngineVersionBreak) return child;
  const ChopSnapshotPtr view = session.annotatedChops();
  if (!view) return child;
  const auto role = pattern::describeEvent(p, id, *view);
  if (!role || !role->anchor) return child;
  return keepTransientFirst(child, parent, seed, budget);
}

Status ProjectSession::zoomIn(EventId id, const zoom::Settings& zs, std::uint64_t seed) {
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  const pattern::Pattern& p = patterns_.current();
  Target t;
  Status s = chooseTarget(p, id, t);
  if (!s.ok()) return s;
  if (t.lockedPath || t.event->locked || (t.event->child && t.event->child->locked)) return makeError(ErrorCode::Blocked, "this hit (or a parent) is locked; unlock it first");
  auto ctx = zoomContext(p, t);
  if (!ctx.ok()) return ctx.error();
  Event parent = *t.event;
  parent.child.reset();
  auto child = zoom::createChildPattern(parent, ctx.value(), zs, seed);
  if (!child.ok()) return child.error();
  return finalize(withChild(p, t, anchoredChild(*this, p, t.event->id, t.top == t.event, parent, child.value(), seed, ctx.value().eventBudget)), "Zoom");
}

Status ProjectSession::mutateChildren(EventId id, const zoom::Settings& zs, std::uint64_t seed) {
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  const pattern::Pattern& p = patterns_.current();
  Target t;
  Status s = chooseTarget(p, id, t);
  if (!s.ok()) return s;
  if (!t.event->child) return makeError(ErrorCode::InvalidArgument, "this hit has not been zoomed into");
  if (t.lockedPath || t.event->locked || t.event->child->locked) return makeError(ErrorCode::Blocked, "the child pattern is locked; unlock it first");
  auto ctx = zoomContext(p, t);
  if (!ctx.ok()) return ctx.error();
  auto child = zoom::mutateChildren(*t.event, ctx.value(), zs, seed);
  if (!child.ok()) return child.error();
  Event parent = *t.event;
  parent.child.reset();
  return finalize(withChild(p, t, anchoredChild(*this, p, t.event->id, t.top == t.event, parent, child.value(), seed, ctx.value().eventBudget)), "Mutate children");
}

Status ProjectSession::collapse(EventId id) {
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  const pattern::Pattern& p = patterns_.current();
  Target t;
  Status s = chooseTarget(p, id, t);
  if (!s.ok()) return s;
  if (t.lockedPath || t.event->locked || (t.event->child && t.event->child->locked)) return makeError(ErrorCode::Blocked, "this hit (or a parent) is locked; unlock it first");
  return finalize(withChild(p, t, nullptr), nullptr);
}

// ---------------------------------------------------------------------------------------------
// variation family tree
// ---------------------------------------------------------------------------------------------

Status ProjectSession::activate(history::NodeId node, Quantize when) {
  auto snap = history_.getSnapshot(node);  // verifies integrity
  if (!snap.ok()) return convert(snap.error());
  if (when == Quantize::LoopBoundary) {
    pendingActivation_ = node;
    return {};
  }
  return applyActivation(node);
}

Status ProjectSession::applyActivation(history::NodeId node) {
  auto snap = history_.getSnapshot(node);
  if (!snap.ok()) return convert(snap.error());
  auto p = pattern::deserialize(snap.value().payload->data(), snap.value().payload->size());
  if (!p.ok()) return makeError(ErrorCode::Corrupt, "snapshot " + std::to_string(node) + " could not be restored: " + p.error().message, static_cast<std::int64_t>(node));
  if (!chops_) return makeError(ErrorCode::InvalidArgument, "load a source first");
  pattern::Pattern restored = pattern::sanitize(p.value(), *chops_);
  Status v = installPlayback(restored);
  if (!v.ok()) return v;
  patterns_.adopt(std::move(restored));
  history_.setActive(node);
  pendingActivation_ = history::kNoNode;
  return {};
}

void ProjectSession::onLoopBoundary() {
  if (pendingActivation_ == history::kNoNode) return;
  const history::NodeId node = pendingActivation_;
  pendingActivation_ = history::kNoNode;
  Status s = applyActivation(node);
  if (!s.ok()) notice(Notice::Level::Error, "Could not switch variation: " + s.error().message);
}

Status ProjectSession::renameNode(history::NodeId node, std::string label) {
  auto s = history_.rename(node, std::move(label));
  return s.ok() ? Status{} : Status{convert(s.error())};
}

Status ProjectSession::setFavorite(history::NodeId node, bool favorite) {
  auto s = history_.setFavorite(node, favorite);
  return s.ok() ? Status{} : Status{convert(s.error())};
}

Status ProjectSession::deleteBranch(history::NodeId node, bool includeFavorites) {
  auto r = history_.deleteBranch(node, includeFavorites ? history::DeletePolicy::IncludeFavorites : history::DeletePolicy::RefuseProtected);
  if (!r.ok()) return convert(r.error());
  if (pendingActivation_ != history::kNoNode && !history_.contains(pendingActivation_)) pendingActivation_ = history::kNoNode;
  return {};
}

Result<std::string> ProjectSession::compareNodes(history::NodeId a, history::NodeId b) const {
  auto r = history_.compare(a, b, [](const std::vector<std::uint8_t>& x, const std::vector<std::uint8_t>& y) -> std::string {
    auto pa = pattern::deserialize(x.data(), x.size());
    auto pb = pattern::deserialize(y.data(), y.size());
    if (!pa.ok() || !pb.ok()) return "one of the snapshots could not be read";
    std::string text = std::to_string(pattern::eventCount(pa.value())) + " vs " + std::to_string(pattern::eventCount(pb.value())) + " events";
    const std::size_t bars = std::min(pa.value().bars.size(), pb.value().bars.size());
    std::string differing;
    for (std::size_t i = 0; i < bars; ++i)
      if (barBytes(pa.value(), i) != barBytes(pb.value(), i)) differing += (differing.empty() ? "" : ", ") + std::to_string(i + 1);
    return text + "; bars that differ: " + (differing.empty() ? "none" : differing);
  });
  if (!r.ok()) return convert(r.error());
  return r.value();
}

// ---------------------------------------------------------------------------------------------
// project state
// ---------------------------------------------------------------------------------------------

std::size_t ProjectSession::embeddedSourceBytes() const { return sourceData_ ? 16 + sourceData_->samples.size() * sizeof(float) : 0; }

Status ProjectSession::setEmbeddedSourceCap(std::size_t bytes) {
  constexpr std::size_t kHeadroom = 64u * 1024u * 1024u;
  if (bytes == 0 || bytes > kCodecCeilingBytes - kHeadroom)
    return makeError(ErrorCode::OutOfRange, "embed cap out of range", static_cast<std::int64_t>(kCodecCeilingBytes - kHeadroom));
  embeddedCap_ = bytes;
  return {};
}

Result<std::vector<std::uint8_t>> ProjectSession::saveState(bool embedSource) const {
  codec::ProjectState st;
  if (chopMap_) st.modules[kModSource] = {source::kSchemaVersion, chopMap_->serialize()};
  if (patterns_.hasPattern()) st.modules[kModPattern] = {pattern::kSchemaVersion, pattern::serialize(patterns_.current())};
  st.modules[kModRoles] = {roles::kSchemaVersion, roles::serialize(roles_, rules_)};
  st.modules[kModHistory] = {history::kSchemaVersion, history_.serialize()};
  const bool embedded = embedSource && sourceData_;
  if (embedded) {
    if (embeddedSourceBytes() > embeddedCap_)
      return makeError(ErrorCode::LimitExceeded, "the source is too large to embed in the project", static_cast<std::int64_t>(embeddedCap_));
    std::vector<std::uint8_t> payload;
    bytes::Writer w(payload);
    w.i32(sourceData_->channels);
    w.i32(sourceData_->sampleRate);
    w.i64(sourceData_->frames);
    for (float f : sourceData_->samples) w.f32(f);
    st.modules[kModAudio] = {kSessionAudioSchema, std::move(payload)};
  }
  std::vector<std::uint8_t> meta;
  bytes::Writer mw(meta);
  mw.boolean(embedded);
  {
    const evolve::Settings& es = evolve_.settings();  // optional trailing block; absent when never configured
    mw.u8(1);
    mw.i32(es.everyLoops);
    mw.f64(es.amount);
    mw.f64(es.ramp);
    mw.f64(es.rampCeiling);
    mw.u64(es.startSeed);
  }
  std::uint32_t sessionSchema = kSessionSchemaV1;
  {
    const statewire::Blocks blocks = extrasBlocks();
    if (!blocks.empty()) {  // lazy versioning: only a project that uses something new becomes schema 2
      std::size_t total = 0;
      for (const auto& kv : blocks) total += kv.second.size() + 5;
      if (blocks.size() > kMaxExtrasBlocks || total > kMaxExtrasBytes)
        return makeError(ErrorCode::LimitExceeded, "the project's extra settings are too large to save", static_cast<std::int64_t>(kMaxExtrasBytes));
      statewire::write(blocks, meta);
      sessionSchema = kSessionSchemaVersion;
    }
  }
  st.modules[kModSession] = {sessionSchema, std::move(meta)};
  for (const auto& kv : foreignModules_) st.modules.emplace(kv.first, kv.second);  // never overwrites a module this build wrote
  auto enc = codec::encode(st);
  if (!enc.ok()) return convert(enc.error());
  return enc.value();
}

Status ProjectSession::loadState(const std::uint8_t* data, std::size_t size, const SourceResolver& resolver, const LoadOptions& options) {
  auto dec = codec::decode(data, size);
  if (!dec.ok()) return convert(dec.error());
  codec::ProjectState st = std::move(dec.value());

  // Pull one module's payload, migrating it to the schema this build understands.
  auto take = [&](const char* id, std::uint32_t current, std::vector<std::uint8_t>& out, bool& present) -> Status {
    auto it = st.modules.find(id);
    present = it != st.modules.end();
    if (!present) return {};
    auto m = migrations_.migrate(id, it->second, current);
    if (!m.ok()) return convert(m.error());
    out = std::move(m.value().bytes);
    return {};
  };

  std::map<std::string, codec::ModulePayload> foreign;  // anything this build does not know stays attached to the project
  for (const auto& kv : st.modules)
    if (kv.first != kModSource && kv.first != kModPattern && kv.first != kModRoles && kv.first != kModHistory && kv.first != kModAudio && kv.first != kModSession)
      foreign.insert(kv);
  std::vector<std::uint8_t> srcBytes, patBytes, roleBytes, histBytes, audioBytes, sessionBytes;
  bool hasSrc = false, hasPat = false, hasRoles = false, hasHist = false, hasAudio = false, hasSession = false;
  for (Status s : {take(kModSource, source::kSchemaVersion, srcBytes, hasSrc), take(kModPattern, pattern::kSchemaVersion, patBytes, hasPat),
                   take(kModRoles, roles::kSchemaVersion, roleBytes, hasRoles), take(kModHistory, history::kSchemaVersion, histBytes, hasHist),
                   take(kModAudio, kSessionAudioSchema, audioBytes, hasAudio), take(kModSession, kSessionSchemaVersion, sessionBytes, hasSession)})
    if (!s.ok()) return s;

  // ---- build everything in temporaries; the live session is untouched until all of it validates ----
  std::optional<source::ChopMap> newMap;
  if (hasSrc) {
    auto m = source::ChopMap::deserialize(srcBytes.data(), srcBytes.size());
    if (!m.ok()) return m.error();
    newMap = std::move(m.value());
  }
  if (hasPat && !newMap) return makeError(ErrorCode::Corrupt, "the project has a pattern but no source description");
  ChopSnapshotPtr newChops = newMap ? newMap->snapshot() : nullptr;

  std::shared_ptr<const render::SourceData> newData;
  std::vector<Notice> newNotices;
  if (newMap) {
    const source::SourceInfo& info = newMap->info();
    if (hasAudio) {
      bytes::Reader r(audioBytes.data(), audioBytes.size());
      auto d = std::make_shared<render::SourceData>();
      d->channels = r.i32();
      d->sampleRate = r.i32();
      d->frames = r.i64();
      if (!r.ok() || d->channels < 1 || d->channels > 2 || d->frames <= 0 || d->frames > source::kMaxSourceFrames ||
          static_cast<std::size_t>(d->channels) * static_cast<std::size_t>(d->frames) * 4 != r.remaining())
        return makeError(ErrorCode::Corrupt, "the embedded audio is malformed");
      d->samples.resize(static_cast<std::size_t>(d->channels) * static_cast<std::size_t>(d->frames));
      for (float& f : d->samples) f = r.f32();
      if (!r.ok()) return makeError(ErrorCode::Corrupt, "the embedded audio contains invalid samples");
      if (idOf(*d) != info.id) return makeError(ErrorCode::Corrupt, "the embedded audio does not match the saved source");
      newData = std::move(d);
    } else if (resolver && !options.deferSourceResolve) {
      auto found = resolver(info);
      if (found && validSource(found) && idOf(*found) == info.id && found->frames == info.frames) {
        newData = std::move(found);
      } else if (found) {
        newNotices.push_back({Notice::Level::Warning, wrongAudioText(info)});
      }
    }
    if (!newData && newNotices.empty() && !options.deferSourceResolve)
      newNotices.push_back({Notice::Level::Warning, notFoundText(info)});
  }

  roles::State newRoles;
  if (hasRoles) {
    auto r = roles::deserialize(roleBytes.data(), roleBytes.size());
    if (!r.ok()) return r.error();
    newRoles = std::move(r.value());
  }
  if (newChops) {
    std::vector<ChopId> ids;  // roles follow the marker (as in reconcileAfterMarkerChange), so disabled chops keep theirs
    if (newMap)
      for (const source::Marker& m : newMap->markers()) ids.push_back(m.id);
    newRoles.roles.retainOnly(ids);
  }

  history::VariationTree newHistory(history_.config());
  if (hasHist) {
    auto h = history::VariationTree::deserialize(histBytes.data(), histBytes.size());
    if (!h.ok()) return convert(h.error());
    newHistory = std::move(h.value());
  }

  std::optional<pattern::Pattern> newPattern;
  std::shared_ptr<const render::Playback> newPlayback;
  if (hasPat) {
    auto p = pattern::deserialize(patBytes.data(), patBytes.size());
    if (!p.ok()) return p.error();
    p.value() = pattern::sanitize(p.value(), *newChops);  // drop only what no longer fits (for example a deleted chop's zoom)
    auto flat = pattern::flatten(p.value(), *newChops);  // the pattern must fit the saved chops
    if (!flat.ok()) return makeError(ErrorCode::Corrupt, "the saved pattern does not fit the saved chops: " + flat.error().message);
    if (newData) {
      auto pb = render::makePlayback(newData, flat.value(), pattern::lengthTicks(p.value()), playbackOptions_);
      if (!pb.ok()) return makeError(ErrorCode::Corrupt, "the saved pattern cannot be played: " + pb.error().message);
      newPlayback = pb.value();
    }
    newPattern = std::move(p.value());
  } else if (newData) {
    auto pt = render::makePassThroughPlayback(newData);
    if (!pt.ok()) return pt.error();
    newPlayback = pt.value();
  }

  std::optional<evolve::Settings> newEvolve;
  bool loadedFlag = false;
  ProjectExtras newExtras;
  std::map<std::uint8_t, StateSection> newSections;
  std::array<std::vector<std::uint8_t>, 2> slotBytes;
  if (hasSession && sessionBytes.size() >= 2 + 1) {
    bytes::Reader sr(sessionBytes.data(), sessionBytes.size());
    loadedFlag = sr.boolean();  // embedded flag
    if (sr.u8() == 1) {
      evolve::Settings es;
      es.everyLoops = sr.i32();
      es.amount = sr.f64();
      es.ramp = sr.f64();
      es.rampCeiling = sr.f64();
      es.startSeed = sr.u64();
      es.enabled = false;  // a project never starts changing by itself
      if (sr.ok() && evolve::valid(es)) newEvolve = es;
    }
    // The extras section (schema 2) follows the v1 bytes. A malformed one never fails the load: defaults and one warning.
    if (sr.ok() && sr.remaining() > 0) {
      const std::uint8_t* rest = sessionBytes.data() + (sessionBytes.size() - sr.remaining());
      bool good = false;
      auto blocks = statewire::parse(rest, sr.remaining());
      if (blocks.ok()) {
        auto ex = statewire::decodeExtras(blocks.value());
        good = ex.ok();
        if (good) newExtras = ex.value();
        if (good) {
          auto ab = blocks.value().find(statewire::kTagAB);
          if (ab != blocks.value().end()) {
            auto slots = statewire::decodeAB(ab->second);
            good = slots.ok();
            if (good) slotBytes = std::move(slots.value());
          }
        }
        for (auto it = blocks.value().begin(); good && it != blocks.value().end(); ++it) {
          if (!statewire::isFeatureTag(it->first)) continue;
          if (it->second.size() < 2) {
            good = false;
            break;
          }
          StateSection sec;
          sec.tag = it->first;
          sec.schema = static_cast<std::uint16_t>(it->second[0] | (it->second[1] << 8));
          sec.bytes.assign(it->second.begin() + 2, it->second.end());
          newSections.emplace(sec.tag, std::move(sec));
        }
      }
      if (!good) {
        newExtras = ProjectExtras{};
        newSections.clear();
        slotBytes = {};
        newNotices.push_back({Notice::Level::Warning, "Some editor settings could not be restored."});
      }
    }
  }

  // A/B snapshots must fit the loaded chops; one that does not is dropped on its own.
  std::array<std::shared_ptr<const pattern::Pattern>, 2> newSlots;
  for (std::size_t i = 0; i < 2; ++i) {
    if (slotBytes[i].empty()) continue;
    bool kept = false;
    if (newChops && newPattern) {
      auto p = pattern::deserialize(slotBytes[i].data(), slotBytes[i].size());
      if (p.ok()) {
        pattern::Pattern clean = pattern::sanitize(p.value(), *newChops);
        if (pattern::flatten(clean, *newChops).ok()) {
          newSlots[i] = std::make_shared<const pattern::Pattern>(std::move(clean));
          kept = true;
        }
      }
    }
    if (!kept) newNotices.push_back({Notice::Level::Warning, std::string("Snapshot ") + (i == 0 ? "A" : "B") + " could not be restored and was dropped."});
  }

  // ---- commit ----
  loadedEmbedded_ = loadedFlag;
  if (newEvolve) {
    evolve_.start(*newEvolve);  // not enabled: only remembers the settings
  } else {
    evolve_.stop();
  }
  sourceData_ = std::move(newData);
  chopMap_ = std::move(newMap);
  chops_ = std::move(newChops);
  roles_ = std::move(newRoles.roles);
  rules_ = std::move(newRoles.rules);
  foreignModules_ = std::move(foreign);
  history_ = std::move(newHistory);
  pendingActivation_ = history::kNoNode;
  patterns_.reset();
  groove_ = groove::Groove{};  // a project without the Groove section plays straight; the decoder below restores a saved one
  grooveHistory_.reset(groove_);
  if (newPattern) patterns_.restore(std::move(*newPattern));
  patterns_.restoreSlots(std::move(newSlots));  // after restore(), which clears the slots
  extras_ = std::move(newExtras);
  sections_ = std::move(newSections);
  for (Notice& n : newNotices) notices_.push_back(std::move(n));
  publish(newPlayback ? newPlayback : std::make_shared<const render::Playback>());
  // Feature decoders run last; a failure drops only that section.
  for (auto it = sections_.begin(); it != sections_.end();) {
    auto dec = sectionDecoders_.find(it->first);
    if (dec != sectionDecoders_.end() && !dec->second.decode(it->second).ok()) {
      notice(Notice::Level::Warning, "Some " + dec->second.feature + " settings could not be restored.");
      it = sections_.erase(it);
    } else {
      ++it;
    }
  }
  return {};
}

void ProjectSession::reportSourceUnavailable(bool wrongAudio) {
  if (!chopMap_) return;
  notice(Notice::Level::Warning, wrongAudio ? wrongAudioText(chopMap_->info()) : notFoundText(chopMap_->info()));
}

void ProjectSession::reset() {
  evolve_.stop();
  clearSource();
  rules_ = roles::RuleSet{};
  extras_ = ProjectExtras{};
  sections_.clear();
  groove_ = groove::Groove{};
  grooveHistory_.reset(groove_);
  notices_.clear();
}

// ---------------------------------------------------------------------------------------------
// host parameter binding
// ---------------------------------------------------------------------------------------------

pattern::FxPassSettings fxSettingsFromParams(const host::ParamValues& v, std::uint64_t seed) {
  pattern::FxPassSettings s;
  s.seed = seed;
  auto amount = [&](host::ParamIndex allow, host::ParamIndex amt) { return v.get(allow) >= 0.5 ? v.get(amt) / 100.0 : 0.0; };
  s.echoAmount = amount(host::kAllowEcho, host::kEchoAmount);
  s.tremPanAmount = amount(host::kAllowTremolo, host::kTremAmount);
  s.stutterAmount = amount(host::kAllowStutter, host::kStutterAmount);
  s.rollAmount = amount(host::kAllowRoll, host::kRollAmount);
  s.stutterRate = static_cast<int>(std::lround(v.get(host::kStutterRate)));
  s.stutterGate = static_cast<int>(std::lround(v.get(host::kStutterGate)));
  s.stutterDecay = static_cast<int>(std::lround(v.get(host::kStutterDecayDb) * 10.0));
  s.stutterPitch = static_cast<int>(std::lround(v.get(host::kStutterPitch) * 10.0));
  s.rollRate = static_cast<int>(std::lround(v.get(host::kRollRate)));
  s.rollRateEnd = static_cast<int>(std::lround(v.get(host::kRollRateEnd)));
  s.rollFreeze = static_cast<int>(std::lround(v.get(host::kRollFreeze)));
  return s;
}

pattern::Settings settingsFromParams(const host::ParamValues& v, pattern::Settings base) {
  base.density = v.get(host::kDensity);
  base.swing = v.get(host::kSwing);
  base.bars = host::barsFromChoice(v.get(host::kPatternBars));
  base.allowReverse = v.get(host::kAllowReverse) >= 0.5;
  base.allowPitch = v.get(host::kAllowPitch) >= 0.5;
  base.allowRetrigger = v.get(host::kAllowRetrigger) >= 0.5;
  base.allowFilter = v.get(host::kAllowFilter) >= 0.5;
  base.allowGlide = v.get(host::kAllowGlide) >= 0.5;
  base.allowCrunch = v.get(host::kAllowCrunch) >= 0.5;
  base.fxIntensity = v.get(host::kFxIntensity);
  applyBreakParams(v, base, true);
  return applyChaos(base, v.get(host::kChaos));
}

double mutateAmountFromParams(const host::ParamValues& v) { return v.get(host::kVariationAmount); }

}  // namespace chopfractal::composition
