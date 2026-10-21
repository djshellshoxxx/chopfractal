#include "PluginProcessor.h"

#include <chopfractal/plugin_host_adapter/loop_position.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <chopfractal/plugin_ui_adapter/capture_view.hpp>
#include <chopfractal/plugin_ui_adapter/pads_view.hpp>

#include "PluginEditor.h"

namespace cf = chopfractal;

namespace {
constexpr char kStateMagic[4] = {'C', 'F', 'P', 'L'};
// Wrapper version: 2 only when the project session wrote schema 2 (lazy versioning, see project_session.hpp); a
// project that uses nothing new keeps writing 1 and stays readable by older builds. Both are read.
constexpr int kStateVersionV1 = 1;
constexpr int kStateVersionV2 = 2;
}  // namespace
static_assert(ChopFractalProcessor::kMaxSourceSeconds == cf::capture::kMaxCaptureSeconds, "capture and file decoding share the 60 s limit");
static_assert(ChopFractalProcessor::kMaxDecodedBytes == cf::capture::kMaxCaptureBytes, "capture and file decoding share the 100 MiB limit");

ChopFractalProcessor::ChopFractalProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ChopFractal", createLayout()) {
  const auto& manifest = cf::host::parameterManifest();
  for (std::size_t i = 0; i < cf::host::kParamCount; ++i) paramPtrs_[i] = apvts.getRawParameterValue(manifest[i].id);
  {
    const cf::host::ParamValues d = cf::host::ParamValues::defaults();
    lastGrooveParams_ = {d.get(cf::host::kGrooveEnable), d.get(cf::host::kGrooveTemplate), d.get(cf::host::kGrooveSwing), d.get(cf::host::kGrooveHumTime),
                         d.get(cf::host::kGrooveHumVel)};
  }
  renderer_.prepare(cf::render::Config{});
  session_.attachMailbox(&renderer_.mailbox());
  session_.attachPadMailbox(&renderer_.padMailbox());
  takeBuffer_.reserve(cf::composition::kMaxTakeHits);
  openHit_.fill(-1);
  startTimerHz(30);
}

ChopFractalProcessor::~ChopFractalProcessor() {
  stopTimer();
  exportProgress_.cancel.store(true);  // the worker polls this about every 0.7 s of rendered audio
  exportWorker_.reset();               // joins (at most 5 s)
  loadPool_.removeAllJobs(true, 15000);  // a decode in flight must not outlive the plugin's code
  restorePool_.removeAllJobs(true, 15000);
}

juce::AudioProcessorValueTreeState::ParameterLayout ChopFractalProcessor::createLayout() {
  using namespace cf::host;
  juce::AudioProcessorValueTreeState::ParameterLayout layout;
  for (const ParamDef& d : parameterManifest()) {
    const juce::ParameterID pid{d.id, d.sinceVersion};
    switch (d.kind) {
      case ParamKind::Float:
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            pid, d.name, juce::NormalisableRange<float>(static_cast<float>(d.minValue), static_cast<float>(d.maxValue)),
            static_cast<float>(d.defaultValue), juce::AudioParameterFloatAttributes().withLabel(d.unit)));
        break;
      case ParamKind::Bool:
        layout.add(std::make_unique<juce::AudioParameterBool>(pid, d.name, d.defaultValue >= 0.5));
        break;
      case ParamKind::Choice:
        layout.add(std::make_unique<juce::AudioParameterChoice>(pid, d.name, juce::StringArray::fromTokens(d.choices, "|", ""),
                                                                static_cast<int>(d.defaultValue)));
        break;
    }
  }
  return layout;
}

cf::host::ParamValues ChopFractalProcessor::currentParams() const {
  cf::host::ParamValues v = cf::host::ParamValues::defaults();
  for (std::size_t i = 0; i < cf::host::kParamCount; ++i)
    if (paramPtrs_[i]) v.set(static_cast<cf::host::ParamIndex>(i), static_cast<double>(paramPtrs_[i]->load()));
  return v;
}

void ChopFractalProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) {
  cf::render::Config cfg;
  cfg.sampleRate = sampleRate;
  cfg.maxBlock = maximumExpectedSamplesPerBlock > 0 ? maximumExpectedSamplesPerBlock : 2048;
  renderer_.prepare(cfg);
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  if (capture_.onPrepare(sampleRate_.load(), getTotalNumInputChannels())) setStatusMessage(cf::ui::kCaptureReconfiguredText);
}

void ChopFractalProcessor::setNonRealtime(bool isNonRealtime) noexcept {
  captureBlocked_.store(isNonRealtime);
  AudioProcessor::setNonRealtime(isNonRealtime);
}

bool ChopFractalProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  return cf::host::isSupportedBusLayout(layouts.getMainInputChannels(), layouts.getMainOutputChannels());
}

void ChopFractalProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals noDenormals;
  const int frames = buffer.getNumSamples();
  const int channels = juce::jmin(2, buffer.getNumChannels());
  if (frames <= 0 || channels < 1) return;

  cf::host::HostTimeInfo ht;
  if (auto* playHead = getPlayHead()) {
    if (auto pos = playHead->getPosition()) {
      ht.isPlaying = pos->getIsPlaying();
      ht.isLooping = pos->getIsLooping();
      if (auto bpm = pos->getBpm()) ht.bpm = *bpm;
      if (auto ppq = pos->getPpqPosition()) ht.ppqPosition = *ppq;
      if (auto ts = pos->getTimeSignature()) ht.timeSignature = cf::TimeSignature{ts->numerator, ts->denominator};
    }
  }
  const auto command = transportCommand_.load(std::memory_order_acquire);
  if (command != appliedTransportCommand_) {
    renderer_.reset();
    freeRunQuarters_ = 0.0;
    boundaryCount_.store(0, std::memory_order_relaxed);
    midpointCount_.store(0, std::memory_order_relaxed);
    appliedTransportCommand_ = command;
  }
  if (command != 0) {
    ht = {};  // preview uses the local tempo/clock, independent of a DAW's stopped transport
    ht.isPlaying = (command & 1u) != 0;
    ht.ppqPosition = freeRunQuarters_;
  }
  const cf::host::TimeTranslation tt = cf::host::translateHostTime(ht, manualBpm.load(std::memory_order_relaxed));
  hostPlaying.store(tt.block.playing, std::memory_order_relaxed);
  usedFallbackTempo.store(tt.usedFallbackTempo, std::memory_order_relaxed);
  usedFallbackMeter.store(tt.usedFallbackMeter, std::memory_order_relaxed);
  hostHasPosition.store(tt.block.positionValid, std::memory_order_relaxed);
  activeBpm.store(tt.block.bpm, std::memory_order_relaxed);
  hostBeatsPerBar.store(tt.timeSignature.numerator, std::memory_order_relaxed);
  hostBeatUnit.store(tt.timeSignature.denominator, std::memory_order_relaxed);

  cf::host::ParamValues pv;
  for (std::size_t i = 0; i < cf::host::kParamCount; ++i) pv.v[i] = paramPtrs_[i] ? static_cast<double>(paramPtrs_[i]->load(std::memory_order_relaxed)) : cf::host::parameterManifest()[i].defaultValue;

  // Capture reads the input before the renderer, whose output aliases it. Offline renders never record.
  if (!isNonRealtime()) {
    cf::capture::CaptureTransport ct;
    ct.playing = tt.block.playing;
    ct.positionValid = tt.block.positionValid;
    ct.ppq = tt.block.ppq;
    ct.bpm = tt.block.bpm;
    capture_.process(buffer.getArrayOfReadPointers(), juce::jmin(2, getTotalNumInputChannels(), buffer.getNumChannels()), frames, ct);
  }

  // Performance pads: on-screen presses first (frame 0), then the MIDI notes at their own samples. Nothing here allocates.
  int numPads = 0;
  const bool padsOn = pv.get(cf::host::kEffectEnable) >= 0.5;
  const bool recording = padsOn && recordArmed_.load(std::memory_order_relaxed) && tt.block.playing && tt.block.positionValid;
  const double ppqPerFrame = tt.block.bpm / (60.0 * sampleRate_.load(std::memory_order_relaxed));
  const auto addPad = [&](int frame, cf::render::PadEvent::Type type, int note, int velocity) {
    if (numPads >= cf::render::kMaxPadEventsPerBlock) {
      padInputDropped_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    cf::render::PadEvent& e = padEvents_[static_cast<std::size_t>(numPads++)];
    e.frame = frame;
    e.type = type;
    e.note = static_cast<std::uint8_t>(note);
    e.velocity = static_cast<std::uint8_t>(velocity);
    if (type == cf::render::PadEvent::Type::NoteOn) padActivity_[static_cast<std::size_t>(note >> 5)].fetch_or(1u << (note & 31), std::memory_order_relaxed);
    if (recording && type != cf::render::PadEvent::Type::AllNotesOff)
      if (!takeQueue_.tryPush(TakeEvent{tt.block.ppq + frame * ppqPerFrame, e.note, e.velocity, type == cf::render::PadEvent::Type::NoteOn}))
        takeDropped_.fetch_add(1, std::memory_order_relaxed);
  };
  {
    UiPad u;
    while (uiPadQueue_.tryPop(u))
      addPad(0, u.kind == 2 ? cf::render::PadEvent::Type::AllNotesOff : (u.kind == 1 ? cf::render::PadEvent::Type::NoteOn : cf::render::PadEvent::Type::NoteOff), u.note, u.velocity);
  }
  if (!midi.isEmpty()) {
    const int channel = padChannel_.load(std::memory_order_relaxed);
    for (const auto meta : midi) {
      const juce::MidiMessage msg = meta.getMessage();
      if (channel != 0 && msg.getChannel() != channel) continue;
      const int frame = juce::jlimit(0, frames - 1, meta.samplePosition);
      if (msg.isNoteOn(false) && msg.getVelocity() > 0) addPad(frame, cf::render::PadEvent::Type::NoteOn, msg.getNoteNumber(), msg.getVelocity());
      else if (msg.isNoteOff(true)) addPad(frame, cf::render::PadEvent::Type::NoteOff, msg.getNoteNumber(), 0);
      else if (msg.isAllNotesOff() || msg.isAllSoundOff()) addPad(frame, cf::render::PadEvent::Type::AllNotesOff, 0, 0);
    }
  }
  cf::render::LiveInput live;
  live.pads = padEvents_.data();
  live.numPads = numPads;
  live.padParams = cf::host::toPadParams(pv);
  renderer_.process(tt.block, cf::host::toRenderParams(pv, session_.matchGainDb()), buffer.getArrayOfReadPointers(), buffer.getArrayOfWritePointers(), channels, frames, live);

  // Tell the message thread when the playhead crosses the pattern's loop point (quantized variation switches)
  // or its midpoint (Evolve steps); the arithmetic lives in host::computeLoopPosition so it can be tested alone.
  // Hosts that give no position still run the renderer free-run, so the loop/midpoint events (variation
  // switches, Evolve) follow a local clock that stays aligned with the host position while there is one.
  cf::render::TransportBlock lpBlock = tt.block;
  const double sr = sampleRate_.load(std::memory_order_relaxed);
  const double bpmSafe = (std::isfinite(tt.block.bpm) && tt.block.bpm > 0.0) ? tt.block.bpm : 120.0;
  if (tt.block.playing && !tt.block.positionValid) {
    lpBlock.positionValid = true;
    lpBlock.ppq = freeRunQuarters_;
  }
  if (tt.block.playing) {
    const double start = lpBlock.positionValid ? lpBlock.ppq : freeRunQuarters_;
    freeRunQuarters_ = start + static_cast<double>(frames) * bpmSafe / (60.0 * sr);
  } else {
    freeRunQuarters_ = 0.0;
  }
  const cf::host::LoopPosition lp = cf::host::computeLoopPosition(lpBlock, frames, sr, patternQuarters_.load(std::memory_order_relaxed));
  // Counters, not flags: a fast offline bounce crosses several loop points between two timer ticks.
  if (lp.boundary) boundaryCount_.fetch_add(1, std::memory_order_relaxed);
  if (lp.midpoint) midpointCount_.fetch_add(1, std::memory_order_relaxed);
  playheadQuarters_.store(lp.playheadQuarters, std::memory_order_relaxed);
}

