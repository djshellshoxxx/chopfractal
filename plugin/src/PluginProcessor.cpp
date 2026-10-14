#include "PluginProcessor.h"

#include "Log.h"

#include <chopfractal/plugin_host_adapter/loop_position.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "PluginEditor.h"

namespace cf = chopfractal;

static_assert(ChopFractalProcessor::kMaxSourceSeconds == chopfractal::capture::kMaxCaptureSeconds, "capture and import share one length limit");
static_assert(ChopFractalProcessor::kMaxDecodedBytes == chopfractal::capture::kMaxCaptureBytes, "capture and import share one size limit");

namespace {
constexpr char kStateMagic[4] = {'C', 'F', 'P', 'L'};
constexpr int kStateVersion = 1;
}  // namespace

// Main stereo in/out plus eight optional stereo outputs ("Out 2".."Out 9"), disabled until the host turns them on.
ChopFractalProcessor::BusesProperties ChopFractalProcessor::makeBusLayout() {
  auto p = BusesProperties()
               .withInput("Input", juce::AudioChannelSet::stereo(), true)
               .withOutput("Main", juce::AudioChannelSet::stereo(), true);
  for (int i = 2; i <= 9; ++i) p = p.withOutput("Out " + juce::String(i), juce::AudioChannelSet::stereo(), false);
  return p;
}

ChopFractalProcessor::ChopFractalProcessor()
    : AudioProcessor(makeBusLayout()),
      apvts(*this, nullptr, "ChopFractal", createLayout()) {
  const auto& manifest = cf::host::parameterManifest();
  for (std::size_t i = 0; i < cf::host::kParamCount; ++i) paramPtrs_[i] = apvts.getRawParameterValue(manifest[i].id);
  renderer_.prepare(cf::render::Config{});
  session_.attachMailbox(&renderer_.mailbox());
  session_.attachArmMailbox(&renderer_.armMailbox());
  session_.attachPadMailbox(&renderer_.padMailbox());
  startTimerHz(30);
  cflog::init();
}

ChopFractalProcessor::~ChopFractalProcessor() {
  stopTimer();
  stretch_.cancel();  // a stretch in flight must stop before the pool is drained
  loadPool_.removeAllJobs(true, 15000);  // a decode in flight must not outlive the plugin's code
  cflog::shutdown();
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
  int aux = 0;
  for (int i = 1; i <= cf::render::kMaxAuxBuses; ++i)
    if (auto* bus = getBus(false, i); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() == 2 && getMainBusNumInputChannels() == 2) ++aux;
  cfg.auxBuses = aux;
  auxBuses_.store(aux, std::memory_order_relaxed);
  renderer_.prepare(cfg);
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  capture_.engine().onPrepare(sampleRate_.load(), juce::jmin(2, getTotalNumInputChannels()));
}

bool ChopFractalProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  int aux[cf::render::kMaxAuxBuses] = {};
  for (int i = 1; i <= cf::render::kMaxAuxBuses; ++i) aux[i - 1] = i < layouts.outputBuses.size() ? layouts.getNumChannels(false, i) : 0;
  return cf::host::isSupportedBusLayout(layouts.getMainInputChannels(), layouts.getMainOutputChannels(), aux, cf::render::kMaxAuxBuses);
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
  activeBpm.store(tt.block.bpm, std::memory_order_relaxed);
  hostBeatsPerBar.store(tt.timeSignature.numerator, std::memory_order_relaxed);
  hostBeatUnit.store(tt.timeSignature.denominator, std::memory_order_relaxed);

  cf::host::ParamValues pv;
  for (std::size_t i = 0; i < cf::host::kParamCount; ++i) pv.v[i] = paramPtrs_[i] ? static_cast<double>(paramPtrs_[i]->load(std::memory_order_relaxed)) : cf::host::parameterManifest()[i].defaultValue;

  // Capture reads the input before the renderer overwrites it (the output buffer aliases the input).
  if (!isNonRealtime()) {
    cf::capture::CaptureTransport ct;
    ct.playing = tt.block.playing;
    ct.positionValid = tt.block.positionValid;
    ct.ppq = tt.block.ppq;
    ct.bpm = tt.block.bpm;
    capture_.engine().process(buffer.getArrayOfReadPointers(), juce::jmin(channels, getTotalNumInputChannels()), frames, ct);
  }

  // Live pads: MIDI notes and on-screen presses, plus the take being recorded when armed and the host reports a position.
  cf::render::LiveInput live;
  {
    const bool armed = pads_.recordArmed();
    if (armed && tt.block.playing && !tt.block.positionValid) pads_.noPositionFlag();
    const double recordPpq = (armed && tt.block.playing && tt.block.positionValid) ? tt.block.ppq : -1.0;
    live.pads = pads_.collect(midi, frames, recordPpq, tt.block.bpm, sampleRate_.load(std::memory_order_relaxed), live.numPads);
    live.padParams.mode = static_cast<cf::render::PadMode>(juce::jlimit(0, 2, juce::roundToInt(pv.v[cf::host::kPerfMode])));
    live.padParams.curve = static_cast<cf::render::VelocityCurve>(juce::jlimit(0, 3, juce::roundToInt(pv.v[cf::host::kPerfVelocityCurve])));
  }
  // Aux outputs: each enabled stereo bus is a view onto the host buffer (no allocation); disabled ones fold into main.
  cf::render::AuxOutputs aux;
  float* auxPairs[cf::render::kMaxAuxBuses][2] = {};
  if (channels == 2) {
    for (int i = 1; i <= cf::render::kMaxAuxBuses && aux.count < auxBuses_.load(std::memory_order_relaxed); ++i) {
      auto* bus = getBus(false, i);
      if (!bus || !bus->isEnabled() || bus->getNumberOfChannels() != 2) continue;
      const int first = getChannelIndexInProcessBlockBuffer(false, i, 0);
      if (first < 0 || first + 1 >= buffer.getNumChannels()) continue;
      auxPairs[aux.count][0] = buffer.getWritePointer(first);
      auxPairs[aux.count][1] = buffer.getWritePointer(first + 1);
      aux.bus[aux.count] = auxPairs[aux.count];
      ++aux.count;
    }
  }
  renderer_.process(tt.block, cf::host::toRenderParams(pv), live, buffer.getArrayOfReadPointers(), buffer.getArrayOfWritePointers(), channels, frames, aux);

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

