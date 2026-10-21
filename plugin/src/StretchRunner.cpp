#include "StretchRunner.h"

#include <chopfractal/composition/project_session.hpp>

namespace cf = chopfractal;

class StretchRunner::Worker : public juce::Thread {
 public:
  Worker(cf::composition::StretchPlan p, std::atomic<float>& progress) : juce::Thread("ChopFractal stretch"), plan(std::move(p)), progress_(progress) {}
  ~Worker() override { stopThread(10000); }
  void run() override {
    result = cf::composition::runStretch(plan, [this](double f) {
      progress_.store(static_cast<float>(f));
      return !threadShouldExit() && !cancelled.load();
    });
    done.store(true);
  }
  cf::composition::StretchPlan plan;
  std::atomic<bool> cancelled{false}, done{false};
  std::optional<cf::Result<std::shared_ptr<const cf::render::SourceData>>> result;

 private:
  std::atomic<float>& progress_;
};

StretchRunner::StretchRunner(ChopFractalProcessor& p) : proc_(p) {}

StretchRunner::~StretchRunner() {
  if (worker_) worker_->cancelled.store(true);
  worker_.reset();  // joins
}

cf::Status StretchRunner::start(const cf::composition::StretchRequest& request) {
  if (worker_) return cf::makeError(cf::ErrorCode::Conflict, "A stretch is already running.");
  cf::Result<cf::composition::StretchPlan> planned = cf::makeError(cf::ErrorCode::NotFound, "Load a loop first.");
  proc_.withSession([&](cf::composition::ProjectSession& s) { planned = s.planStretch(request); });
  if (!planned.ok()) return cf::Status{planned.error()};
  progress_.store(0.f);
  worker_ = std::make_unique<Worker>(std::move(planned.value()), progress_);
  worker_->startThread();
  return cf::Status{};
}

void StretchRunner::cancel() {
  if (worker_) worker_->cancelled.store(true);
}

StretchRunner::Outcome StretchRunner::poll(juce::String& message) {
  if (!worker_ || !worker_->done.load()) return Outcome::None;
  std::unique_ptr<Worker> w = std::move(worker_);
  w->stopThread(10000);
  const bool wasCancelled = w->cancelled.load();
  if (!w->result || !w->result->ok()) {
    if (wasCancelled || (w->result && w->result->error().code == cf::ErrorCode::Cancelled)) {
      message = "Stretching cancelled; nothing was changed.";
      return Outcome::Cancelled;
    }
    message = w->result ? juce::String(w->result->error().message) : juce::String("Stretching failed.");
    return Outcome::Failed;
  }
  if (wasCancelled) {  // cancelled after the last block: honour it
    message = "Stretching cancelled; nothing was changed.";
    return Outcome::Cancelled;
  }
  cf::Status applied;
  juce::String notice;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    undo_.capture(s);
    applied = s.applyStretched(w->plan, w->result->value());
    if (!applied.ok()) undo_.clear();
    for (const auto& n : s.takeNotices()) notice = juce::String(n.text);
    if (applied.ok()) {
      appliedData_ = s.sourceData();
      appliedHistorySize_ = s.history().size();
      appliedActive_ = static_cast<std::uint64_t>(s.history().active());
    }
  });
  if (!applied.ok()) {
    message = juce::String(applied.error().message);
    return Outcome::Failed;
  }
  proc_.clearUndoKeep();  // the source changed: an older Undo Keep would restore a stale project
  message = notice + " The stretched audio is saved inside the project.";
  return Outcome::Applied;
}

bool StretchRunner::undoAvailable() {
  if (!undo_.available()) return false;
  bool same = false;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    same = s.sourceData() == appliedData_ && s.history().size() == appliedHistorySize_ && static_cast<std::uint64_t>(s.history().active()) == appliedActive_;
  });
  if (!same) undo_.clear();
  return same;
}

cf::Status StretchRunner::undo() {
  if (!undoAvailable()) return cf::makeError(cf::ErrorCode::NotFound, "Nothing to undo.");
  cf::Status st;
  proc_.withSession([&](cf::composition::ProjectSession& s) { st = undo_.restore(s); });
  appliedData_.reset();
  return st;
}