void ChopFractalProcessor::pollOutputStage() {
  const cf::host::ParamValues pv = currentParams();
  const cf::pattern::FxPassSettings fx = cf::composition::fxSettingsFromParams(pv, appliedFx_.seed);
  const cf::render::PlaybackOptions opt = cf::host::toPlaybackOptions(pv);
  const double bpm = std::round(activeBpm.load(std::memory_order_relaxed));
  withSession([&](cf::composition::ProjectSession& s) {
    if (!fxApplied_ || !(fx == appliedFx_) || opt.echoRate != appliedOptions_.echoRate || opt.echoDecay != appliedOptions_.echoDecay ||
        opt.echoTone != appliedOptions_.echoTone || opt.tremMode != appliedOptions_.tremMode || opt.tremRate != appliedOptions_.tremRate ||
        opt.tremDepth != appliedOptions_.tremDepth) {
      const cf::Status st = s.setFx(fx, opt);
      if (!st.ok()) setStatusMessage("Effects: " + juce::String(st.error().message));
      appliedFx_ = fx;
      appliedOptions_ = opt;
      fxApplied_ = true;
    }
    s.setRenderParams(cf::host::toRenderParams(pv));
    MatchKey key;
    key.playback = s.playback().get();
    key.on = pv.get(cf::host::kLevelMatch) >= 0.5;
    key.limiter = pv.get(cf::host::kLimiterEnable) >= 0.5;
    key.trim = pv.get(cf::host::kMatchTrimDb);
    key.ceiling = pv.get(cf::host::kLimiterCeilingDb);
    key.bpm = bpm;
    if (!(key == matchKey_)) {
      matchKey_ = key;
      auto m = s.updateLevelMatch(pv, bpm);
      if (!m.ok()) setStatusMessage("Level match: " + juce::String(m.error().message));
    }
  });
  const int latency = renderer_.latencyFrames();
  if (latency != reportedLatency_) {
    reportedLatency_ = latency;
    setLatencySamples(latency);
  }
}

void ChopFractalProcessor::pollAudioFlags() {
  pollOutputStage();
  pollCapture();
  {  // the project may have been loaded since the last poll: follow its MIDI channel
    int channel = 0;
    withSession([&channel](cf::composition::ProjectSession& s) { channel = s.padSettings().midiChannel; });
    padChannel_.store(channel, std::memory_order_relaxed);
  }
  {  // Groove Lab knobs: applied to the session (playback-time layer) only when one of them moved since the last poll
    const cf::host::ParamValues pv = currentParams();
    const std::array<double, 5> now = {pv.get(cf::host::kGrooveEnable), pv.get(cf::host::kGrooveTemplate), pv.get(cf::host::kGrooveSwing),
                                       pv.get(cf::host::kGrooveHumTime), pv.get(cf::host::kGrooveHumVel)};
    if (now != lastGrooveParams_) {
      lastGrooveParams_ = now;
      withSession([&pv](cf::composition::ProjectSession& s) { (void)s.setGroove(cf::composition::grooveFromParams(pv, s.groove())); });
    }
  }
  // A quantized switch can only complete when a pattern is playing; without one, apply it now.
  if (patternQuarters_.load(std::memory_order_relaxed) <= 0.0)
    withSession([](cf::composition::ProjectSession& s) {
      if (s.hasPendingActivation()) s.onLoopBoundary();
    });
  if (boundaryCount_.exchange(0) > 0) withSession([](cf::composition::ProjectSession& s) { s.onLoopBoundary(); });
  const std::uint32_t crossings = midpointCount_.exchange(0);
  pollPads(crossings > 0);  // before Evolve steps: the take is merged first, then the pattern evolves around it
  if (crossings) {
    // Evolve: the step lands mid-loop so the new pattern is in place before the next loop begins. Each loop
    // midpoint crossed since the last tick is one step (bounded, so a long stall cannot trigger a burst).
    const double chaos = currentParams().get(cf::host::kChaos);
    const std::uint32_t steps = std::min<std::uint32_t>(crossings, 16);
    withSession([this, chaos, steps](cf::composition::ProjectSession& s) {
      for (std::uint32_t i = 0; i < steps; ++i) {
        auto r = s.evolveStep(chaos);
        if (!r.ok()) {
          setStatusMessage("Evolve: " + juce::String(r.error().message));
          break;
        }
        if (r.value()) setStatusMessage("Evolve stepped");
      }
    });
  }
}

void ChopFractalProcessor::syncAudioInfo() {
  const auto pb = session_.playback();
  patternQuarters_.store((pb && !pb->passThrough && pb->source && pb->lengthTicks > 0) ? static_cast<double>(pb->lengthTicks) / static_cast<double>(cf::kTicksPerQuarter) : 0.0);
}

bool ChopFractalProcessor::startPlayback() {
  bool ready = false;
  withSession([&](cf::composition::ProjectSession& s) {
    const auto pb = s.playback();
    ready = pb && pb->source && !pb->passThrough && !pb->events.empty();
    if (!ready)
      setStatusMessage(!pb || !pb->source
          ? "Load a loop first, then Detect Chops and Generate before pressing Play."
          : "Press Detect Chops and Generate to create a pattern, then press Play.");
  });
  if (!ready) return false;
  const auto command = transportCommand_.load(std::memory_order_relaxed);
  transportCommand_.store((command + 2u) | 1u, std::memory_order_release);
  setStatusMessage("Playing pattern. Stop halts playback; Play restarts from the beginning.");
  return true;
}

