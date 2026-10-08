#include <chopfractal/chop_contracts/bytes.hpp>
#include <chopfractal/composition/project_session.hpp>

#include <algorithm>
#include <cstring>

namespace chopfractal::composition {
namespace {

// The dependency-free modules (state_codec, variation_history) own private Error types with the same
// shape; the composition root is where they are translated into the shared one.
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
      if (e.child->locked) lockedPath = true;
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

}  // namespace

struct ProjectSession::Target {
  const Event* event = nullptr;
  const Event* top = nullptr;
  int depth = 0;
  SampleRange bounds;
  bool lockedPath = false;
};

ProjectSession::ProjectSession() : history_(history::Config{}) {}

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
  auto flat = pattern::flatten(p, *chops_);
  if (!flat.ok()) return flat.error();
  if (!sourceData_) return {};  // source missing: pattern authoring continues without audio
  auto pb = render::makePlayback(sourceData_, flat.value(), pattern::lengthTicks(p));
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
  Status v = installPlayback(result.value());  // validated before the audio side ever sees it
  if (!v.ok()) return v;
  patterns_.adopt(std::move(result.value()));
  if (report) noteReport(*report);
  if (historyLabel) recordHistory(historyLabel);
  return {};
}

void ProjectSession::recordHistory(const std::string& label) {
  if (!patterns_.hasPattern()) return;
  const pattern::Pattern& p = patterns_.current();
  history::Metadata meta;
  meta.seed = p.settings.seed;
  meta.label = label;
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
  info.name = std::move(name);
  info.channels = data->channels;
  info.sampleRate = data->sampleRate;
  info.frames = data->frames;
  info.path = std::move(path);
  auto map = source::ChopMap::create(std::move(info));
  if (!map.ok()) return map.error();  // nothing has changed yet
  sourceData_ = std::move(data);
  chopMap_ = std::move(map.value());
  chops_ = chopMap_->snapshot();
  roles_ = roles::RoleMap{};
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
  Status s = edit(*chopMap_);
  if (!s.ok()) return s;
  return reconcileAfterMarkerChange();
}

Status ProjectSession::undoMarkers() {
  if (!chopMap_ || !chopMap_->undo()) return makeError(ErrorCode::NotFound, "nothing to undo");
  return reconcileAfterMarkerChange();
}

Status ProjectSession::redoMarkers() {
  if (!chopMap_ || !chopMap_->redo()) return makeError(ErrorCode::NotFound, "nothing to redo");
  return reconcileAfterMarkerChange();
}

Status ProjectSession::reconcileAfterMarkerChange() {
  chops_ = chopMap_->snapshot();
  std::vector<ChopId> ids;
  for (const ChopInfo& c : chops_->chops) ids.push_back(c.id);
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
  auto r = pattern::generate(*chops_, settings, policy.get(), &report);
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
    cur.pitchRange = live->pitchRange;
    cur.maxRetrigger = live->maxRetrigger;
    cur.maxShiftTicks = live->maxShiftTicks;
    cur.maxEvents = live->maxEvents;
    Status vs = pattern::validateSettings(cur);
    if (!vs.ok()) return vs;
  }
  auto policy = makePolicy();
  pattern::GenerationReport report;
  auto r = pattern::mutate(base, *chops_, options, policy.get(), &report);
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

bool ProjectSession::undo() {
  if (!patterns_.undo()) return false;
  Status s = republish();
  if (!s.ok()) notice(Notice::Level::Error, "Could not publish the restored pattern: " + s.error().message);
  return true;
}

bool ProjectSession::redo() {
  if (!patterns_.redo()) return false;
  Status s = republish();
  if (!s.ok()) notice(Notice::Level::Error, "Could not publish the restored pattern: " + s.error().message);
  return true;
}

