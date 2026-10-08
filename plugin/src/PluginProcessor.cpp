#include "PluginProcessor.h"

#include <cmath>
#include <cstring>

#include "PluginEditor.h"

namespace cf = chopfractal;

namespace {
constexpr char kStateMagic[4] = {'C', 'F', 'P', 'L'};
constexpr int kStateVersion = 1;
constexpr double kMaxSourceSeconds = 60.0;
constexpr juce::int64 kMaxDecodedBytes = 100ll * 1024 * 1024;
}  // namespace

ChopFractalProcessor::ChopFractalProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ChopFractal", createLayout()) {
  const auto& manifest = cf::host::parameterManifest();
  for (std::size_t i = 0; i < cf::host::kParamCount; ++i) paramPtrs_[i] = apvts.getRawParameterValue(manifest[i].id);
  renderer_.prepare(cf::render::Config{});
  session_.attachMailbox(&renderer_.mailbox());
  startTimerHz(30);
}

ChopFractalProcessor::~ChopFractalProcessor() { stopTimer(); }

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
}

bool ChopFractalProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  return cf::host::isSupportedBusLayout(layouts.getMainInputChannels(), layouts.getMainOutputChannels());
}

void ChopFractalProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
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
  const cf::host::TimeTranslation tt = cf::host::translateHostTime(ht, manualBpm.load(std::memory_order_relaxed));
  hostPlaying.store(tt.block.playing, std::memory_order_relaxed);
  usedFallbackTempo.store(tt.usedFallbackTempo, std::memory_order_relaxed);
  usedFallbackMeter.store(tt.usedFallbackMeter, std::memory_order_relaxed);

  cf::host::ParamValues pv;
  for (std::size_t i = 0; i < cf::host::kParamCount; ++i) pv.v[i] = static_cast<double>(paramPtrs_[i]->load(std::memory_order_relaxed));

  renderer_.process(tt.block, cf::host::toRenderParams(pv), buffer.getArrayOfReadPointers(), buffer.getArrayOfWritePointers(), channels, frames);

  // Tell the message thread when the playhead crosses the pattern's loop point (for quantized variation switches).
  const double quarters = patternQuarters_.load(std::memory_order_relaxed);
  if (tt.block.playing && tt.block.positionValid && quarters > 0.0) {
    const double end = tt.block.ppq + static_cast<double>(frames) * tt.block.bpm / (60.0 * sampleRate_.load(std::memory_order_relaxed));
    if (std::floor(tt.block.ppq / quarters) != std::floor(end / quarters)) boundaryFlag_.store(true, std::memory_order_relaxed);
  }
}

void ChopFractalProcessor::timerCallback() {
  if (boundaryFlag_.exchange(false)) withSession([](cf::composition::ProjectSession& s) { s.onLoopBoundary(); });
}

void ChopFractalProcessor::syncAudioInfo() {
  const auto pb = session_.playback();
  patternQuarters_.store((pb && !pb->passThrough && pb->source && pb->lengthTicks > 0) ? static_cast<double>(pb->lengthTicks) / static_cast<double>(cf::kTicksPerQuarter) : 0.0);
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
  for (int c = 0; c < channels; ++c)
    for (juce::int64 i = 0; i < length; ++i) {
      const float v = buffer.getSample(c, static_cast<int>(i));
      data->samples[static_cast<std::size_t>(c) * static_cast<std::size_t>(length) + static_cast<std::size_t>(i)] = std::isfinite(v) ? v : 0.f;
    }
  return data;
}

void ChopFractalProcessor::loadFileAsync(const juce::File& file) {
  setStatusMessage("Loading " + file.getFileName() + "...");
  juce::WeakReference<ChopFractalProcessor> self(this);
  juce::Thread::launch([self, file] {
    juce::String error;
    auto data = decodeFile(file, error);
    juce::MessageManager::callAsync([self, file, data, error] {
      if (self == nullptr) return;
      if (!data) {
        self->setStatusMessage(error);
        return;
      }
      cf::Status status;
      self->withSession([&](cf::composition::ProjectSession& s) {
        status = s.loadSource(data, file.getFileNameWithoutExtension().toStdString(), file.getFullPathName().toStdString());
      });
      self->setStatusMessage(status.ok() ? "Loaded " + file.getFileName() : juce::String(status.error().message));
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
  std::unique_ptr<juce::XmlElement> xml(apvts.copyState().createXml());
  out.writeString(xml ? xml->toString() : juce::String());

  std::vector<std::uint8_t> blob;
  {
    const juce::ScopedLock lock(sessionLock_);
    auto saved = session_.saveState(embedSource.load());
    if (!saved.ok()) saved = session_.saveState(false);  // e.g. the source is too large to embed: reference it instead
    if (saved.ok()) blob = std::move(saved.value());
  }
  out.writeInt(static_cast<int>(blob.size()));
  if (!blob.empty()) out.write(blob.data(), blob.size());
}

void ChopFractalProcessor::setStateInformation(const void* data, int sizeInBytes) {
  juce::MemoryInputStream in(data, static_cast<std::size_t>(juce::jmax(0, sizeInBytes)), false);
  char magic[4] = {};
  if (in.read(magic, 4) != 4 || std::memcmp(magic, kStateMagic, 4) != 0 || in.readInt() != kStateVersion) return;  // foreign or newer: ignore safely
  const juce::String xmlText = in.readString();
  const int blobSize = in.readInt();
  if (blobSize < 0 || static_cast<juce::int64>(blobSize) > in.getNumBytesRemaining()) return;
  std::vector<std::uint8_t> blob(static_cast<std::size_t>(blobSize));
  if (blobSize > 0 && in.read(blob.data(), blobSize) != blobSize) return;

  if (auto xml = juce::parseXML(xmlText))
    if (xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));

  if (blob.empty()) return;
  cf::Status status;
  {
    const juce::ScopedLock lock(sessionLock_);
    // Reference-only projects re-decode their file here, on the calling thread; embedded audio needs no file access.
    status = session_.loadState(blob.data(), blob.size(), [](const cf::source::SourceInfo& info) -> std::shared_ptr<const cf::render::SourceData> {
      if (info.path.empty()) return nullptr;
      juce::File f(juce::String::fromUTF8(info.path.c_str()));
      if (!f.existsAsFile()) return nullptr;
      juce::String ignored;
      return decodeFile(f, ignored);
    });
    syncAudioInfo();
  }
  // On failure the session is left exactly as it was (pass-through in a fresh instance); say why.
  setStatusMessage(status.ok() ? juce::String() : juce::String("Could not restore the project: ") + status.error().message);
}

juce::AudioProcessorEditor* ChopFractalProcessor::createEditor() { return new ChopFractalEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChopFractalProcessor(); }