void ChopFractalProcessor::stopPlayback() {
  const auto command = transportCommand_.load(std::memory_order_relaxed);
  transportCommand_.store((command + 2u) & ~std::uint64_t{1}, std::memory_order_release);
  setStatusMessage("Stopped. Press Play to restart the pattern.");
}

void ChopFractalProcessor::followHostTransport() {
  transportCommand_.store(0, std::memory_order_release);
  setStatusMessage("Following DAW transport. In the standalone app, use Play to start the pattern.");
}

void ChopFractalProcessor::audition(cf::ChopId chop) {
  withSession([&](cf::composition::ProjectSession& s) {
    auto req = s.auditionRequest(chop);
    if (req.ok()) renderer_.requestPreview(req.value());
  });
}

juce::String ChopFractalProcessor::statusMessage() const {
  const juce::ScopedLock lock(statusLock_);
  return status_;
}

void ChopFractalProcessor::setStatusMessage(const juce::String& message) {
  const juce::ScopedLock lock(statusLock_);
  status_ = message;
}

// ---------------------------------------------------------------------------------------------
// source decoding
// ---------------------------------------------------------------------------------------------

namespace {
thread_local int tDecodeCalls = 0;
}
int ChopFractalProcessor::decodeCallsOnThisThread() { return tDecodeCalls; }

std::shared_ptr<const cf::render::SourceData> ChopFractalProcessor::decodeFile(const juce::File& file, juce::String& error) {
  ++tDecodeCalls;
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
  if (!reader) {
    error = "Unsupported or unreadable audio file.";
    return nullptr;
  }
  const juce::int64 length = reader->lengthInSamples;
  const int channels = static_cast<int>(reader->numChannels);
  if (length <= 0) {
    error = "The file contains no audio.";
    return nullptr;
  }
  if (channels < 1 || channels > 2) {
    error = "Only mono and stereo files are supported.";
    return nullptr;
  }
  if (reader->sampleRate <= 0.0 || static_cast<double>(length) / reader->sampleRate > kMaxSourceSeconds ||
      length * channels * static_cast<juce::int64>(sizeof(float)) > kMaxDecodedBytes) {
    error = "The file is longer than the 60 second / 100 MB limit.";
    return nullptr;
  }
  juce::AudioBuffer<float> buffer(channels, static_cast<int>(length));
  if (!reader->read(&buffer, 0, static_cast<int>(length), 0, true, true)) {
    error = "The file could not be decoded.";
    return nullptr;
  }
  auto data = std::make_shared<cf::render::SourceData>();
  data->channels = channels;
  data->sampleRate = static_cast<int>(std::lround(reader->sampleRate));
  data->frames = length;
  data->samples.resize(static_cast<std::size_t>(channels) * static_cast<std::size_t>(length));
  for (int c = 0; c < channels; ++c) {
    const float* src = buffer.getReadPointer(c);
    float* dst = data->samples.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(length);
    for (juce::int64 i = 0; i < length; ++i) dst[i] = std::isfinite(src[i]) ? src[i] : 0.f;
  }
  return data;
}

void ChopFractalProcessor::loadFileAsync(const juce::File& file, bool relink) {
  if (!relink) clearUndoKeep();
  setStatusMessage("Loading " + file.getFileName() + "...");
  juce::WeakReference<ChopFractalProcessor> self(this);
  const std::uint64_t ticket = ++loadTicket_;
  loadPool_.addJob([self, file, relink, ticket] {
    juce::String error;
    auto data = decodeFile(file, error);
    juce::MessageManager::callAsync([self, file, data, error, relink, ticket] {
      if (self == nullptr || ticket != self->loadTicket_.load()) return;  // a newer request superseded this one
      if (!data) {
        self->setStatusMessage(error);
        return;
      }
      cf::Status status;
      self->withSession([&](cf::composition::ProjectSession& s) {
        status = relink ? s.relinkSource(data)
                        : s.loadSource(data, file.getFileNameWithoutExtension().toStdString(), file.getFullPathName().toStdString());
      });
      self->setStatusMessage(status.ok() ? (relink ? "Relinked " : "Loaded ") + file.getFileName() : juce::String(status.error().message));
    });
  });
}

// ---------------------------------------------------------------------------------------------
// project state: [magic][version][parameter XML][session blob]
// ---------------------------------------------------------------------------------------------

void ChopFractalProcessor::getStateInformation(juce::MemoryBlock& destData) {
  juce::ValueTree stateCopy = apvts.copyState();  // never mutate the live tree: hosts may call this off the message thread
  stateCopy.setProperty("manualBpm", manualBpm.load(), nullptr);
  stateCopy.setProperty("embedSource", embedSource.load(), nullptr);
  stateCopy.setProperty("engineParams", 1, nullptr);  // marks a project that knows the engine v2 parameters; absent = saved before they existed
  std::unique_ptr<juce::XmlElement> xml(stateCopy.createXml());

  std::vector<std::uint8_t> blob;
  bool v2 = false;
  {
    const juce::ScopedLock lock(sessionLock_);  // extras and A/B are captured in the same critical section as the rest: no torn state
    // A source with no file behind it (a kept capture, a stretched loop) must always travel inside the project.
    const bool pathless = session_.sourceNeedsEmbed();
    const bool embed = embedSource.load() || pathless;
    auto saved = session_.saveState(embed);
    if (!saved.ok() && embed) {
      const juce::String mb(static_cast<int>(session_.embeddedSourceBytes() / (1024 * 1024)));
      setStatusMessage(pathless ? "The captured audio is too large to embed (" + mb + " MB). Raise the embed limit or keep a shorter capture."
                                : "The audio is too large to embed (" + mb + " MB); the project references the file instead.");
      saved = session_.saveState(false);  // reference it instead (a pathless source is then saved without its audio)
    }
    if (saved.ok()) {
      blob = std::move(saved.value());
      v2 = session_.stateUsesSchemaV2();
    }
  }
  juce::MemoryOutputStream out(destData, false);
  out.write(kStateMagic, 4);
  out.writeInt(v2 ? kStateVersionV2 : kStateVersionV1);
  out.writeString(xml ? xml->toString() : juce::String());
  out.writeInt(static_cast<int>(blob.size()));
  if (!blob.empty()) out.write(blob.data(), blob.size());
}

