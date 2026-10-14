#include "StretchController.h"

#include "PluginProcessor.h"

namespace cf = chopfractal;

juce::String StretchController::start(const cf::composition::StretchRequest& request) {
  if (running_.load()) return "A stretch is already running.";
  cf::Result<cf::composition::StretchPlan> plan = cf::makeError(cf::ErrorCode::InvalidArgument, "load a source first");
  std::vector<std::uint8_t> before;
  std::shared_ptr<const cf::render::SourceData> beforeSource;
  owner_.withSession([&](cf::composition::ProjectSession& s) {
    plan = s.planStretch(request);
    if (!plan.ok()) return;
    auto saved = s.saveState(true);
    if (!saved.ok()) {  // too large to embed: restore from the audio buffer instead
      saved = s.saveState(false);
      beforeSource = s.sourceData();
    }
    if (saved.ok()) before = std::move(saved.value());
  });
  if (!plan.ok()) return plan.error().message;
  preState_ = std::move(before);
  preSource_ = std::move(beforeSource);
  canUndo_ = false;
  running_ = true;
  cancel_ = false;
  progress_ = 0.f;
  owner_.setStatusMessage("Stretching... 0%");
  juce::WeakReference<ChopFractalProcessor> self(&owner_);
  auto shared = std::make_shared<cf::composition::StretchPlan>(std::move(plan.value()));
  owner_.workerPool().addJob([self, shared] {
    if (self == nullptr) return;
    StretchController& c = self->stretchController();
    const auto& in = *shared->input;
    std::vector<const float*> channels;
    for (int ch = 0; ch < in.channels; ++ch) channels.push_back(in.channel(ch));
    cf::stretch::Input si;
    si.channels = channels.data();
    si.numChannels = in.channels;
    si.frames = in.frames;
    si.sampleRate = in.sampleRate;
    auto out = cf::stretch::stretch(si, shared->plan, shared->request.quality, [&c, self](double f) {
      c.progress_ = static_cast<float>(f);
      return !c.cancel_.load();
    });
    std::shared_ptr<cf::render::SourceData> data;
    juce::String error;
    if (out.ok()) {
      data = std::make_shared<cf::render::SourceData>();
      data->channels = in.channels;
      data->sampleRate = in.sampleRate;
      data->frames = shared->plan.outFrames;
      data->samples = std::move(out.value());
    } else {
      error = out.error().message;
    }
    juce::MessageManager::callAsync([self, shared, data, error] {
      if (self == nullptr) return;
      StretchController& cc = self->stretchController();
      cc.running_ = false;
      if (!data) {
        self->setStatusMessage(error.isNotEmpty() ? error : juce::String("Stretching cancelled; nothing was changed."));
        return;
      }
      cf::Status st;
      juce::String note;
      self->withSession([&](cf::composition::ProjectSession& s) {
        st = s.applyStretched(*shared, data);
        for (const auto& n : s.takeNotices()) note = juce::String(n.text);
      });
      if (!st.ok()) {
        self->setStatusMessage(st.error().message);
        return;
      }
      self->embedSource = true;  // a stretched source has no file: it is saved inside the project
      cc.canUndo_ = !cc.preState_.empty();
      self->setStatusMessage(note.isNotEmpty() ? note + " The stretched audio is saved inside the project." : juce::String("The stretched audio is saved inside the project."));
    });
  });
  return {};
}

void StretchController::clearUndo() {
  canUndo_ = false;
  preState_.clear();
  preSource_.reset();
}

bool StretchController::undo() {
  if (!canUndo_.load() || running_.load()) return false;
  cf::Status st;
  const auto source = preSource_;
  owner_.withSession([&](cf::composition::ProjectSession& s) {
    st = s.loadState(preState_.data(), preState_.size(), [source](const cf::source::SourceInfo&) { return source; });
  });
  if (!st.ok()) {
    owner_.setStatusMessage(st.error().message);
    return false;
  }
  clearUndo();
  owner_.setStatusMessage("Restored the source from before the stretch.");
  return true;
}