void ChopFractalProcessor::pollAudioFlags() {
  capture_.engine().reap();
  // A quantized switch can only complete when a pattern is playing; without one, apply it now.
  if (patternQuarters_.load(std::memory_order_relaxed) <= 0.0)
    withSession([](cf::composition::ProjectSession& s) {
      if (s.hasPendingActivation()) s.onLoopBoundary();
    });
  const bool playingNow = hostPlaying.load(std::memory_order_relaxed);
  if (playingNow != sceneTransportPlaying_) {
    sceneTransportPlaying_ = playingNow;
    withSession([playingNow](cf::composition::ProjectSession& s) { s.onTransportChanged(playingNow); });
  }
  if (const std::uint32_t crossings = boundaryCount_.exchange(0); crossings > 0) {
    // Every boundary counts for scene sequencing; the renderer reports the latest armed switch it applied.
    withSession([this, crossings](cf::composition::ProjectSession& s) {
      s.onLoopBoundary();
      for (std::uint32_t i = 0; i < std::min<std::uint32_t>(crossings, 8); ++i) s.onSceneBoundary(renderer_.appliedSwitchToken());
    });
  }
  pads_.pollTake();
  if (pads_.takeBufferWasFull()) setStatusMessage("Take buffer full (4096 hits); extra hits were ignored.");
  if (pads_.noPositionWhileArmed()) setStatusMessage("Recording needs the host to report its song position.");
  if (!playingNow && pads_.waitingHits() > 0) flushTake();  // the transport stopped: commit what was played
  const std::uint32_t midpoints = midpointCount_.load(std::memory_order_relaxed);
  if (midpoints > 0 && pads_.waitingHits() > 0) flushTake();  // hits land at the loop midpoint so they are not re-triggered in the same pass
  if (const std::uint32_t crossings = midpointCount_.exchange(0)) {
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
  pads_.setChannel(session_.padSettings().midiChannel);
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

std::shared_ptr<const cf::render::SourceData> ChopFractalProcessor::decodeFile(const juce::File& file, juce::String& error) {
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
  if (reader->sampleRate <= 0.0 || static_cast<double>(length) / reader->sampleRate > ChopFractalProcessor::kMaxSourceSeconds ||
      length * channels * static_cast<juce::int64>(sizeof(float)) > ChopFractalProcessor::kMaxDecodedBytes) {
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
  stretch_.clearUndo();
  captureController().clearUndo();
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
  juce::MemoryOutputStream out(destData, false);
  out.write(kStateMagic, 4);
  out.writeInt(kStateVersion);
  juce::ValueTree stateCopy = apvts.copyState();  // never mutate the live tree: hosts may call this off the message thread
  stateCopy.setProperty("manualBpm", manualBpm.load(), nullptr);
  stateCopy.setProperty("embedSource", embedSource.load(), nullptr);
  std::unique_ptr<juce::XmlElement> xml(stateCopy.createXml());
  out.writeString(xml ? xml->toString() : juce::String());

  std::vector<std::uint8_t> blob;
  {
    const juce::ScopedLock lock(sessionLock_);
    const bool embed = embedSource.load() || session_.sourceNeedsEmbed();  // pathless audio has no file to reload
    auto saved = session_.saveState(embed);
    if (!saved.ok() && embed && session_.sourceNeedsEmbed()) {
      setStatusMessage("The captured audio is too large to embed (" + juce::String(static_cast<int>(session_.embeddedSourceBytes() / (1024 * 1024))) + " MB). Raise the embed limit or keep a shorter capture.");
      saved = session_.saveState(false);
    } else if (!saved.ok() && embed) {
      setStatusMessage("The audio is too large to embed (" + juce::String(static_cast<int>(session_.embeddedSourceBytes() / (1024 * 1024))) + " MB); the project references the file instead.");
      saved = session_.saveState(false);  // reference it instead
    }
    if (saved.ok()) blob = std::move(saved.value());
  }
  out.writeInt(static_cast<int>(blob.size()));
  if (!blob.empty()) out.write(blob.data(), blob.size());
}

void ChopFractalProcessor::setStateInformation(const void* data, int sizeInBytes) {
  stretch_.clearUndo();
  ++loadTicket_;  // a file load still in flight must not replace the project being restored
  juce::MemoryInputStream in(data, static_cast<std::size_t>(juce::jmax(0, sizeInBytes)), false);
  char magic[4] = {};
  if (in.read(magic, 4) != 4 || std::memcmp(magic, kStateMagic, 4) != 0) return;  // not ours: ignore safely
  if (in.readInt() != kStateVersion) {
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
      manualBpm = juce::jlimit(20.0, 999.0, static_cast<double>(apvts.state.getProperty("manualBpm", 120.0)));
      embedSource = static_cast<bool>(apvts.state.getProperty("embedSource", false));
    }

  if (blob.empty()) return;
  cf::Status status;
  {
    const juce::ScopedLock lock(sessionLock_);
    // Reference-only projects re-decode their file here, on the calling thread; embedded audio needs no file access.
    status = session_.loadState(blob.data(), blob.size(), [](const cf::source::SourceInfo& info) -> std::shared_ptr<const cf::render::SourceData> {
      if (info.path.empty()) return nullptr;
      if (info.path.rfind("\\\\", 0) == 0 || info.path.rfind("//", 0) == 0) return nullptr;  // never fetch a project's network path unprompted (Locate Source relinks)
      juce::File f(juce::String::fromUTF8(info.path.c_str()));
      if (!f.existsAsFile()) return nullptr;
      juce::String ignored;
      return decodeFile(f, ignored);
    });
    syncAudioInfo();
    if (status.ok() && session_.wasEmbedded()) embedSource = true;  // a re-save keeps the audio inside the project
  }
  // On failure the session is left exactly as it was (pass-through in a fresh instance); say why.
  setStatusMessage(status.ok() ? juce::String() : juce::String("Could not restore the project: ") + status.error().message);
}

juce::AudioProcessorEditor* ChopFractalProcessor::createEditor() { return new ChopFractalEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChopFractalProcessor(); }

// ---------------------------------------------------------------------------------------------
// recording pad hits into the pattern
// ---------------------------------------------------------------------------------------------

void ChopFractalProcessor::setRecordArmed(bool armed) {
  pads_.setRecordArmed(armed);
  if (armed) setStatusMessage("Recording armed: start the transport and play the pads.");
  else flushTake();
}

void ChopFractalProcessor::discardTake() { pads_.discardTake(); }

void ChopFractalProcessor::flushTake(const char*) {
  auto hits = pads_.takeHits();
  if (hits.empty()) return;
  cf::Result<cf::composition::TakeReport> r = cf::makeError(cf::ErrorCode::InvalidArgument, "nothing recorded");
  const cf::host::ParamValues pv = currentParams();
  withSession([&](cf::composition::ProjectSession& s) {
    cf::composition::TakeOptions o;
    const auto& ps = s.padSettings();
    o.quantizeDivision = ps.quantizeDivision;
    o.triplet = ps.quantizeTriplet;
    o.merge = ps.merge;
    o.baseNote = ps.baseNote;
    o.mode = static_cast<cf::render::PadMode>(juce::jlimit(0, 2, juce::roundToInt(pv.get(cf::host::kPerfMode))));
    o.curve = static_cast<cf::render::VelocityCurve>(juce::jlimit(0, 3, juce::roundToInt(pv.get(cf::host::kPerfVelocityCurve))));
    r = s.mergeTake(hits, o);
  });
  if (!r.ok()) {
    setStatusMessage(r.error().message);
    return;
  }
  const auto& t = r.value();
  juce::String msg = "Recorded " + juce::String(t.added) + (t.added == 1 ? " hit." : " hits.");
  juce::StringArray skipped;
  if (t.skippedLocked) skipped.add(juce::String(t.skippedLocked) + " skipped: their beat is locked.");
  if (t.skippedCap) skipped.add(juce::String(t.skippedCap) + " skipped: the event cap (512) is reached.");
  if (t.skippedUnmapped) skipped.add(juce::String(t.skippedUnmapped) + " skipped: they matched no chop.");
  if (skipped.size() > 0) msg += " " + skipped.joinIntoString(" ");
  setStatusMessage(msg);
}