void ChopFractalProcessor::setStateInformation(const void* data, int sizeInBytes) {
  clearUndoKeep();
  discardTake();  // a take waiting for a merge belongs to the project being replaced
  const std::uint64_t ticket = ++loadTicket_;  // a file load or audio restore still in flight must not replace the project being restored
  juce::MemoryInputStream in(data, static_cast<std::size_t>(juce::jmax(0, sizeInBytes)), false);
  char magic[4] = {};
  if (in.read(magic, 4) != 4 || std::memcmp(magic, kStateMagic, 4) != 0) return;  // not ours: ignore safely
  const int version = in.readInt();
  if (version != kStateVersionV1 && version != kStateVersionV2) {
    setStatusMessage("This project was saved by a different version of ChopFractal and could not be loaded.");
    return;
  }
  const juce::String xmlText = in.readString();
  const int blobSize = in.readInt();
  if (blobSize < 0 || static_cast<juce::int64>(blobSize) > in.getNumBytesRemaining()) return;
  std::vector<std::uint8_t> blob(static_cast<std::size_t>(blobSize));
  if (blobSize > 0 && in.read(blob.data(), blobSize) != blobSize) return;

  if (auto xml = juce::parseXML(xmlText))
    if (xml->hasTagName(apvts.state.getType())) {
      apvts.replaceState(juce::ValueTree::fromXml(*xml));
      if (!apvts.state.hasProperty("engineParams")) {  // a project saved before engine v2 keeps generating with the classic engine
        if (auto* ev = apvts.getParameter(cf::host::parameterManifest()[cf::host::kEngineVersion].id)) ev->setValueNotifyingHost(0.0f);
        if (auto* ge = apvts.getParameter(cf::host::parameterManifest()[cf::host::kGrooveEnable].id)) ge->setValueNotifyingHost(0.0f);
      }
      manualBpm = juce::jlimit(20.0, 999.0, static_cast<double>(apvts.state.getProperty("manualBpm", 120.0)));
      embedSource = static_cast<bool>(apvts.state.getProperty("embedSource", false));
    }

  if (blob.empty()) return;
  cf::Status status;
  bool needAudio = false;
  std::string path;
  cf::SourceId sourceId{};
  {
    const juce::ScopedLock lock(sessionLock_);
    // Never decode here: a reference-only project restores its audio on a worker (below), so the host stays responsive.
    cf::composition::LoadOptions options;
    options.deferSourceResolve = true;
    status = session_.loadState(blob.data(), blob.size(), {}, options);
    syncAudioInfo();
    if (status.ok() && session_.wasEmbedded()) embedSource = true;  // a re-save keeps the audio inside the project
    if (status.ok() && session_.sourceMissing()) {
      needAudio = true;
      path = session_.chopMap()->info().path;
      sourceId = session_.chopMap()->info().id;
    }
  }
  // On failure the session is left exactly as it was (pass-through in a fresh instance); say why.
  setStatusMessage(status.ok() ? juce::String() : juce::String("Could not restore the project: ") + status.error().message);
  if (needAudio) restoreAudio(path, sourceId, ticket);
}

// Re-attaches the audio of a reference-only project. The session is already usable (markers, pattern, the missing-source
// state); the result is applied only when no newer load, Locate Source or project replacement happened meanwhile.
void ChopFractalProcessor::restoreAudio(const std::string& path, cf::SourceId id, std::uint64_t ticket) {
  const bool network = path.rfind("\\\\", 0) == 0 || path.rfind("//", 0) == 0;  // never fetch a project's network path unprompted (Locate Source relinks)
  const juce::File file = path.empty() || network ? juce::File() : juce::File(juce::String::fromUTF8(path.c_str()));
  if (file == juce::File()) {
    juce::String text;
    withSession([&](cf::composition::ProjectSession& s) {
      s.reportSourceUnavailable(false);
      if (!s.notices().empty()) text = juce::String::fromUTF8(s.notices().back().text.c_str());
    });
    setStatusMessage(text);
    return;
  }
  setStatusMessage("Restoring audio from " + file.getFileName() + "...");
  ++restoresRunning_;
  juce::WeakReference<ChopFractalProcessor> self(this);
  restorePool_.addJob([self, file, id, ticket] {
    juce::String ignored;
    std::shared_ptr<const cf::render::SourceData> data;
    if (file.existsAsFile()) data = decodeFile(file, ignored);
    juce::MessageManager::callAsync([self, file, id, ticket, data] {
      if (self == nullptr) return;
      self->restoresRunning_--;
      if (ticket != self->loadTicket_.load()) return;  // superseded: a newer project, file load or Locate Source owns the session now
      juce::String text;
      self->withSession([&](cf::composition::ProjectSession& s) {
        if (!s.chopMap() || s.chopMap()->info().id != id || !s.sourceMissing()) return;  // a different project: drop the result
        auto note = [&](bool wrongAudio) {
          s.reportSourceUnavailable(wrongAudio);
          if (!s.notices().empty()) text = juce::String::fromUTF8(s.notices().back().text.c_str());
        };
        if (!data) {
          note(false);
        } else if (auto st = s.relinkSource(data); st.ok()) {
          text = "Restored audio: " + file.getFileName();
        } else if (st.error().code == cf::ErrorCode::Conflict) {
          note(true);
        } else {
          text = juce::String(st.error().message);
        }
      });
      if (text.isNotEmpty()) self->setStatusMessage(text);
    });
  });
}

// ---------------------------------------------------------------------------------------------
// input capture
// ---------------------------------------------------------------------------------------------