Status ProjectSession::recallSnapshot(std::size_t slot) {
  Status s = patterns_.recallSnapshot(slot);
  if (!s.ok()) return s;
  return republish();
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
            out.lockedPath = lockedPath || e.child->locked;
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

Status ProjectSession::zoomIn(EventId id, const zoom::Settings& zs, std::uint64_t seed) {
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  const pattern::Pattern& p = patterns_.current();
  Target t;
  Status s = chooseTarget(p, id, t);
  if (!s.ok()) return s;
  if (t.lockedPath || t.event->locked) return makeError(ErrorCode::Blocked, "this hit (or a parent) is locked; unlock it first");
  auto ctx = zoomContext(p, t);
  if (!ctx.ok()) return ctx.error();
  Event parent = *t.event;
  parent.child.reset();
  auto child = zoom::createChildPattern(parent, ctx.value(), zs, seed);
  if (!child.ok()) return child.error();
  return finalize(withChild(p, t, child.value()), "Zoom");
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
  return finalize(withChild(p, t, child.value()), "Mutate children");
}

Status ProjectSession::collapse(EventId id) {
  if (!patterns_.hasPattern()) return makeError(ErrorCode::InvalidArgument, "generate a pattern first");
  const pattern::Pattern& p = patterns_.current();
  Target t;
  Status s = chooseTarget(p, id, t);
  if (!s.ok()) return s;
  if (t.lockedPath || t.event->locked) return makeError(ErrorCode::Blocked, "this hit (or a parent) is locked; unlock it first");
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

Result<std::vector<std::uint8_t>> ProjectSession::saveState(bool embedSource) const {
  codec::ProjectState st;
  if (chopMap_) st.modules[kModSource] = {source::kSchemaVersion, chopMap_->serialize()};
  if (patterns_.hasPattern()) st.modules[kModPattern] = {pattern::kSchemaVersion, pattern::serialize(patterns_.current())};
  st.modules[kModRoles] = {roles::kSchemaVersion, roles::serialize(roles_, rules_)};
  st.modules[kModHistory] = {history::kSchemaVersion, history_.serialize()};
  const bool embedded = embedSource && sourceData_;
  if (embedded) {
    if (embeddedSourceBytes() > kMaxEmbeddedSourceBytes)
      return makeError(ErrorCode::LimitExceeded, "the source is too large to embed in the project", static_cast<std::int64_t>(kMaxEmbeddedSourceBytes));
    std::vector<std::uint8_t> payload;
    bytes::Writer w(payload);
    w.i32(sourceData_->channels);
    w.i32(sourceData_->sampleRate);
    w.i64(sourceData_->frames);
    for (float f : sourceData_->samples) w.f32(f);
    st.modules[kModAudio] = {1, std::move(payload)};
  }
  std::vector<std::uint8_t> meta;
  bytes::Writer mw(meta);
  mw.boolean(embedded);
  st.modules[kModSession] = {kSessionSchemaVersion, std::move(meta)};
  auto enc = codec::encode(st);
  if (!enc.ok()) return convert(enc.error());
  return enc.value();
}

Status ProjectSession::loadState(const std::uint8_t* data, std::size_t size, const SourceResolver& resolver) {
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

  std::vector<std::uint8_t> srcBytes, patBytes, roleBytes, histBytes, audioBytes, sessionBytes;
  bool hasSrc = false, hasPat = false, hasRoles = false, hasHist = false, hasAudio = false, hasSession = false;
  for (Status s : {take(kModSource, source::kSchemaVersion, srcBytes, hasSrc), take(kModPattern, pattern::kSchemaVersion, patBytes, hasPat),
                   take(kModRoles, roles::kSchemaVersion, roleBytes, hasRoles), take(kModHistory, history::kSchemaVersion, histBytes, hasHist),
                   take(kModAudio, 1, audioBytes, hasAudio), take(kModSession, kSessionSchemaVersion, sessionBytes, hasSession)})
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
    } else if (resolver) {
      auto found = resolver(info);
      if (found && validSource(found) && idOf(*found) == info.id && found->frames == info.frames) {
        newData = std::move(found);
      } else if (found) {
        newNotices.push_back({Notice::Level::Warning, "The file for \"" + info.name + "\" is not the audio this project was saved with. Markers and pattern are kept; relink the original file."});
      }
    }
    if (!newData && newNotices.empty())
      newNotices.push_back({Notice::Level::Warning, "The source \"" + info.name + "\" was not found" + (info.path.empty() ? "" : " at " + info.path) +
                                                        ". Markers and pattern are kept; relink the file to hear playback."});
  }

  roles::State newRoles;
  if (hasRoles) {
    auto r = roles::deserialize(roleBytes.data(), roleBytes.size());
    if (!r.ok()) return r.error();
    newRoles = std::move(r.value());
  }
  if (newChops) {
    std::vector<ChopId> ids;
    for (const ChopInfo& c : newChops->chops) ids.push_back(c.id);
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
    auto flat = pattern::flatten(p.value(), *newChops);  // the pattern must fit the saved chops
    if (!flat.ok()) return makeError(ErrorCode::Corrupt, "the saved pattern does not fit the saved chops: " + flat.error().message);
    if (newData) {
      auto pb = render::makePlayback(newData, flat.value(), pattern::lengthTicks(p.value()));
      if (!pb.ok()) return makeError(ErrorCode::Corrupt, "the saved pattern cannot be played: " + pb.error().message);
      newPlayback = pb.value();
    }
    newPattern = std::move(p.value());
  } else if (newData) {
    auto pt = render::makePassThroughPlayback(newData);
    if (!pt.ok()) return pt.error();
    newPlayback = pt.value();
  }

  // ---- commit ----
  sourceData_ = std::move(newData);
  chopMap_ = std::move(newMap);
  chops_ = std::move(newChops);
  roles_ = std::move(newRoles.roles);
  rules_ = std::move(newRoles.rules);
  history_ = std::move(newHistory);
  pendingActivation_ = history::kNoNode;
  patterns_.reset();
  if (newPattern) patterns_.restore(std::move(*newPattern));
  for (Notice& n : newNotices) notices_.push_back(std::move(n));
  publish(newPlayback ? newPlayback : std::make_shared<const render::Playback>());
  return {};
}

void ProjectSession::reset() {
  clearSource();
  rules_ = roles::RuleSet{};
  notices_.clear();
}

// ---------------------------------------------------------------------------------------------
// host parameter binding
// ---------------------------------------------------------------------------------------------

pattern::Settings settingsFromParams(const host::ParamValues& v, pattern::Settings base) {
  base.density = v.get(host::kDensity);
  base.swing = v.get(host::kSwing);
  base.bars = host::barsFromChoice(v.get(host::kPatternBars));
  base.allowReverse = v.get(host::kAllowReverse) >= 0.5;
  base.allowPitch = v.get(host::kAllowPitch) >= 0.5;
  base.allowRetrigger = v.get(host::kAllowRetrigger) >= 0.5;
  return base;
}

double mutateAmountFromParams(const host::ParamValues& v) { return v.get(host::kVariationAmount); }

}  // namespace chopfractal::composition