cf::Status ChopFractalProcessor::armCapture(cf::capture::CaptureMode mode, int bars, double maxSeconds) {
  auto refuse = [this](cf::ErrorCode code, const char* text) {
    setStatusMessage(text);
    return cf::Status(cf::makeError(code, text));
  };
  if (captureBlocked_.load()) return refuse(cf::ErrorCode::Conflict, cf::ui::kCaptureOfflineText);
  if (captureInputChannels() < 1) return refuse(cf::ErrorCode::InvalidArgument, cf::ui::kCaptureNoInputText);
  if (keepInFlight_.load()) return refuse(cf::ErrorCode::Conflict, "Wait for the capture to finish being kept.");
  if (capture_.status().state == cf::capture::CaptureState::Captured) discardCapture();
  cf::capture::ArmSettings s;
  s.mode = mode;
  s.channels = juce::jmin(2, captureInputChannels());
  s.sampleRate = sampleRate_.load();
  s.bars = bars;
  s.barQuarters = 4.0 * static_cast<double>(hostBeatsPerBar.load()) / static_cast<double>(juce::jmax(1, hostBeatUnit.load()));
  s.bpmHint = activeBpm.load();
  s.maxSeconds = maxSeconds;
  const cf::Status st = capture_.arm(s);
  if (!st.ok()) {
    setStatusMessage(juce::String::fromUTF8(st.error().message.c_str()));
    return st;
  }
  clearUndoKeep();  // a new capture ends the undo window of the previous Keep
  lastCaptureState_ = cf::capture::CaptureState::Armed;
  setStatusMessage({});
  return {};
}

cf::ui::CaptureUiInput ChopFractalProcessor::captureUiInput(cf::capture::CaptureMode mode, int bars, double maxSeconds) {
  cf::ui::CaptureUiInput in;
  in.status = capture_.status();
  in.mode = mode;
  in.bars = bars;
  in.maxSeconds = maxSeconds;
  in.bpm = activeBpm.load();
  in.barQuarters = 4.0 * static_cast<double>(hostBeatsPerBar.load()) / static_cast<double>(juce::jmax(1, hostBeatUnit.load()));
  in.sampleRate = sampleRate_.load();
  in.channels = juce::jmax(1, juce::jmin(2, captureInputChannels()));
  in.hostPlaying = hostPlaying.load();
  in.hostHasPosition = hostHasPosition.load();
  in.canUndoKeep = canUndoKeep_.load();
  in.keepInFlight = keepInFlight_.load();
  in.blocked = captureBlocked_.load();
  in.noInput = captureInputChannels() < 1;
  withSession([&](cf::composition::ProjectSession& s) {
    in.hasSource = s.hasSource();
    in.hasPattern = s.pattern() != nullptr;
  });
  return in;
}

void ChopFractalProcessor::pollCapture() {
  capture_.reap();
  const cf::capture::CaptureStatus st = capture_.status();
  if (st.state == lastCaptureState_) return;
  const bool ended = st.state == cf::capture::CaptureState::Captured || (st.state == cf::capture::CaptureState::Idle && st.ended == cf::capture::EndReason::Reconfigured);
  lastCaptureState_ = st.state;
  if (!ended) return;
  const double seconds = static_cast<double>(juce::jmin(st.framesCaptured, st.capacityFrames)) / juce::jmax(1.0, sampleRate_.load());
  const cf::ui::CaptureNotice n = cf::ui::captureEndNotice(st.ended, seconds);
  if (!n.text.empty()) setStatusMessage(juce::String::fromUTF8(n.text.c_str()));
}

void ChopFractalProcessor::clearUndoKeep() {
  keepUndo_.clear();
  undoToEmpty_ = false;
  canUndoKeep_.store(false);
}

void ChopFractalProcessor::keepCaptureAsync(bool chopAfter) {
  if (capture_.status().state != cf::capture::CaptureState::Captured) {
    setStatusMessage("There is no capture to keep.");
    return;
  }
  if (keepInFlight_.exchange(true)) return;
  // Undo Keep: snapshot the project and keep the old audio by pointer, before anything changes.
  clearUndoKeep();
  withSession([&](cf::composition::ProjectSession& s) {
    if (!s.hasSource()) {
      undoToEmpty_ = true;
    } else if (!keepUndo_.capture(s).ok()) {
      keepUndo_.clear();  // too large to snapshot: Keep still works, only the undo is unavailable
    }
  });
  preKeepEmbed_ = embedSource.load();
  setStatusMessage(cf::ui::kCaptureKeepingText);
  const std::uint64_t ticket = ++loadTicket_;
  juce::WeakReference<ChopFractalProcessor> self(this);
  loadPool_.addJob([self, this, chopAfter, ticket] {
    auto taken = capture_.take();  // the only O(n) work; the audio thread never touches a Captured buffer
    std::shared_ptr<const cf::render::SourceData> data;
    std::string name;
    bool wrapped = false;
    double seconds = 0.0;
    if (taken.ok()) {
      auto d = std::make_shared<cf::render::SourceData>();
      d->channels = taken.value().channels;
      d->sampleRate = taken.value().sampleRate;
      d->frames = taken.value().frames;
      d->samples = std::move(taken.value().samples);
      wrapped = taken.value().wrapped;
      seconds = static_cast<double>(d->frames) / juce::jmax(1, d->sampleRate);
      data = std::move(d);
      name = ("Capture " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M")).toStdString();
    }
    const bool failed = !taken.ok();
    juce::MessageManager::callAsync([self, ticket, data, name, chopAfter, wrapped, seconds, failed] {
      if (self != nullptr) self->finishKeep(ticket, data, name, chopAfter, wrapped, seconds, failed);
    });
  });
}

void ChopFractalProcessor::finishKeep(std::uint64_t ticket, std::shared_ptr<const cf::render::SourceData> data, std::string name, bool chopAfter, bool wrapped,
                                      double seconds, bool failed) {
  keepInFlight_.store(false);
  if (failed || !data) {
    clearUndoKeep();
    setStatusMessage("The capture could not be kept.");
    return;
  }
  if (ticket != loadTicket_.load()) {  // a project load replaced the session while the audio was moving: do not overwrite it
    clearUndoKeep();
    setStatusMessage("Keep cancelled: another project was loaded.");
    return;
  }
  cf::Status st;
  cf::SourceId id{};
  withSession([&](cf::composition::ProjectSession& s) {
    st = s.loadCapturedSource(data, name);
    if (st.ok()) {
      id = s.chopMap()->info().id;
      if (chopAfter) {
        auto setup = s.smartSetup(usedFallbackTempo.load() ? 0.0 : activeBpm.load());
        if (!setup.ok()) setStatusMessage(juce::String::fromUTF8(setup.error().message.c_str()));
      }
    }
  });
  if (!st.ok()) {
    clearUndoKeep();
    setStatusMessage(juce::String::fromUTF8(st.error().message.c_str()));
    return;
  }
  keptSourceId_ = id;
  embedSource = true;  // the captured audio has no file: it is saved inside the project
  canUndoKeep_.store(keepUndo_.available() || undoToEmpty_);
  juce::String text = cf::ui::kCaptureSavedInsideText;
  const std::string ring = cf::ui::captureKeptRingText(wrapped, seconds);
  if (!ring.empty()) text = juce::String::fromUTF8(ring.c_str()) + " " + text;
  setStatusMessage(text);
}

bool ChopFractalProcessor::undoKeep() {
  if (!canUndoKeep_.load()) return false;
  cf::Status st;
  withSession([&](cf::composition::ProjectSession& s) {
    if (!s.chopMap() || s.chopMap()->info().id != keptSourceId_) {
      st = cf::makeError(cf::ErrorCode::Conflict, "The kept capture is no longer the source.");
    } else if (undoToEmpty_) {
      s.clearSource();
    } else {
      st = keepUndo_.restore(s);
    }
  });
  if (!st.ok()) {
    clearUndoKeep();
    setStatusMessage(juce::String::fromUTF8(st.error().message.c_str()));
    return false;
  }
  embedSource = preKeepEmbed_;
  clearUndoKeep();
  setStatusMessage("Restored the source from before Keep.");
  return true;
}

// ---------------------------------------------------------------------------------------------
// export on a worker thread
// ---------------------------------------------------------------------------------------------

class ChopFractalProcessor::ExportWorker : public juce::Thread {
 public:
  ExportWorker(cf::composition::ExportJob job, cf::composition::ExportProgress& progress, juce::WeakReference<ChopFractalProcessor> owner)
      : juce::Thread("ChopFractal export"), job_(std::move(job)), progress_(progress), owner_(std::move(owner)) {}
  ~ExportWorker() override { stopThread(5000); }

  void run() override {
    cf::Result<cf::composition::ExportReport> result = job_.run(progress_);
    juce::WeakReference<ChopFractalProcessor> owner = owner_;
    juce::MessageManager::callAsync([owner, result] {
      if (owner != nullptr) owner->finishExport(result);
    });
  }

 private:
  cf::composition::ExportJob job_;
  cf::composition::ExportProgress& progress_;
  juce::WeakReference<ChopFractalProcessor> owner_;
};

cf::Status ChopFractalProcessor::startExport(const ExportRequest& request) {
  if (exportState_.load() == static_cast<int>(ExportStatus::State::Running)) return cf::makeError(cf::ErrorCode::Conflict, "An export is already running.");
  exportWorker_.reset();  // a finished worker from the previous export (joins immediately)
  cf::Result<cf::composition::ExportJob> job = cf::makeError(cf::ErrorCode::InvalidArgument, "no export");
  {
    const juce::ScopedLock lock(sessionLock_);  // plan under the lock; the job only holds immutable snapshots afterwards
    job = request.kind == ExportRequest::Kind::Wav ? cf::composition::ExportJob::planWav(session_, request.path, request.wav)
                                                   : cf::composition::ExportJob::planKit(session_, request.path, request.kit);
  }
  if (!job.ok()) return job.error();
  exportProgress_.fraction.store(0.f);
  exportProgress_.cancel.store(false);
  {
    const juce::ScopedLock lock(exportLock_);
    exportName_ = juce::String::fromUTF8(job.value().displayName().c_str());
    exportKind_ = request.kind;
  }
  exportState_.store(static_cast<int>(ExportStatus::State::Running));
  setStatusMessage("Exporting " + exportDisplayName() + "...");
  exportWorker_ = std::make_unique<ExportWorker>(std::move(job.value()), exportProgress_, juce::WeakReference<ChopFractalProcessor>(this));
  exportWorker_->startThread();
  return {};
}

void ChopFractalProcessor::cancelExport() { exportProgress_.cancel.store(true); }

ExportStatus ChopFractalProcessor::exportStatus() const {
  ExportStatus st;
  st.state = static_cast<ExportStatus::State>(exportState_.load());
  st.fraction = exportProgress_.fraction.load();
  return st;
}

juce::String ChopFractalProcessor::exportDisplayName() const {
  const juce::ScopedLock lock(exportLock_);
  return exportName_;
}

void ChopFractalProcessor::finishExport(const cf::Result<cf::composition::ExportReport>& result) {
  ExportRequest::Kind kind;
  {
    const juce::ScopedLock lock(exportLock_);
    kind = exportKind_;
  }
  const juce::String name = exportDisplayName();
  ExportStatus::State state;
  juce::String text;
  if (result.ok()) {
    state = ExportStatus::State::Done;
    const auto& rep = result.value();
    text = kind == ExportRequest::Kind::Kit
               ? "Producer Kit written to " + name + " (" + juce::String(static_cast<int>(rep.files.size())) + " files)"
               : "Exported " + name + (rep.clipped ? " (" + juce::String(static_cast<juce::int64>(rep.clipped)) + " samples clipped; lower the output gain)" : juce::String());
  } else if (result.error().code == cf::ErrorCode::Cancelled) {
    state = ExportStatus::State::Cancelled;
    text = "Export cancelled.";
  } else {
    state = ExportStatus::State::Failed;
    text = juce::String::fromUTF8(result.error().message.c_str());
  }
  setStatusMessage(text);
  exportState_.store(static_cast<int>(state));
}

juce::AudioProcessorEditor* ChopFractalProcessor::createEditor() { return new ChopFractalEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChopFractalProcessor(); }

// ---------------------------------------------------------------------------------------------
// performance pads (F07)
// ---------------------------------------------------------------------------------------------

void ChopFractalProcessor::padTrigger(int note, int velocity, bool on) {
  if (note < 0 || note > 127) return;
  UiPad u;
  u.note = static_cast<std::uint8_t>(note);
  u.velocity = static_cast<std::uint8_t>(juce::jlimit(1, 127, velocity));
  u.kind = on ? 1 : 0;
  if (!uiPadQueue_.tryPush(u)) padInputDropped_.fetch_add(1, std::memory_order_relaxed);
}

void ChopFractalProcessor::padAllNotesOff() {
  UiPad u;
  u.kind = 2;
  if (!uiPadQueue_.tryPush(u)) padInputDropped_.fetch_add(1, std::memory_order_relaxed);
}

cf::Status ChopFractalProcessor::setPadSettings(const cf::composition::PadSettings& settings) {
  cf::Status st;
  withSession([&](cf::composition::ProjectSession& s) { st = s.setPadSettings(settings); });
  if (st.ok()) padChannel_.store(settings.midiChannel, std::memory_order_relaxed);
  else setStatusMessage(juce::String(st.error().message));
  return st;
}

cf::Status ChopFractalProcessor::setChokeGroup(cf::ChopId chop, std::uint8_t group) {
  cf::Status st;
  withSession([&](cf::composition::ProjectSession& s) { st = s.setChokeGroup(chop, group); });
  if (!st.ok()) setStatusMessage(juce::String(st.error().message));
  return st;
}

cf::composition::PadSettings ChopFractalProcessor::padSettings() {
  cf::composition::PadSettings out;
  withSession([&](cf::composition::ProjectSession& s) { out = s.padSettings(); });
  return out;
}

std::array<std::uint32_t, 4> ChopFractalProcessor::takePadActivity() {
  std::array<std::uint32_t, 4> out{};
  for (std::size_t i = 0; i < out.size(); ++i) out[i] = padActivity_[i].exchange(0, std::memory_order_relaxed);
  return out;
}

void ChopFractalProcessor::setRecordArmed(bool armed) {
  if (!armed) {
    recordArmed_.store(false);  // the next poll merges what was played
    return;
  }
  bool ready = false;
  withSession([&](cf::composition::ProjectSession& s) { ready = s.pattern() != nullptr && s.chops() && !s.chops()->chops.empty(); });
  if (!ready) {
    setStatusMessage(cf::ui::kPadNoPatternText);
    return;
  }
  recordArmed_.store(true);
  const bool noPosition = hostPlaying.load() && !hostHasPosition.load();
  setStatusMessage(noPosition ? cf::ui::kPadNoPositionText : cf::ui::kPadArmedText);
}

void ChopFractalProcessor::discardTake() {
  TakeEvent e;
  while (takeQueue_.tryPop(e)) {
  }
  takeBuffer_.clear();
  openHit_.fill(-1);
  takeCount_.store(0);
  takeFullReported_ = false;
}

void ChopFractalProcessor::pollPads(bool midpointCrossed) {
  TakeEvent e;
  while (takeQueue_.tryPop(e)) {
    if (e.note > 127) continue;
    if (e.on) {
      if (takeBuffer_.size() >= cf::composition::kMaxTakeHits) {
        if (!takeFullReported_) {
          takeFullReported_ = true;
          setStatusMessage(cf::ui::kPadTakeFullText);
        }
        continue;
      }
      cf::composition::TakeHit h;
      h.ppqOn = e.ppq;
      h.note = e.note;
      h.velocity = e.velocity;
      takeBuffer_.push_back(h);
      openHit_[e.note] = static_cast<int>(takeBuffer_.size()) - 1;
    } else {
      const int i = openHit_[e.note];
      if (i >= 0 && i < static_cast<int>(takeBuffer_.size())) takeBuffer_[static_cast<std::size_t>(i)].ppqOff = e.ppq;
      openHit_[e.note] = -1;
    }
  }
  takeCount_.store(static_cast<int>(takeBuffer_.size()));
  const bool playing = hostPlaying.load();
  const bool stopped = wasPlayingForTake_ && !playing;
  wasPlayingForTake_ = playing;
  if (!takeBuffer_.empty() && (midpointCrossed || stopped || !recordArmed_.load())) mergePendingTake();
}

void ChopFractalProcessor::mergePendingTake() {
  const std::vector<cf::composition::TakeHit> hits = takeBuffer_;
  discardTake();
  const cf::host::ParamValues pv = currentParams();
  withSession([&](cf::composition::ProjectSession& s) {
    const cf::composition::PadSettings ps = s.padSettings();
    cf::composition::TakeOptions opt;
    opt.quantizeDivision = ps.quantizeDivision;
    opt.triplet = ps.quantizeTriplet;
    opt.merge = ps.merge;
    opt.baseNote = ps.baseNote;
    const cf::render::PadParams pp = cf::host::toPadParams(pv);
    opt.curve = pp.curve;
    opt.mode = pp.mode;
    auto r = s.mergeTake(hits, opt);
    if (!r.ok()) {
      setStatusMessage(r.error().code == cf::ErrorCode::InvalidArgument ? juce::String(cf::ui::kPadNoPatternText) : juce::String(r.error().message));
      return;
    }
    cf::ui::PadTakeCounts c;
    c.added = r.value().added;
    c.skippedLocked = r.value().skippedLocked;
    c.skippedCap = r.value().skippedCap;
    c.skippedUnmapped = r.value().skippedUnmapped;
    c.skippedDuplicate = r.value().skippedDuplicate;
    c.eventCap = s.pattern() ? s.pattern()->settings.maxEvents : c.eventCap;
    const std::string text = cf::ui::padTakeNotice(c);
    if (!text.empty()) setStatusMessage(text);
  });
}
