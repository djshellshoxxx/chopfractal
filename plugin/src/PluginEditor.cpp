#include "PluginEditor.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <cmath>

namespace cf = chopfractal;

namespace {
juce::Colour laneColour(int chopIndex) {
  return juce::Colour::fromHSV(static_cast<float>(((chopIndex % 8) + 8) % 8) / 8.0f, 0.55f, 0.85f, 1.0f);
}
}  // namespace

ChopFractalEditor::ChopFractalEditor(ChopFractalProcessor& processor) : AudioProcessorEditor(&processor), proc_(processor) {
  setSize(940, 680);
  setResizable(false, false);

  for (juce::Button* b : std::initializer_list<juce::Button*>{&load_, &detect_, &generate_, &mutate_, &undo_, &redo_, &zoom_, &collapse_, &lock_, &dice_}) {
    addAndMakeVisible(*b);
    b->setTitle(b->getButtonText());
  }
  load_.onClick = [this] { chooseFile(); };
  detect_.onClick = [this] {
    run([](cf::composition::ProjectSession& s) { return s.applyChops(cf::composition::DetectOptions{}); });
  };
  generate_.onClick = [this] { doGenerate(); };
  mutate_.onClick = [this] { doMutate(); };
  undo_.onClick = [this] {
    run([](cf::composition::ProjectSession& s) { return s.undo() ? cf::Status{} : cf::Status{cf::makeError(cf::ErrorCode::NotFound, "Nothing to undo.")}; });
  };
  redo_.onClick = [this] {
    run([](cf::composition::ProjectSession& s) { return s.redo() ? cf::Status{} : cf::Status{cf::makeError(cf::ErrorCode::NotFound, "Nothing to redo.")}; });
  };
  zoom_.onClick = [this] {
    const cf::EventId id = selected_;
    const std::uint64_t seed = currentSeed() + 1;
    setSeed(seed);
    run([=](cf::composition::ProjectSession& s) {
      if (!id.valid()) return cf::Status{cf::makeError(cf::ErrorCode::InvalidArgument, "Select a hit in the pattern first.")};
      cf::zoom::Settings zs;
      zs.subdivisions = 4;
      zs.density = cf::zoom::Density::Balanced;
      return s.zoomIn(id, zs, seed);
    });
  };
  collapse_.onClick = [this] {
    const cf::EventId id = selected_;
    run([=](cf::composition::ProjectSession& s) {
      if (!id.valid()) return cf::Status{cf::makeError(cf::ErrorCode::InvalidArgument, "Select a hit in the pattern first.")};
      return s.collapse(id);
    });
  };
  lock_.onClick = [this] { doToggleLock(); };
  dice_.onClick = [this] { setSeed(juce::Random::getSystemRandom().nextInt64() & 0x7fffffff); };

  embed_.setToggleState(proc_.embedSource.load(), juce::dontSendNotification);
  embed_.onClick = [this] { proc_.embedSource = embed_.getToggleState(); };
  addAndMakeVisible(embed_);

  seedLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(seedLabel_);
  seed_.setText("1");
  seed_.setInputRestrictions(12, "0123456789");
  seed_.setTitle("Seed");
  addAndMakeVisible(seed_);

  addSlider(density_, "density", "Density");
  addSlider(swing_, "swing", "Swing");
  addSlider(variation_, "variation_amount", "Variation");
  addSlider(dry_, "dry_mix", "Dry mix");
  addSlider(gain_, "output_gain_db", "Output gain (dB)");

  bars_.addItemList(juce::StringArray::fromTokens(cf::host::parameterManifest()[cf::host::kPatternBars].choices, "|", ""), 1);
  bars_.setTitle("Pattern length in bars");
  addAndMakeVisible(bars_);
  barsAttachment_ = std::make_unique<Attachment::ComboBoxAttachment>(proc_.apvts, "pattern_bars", bars_);
  barsLabel_.attachToComponent(&bars_, true);

  addToggle(enable_, "effect_enable", "Effect on");
  addToggle(reverse_, "allow_reverse", "Reverse");
  addToggle(pitch_, "allow_pitch", "Pitch");
  addToggle(retrigger_, "allow_retrigger", "Retrigger");

  refresh(true);
  startTimerHz(10);
}

ChopFractalEditor::~ChopFractalEditor() { stopTimer(); }

void ChopFractalEditor::addSlider(ParamSlider& s, const char* paramId, const juce::String& title) {
  s.slider.setSliderStyle(juce::Slider::LinearHorizontal);
  s.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 18);
  s.slider.setNumDecimalPlacesToDisplay(2);
  s.slider.setTitle(title);
  addAndMakeVisible(s.slider);
  s.label.setText(title, juce::dontSendNotification);
  s.label.attachToComponent(&s.slider, true);
  s.attachment = std::make_unique<Attachment::SliderAttachment>(proc_.apvts, paramId, s.slider);
}

void ChopFractalEditor::addToggle(ParamToggle& t, const char* paramId, const juce::String& title) {
  t.button.setButtonText(title);
  t.button.setTitle(title);
  addAndMakeVisible(t.button);
  t.attachment = std::make_unique<Attachment::ButtonAttachment>(proc_.apvts, paramId, t.button);
}

void ChopFractalEditor::resized() {
  auto area = getLocalBounds().reduced(10);
  auto row1 = area.removeFromTop(30);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&load_, &detect_, &generate_, &mutate_, &undo_, &redo_, &zoom_, &collapse_, &lock_})
    b->setBounds(row1.removeFromLeft(96).reduced(2));
  area.removeFromTop(6);
  auto row2 = area.removeFromTop(26);
  seedLabel_.setBounds(row2.removeFromLeft(50));
  seed_.setBounds(row2.removeFromLeft(110).reduced(2));
  dice_.setBounds(row2.removeFromLeft(90).reduced(2));
  embed_.setBounds(row2.removeFromLeft(210).reduced(2));
  bars_.setBounds(row2.removeFromRight(90).reduced(2));
  area.removeFromTop(6);
  auto sliders = area.removeFromTop(78);
  auto left = sliders.removeFromLeft(sliders.getWidth() / 2);
  density_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  swing_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  variation_.slider.setBounds(left.removeFromTop(24).withTrimmedLeft(110));
  dry_.slider.setBounds(sliders.removeFromTop(24).withTrimmedLeft(130));
  gain_.slider.setBounds(sliders.removeFromTop(24).withTrimmedLeft(130));
  auto toggles = sliders.removeFromTop(24);
  for (juce::Button* b : std::initializer_list<juce::Button*>{&enable_.button, &reverse_.button, &pitch_.button, &retrigger_.button}) b->setBounds(toggles.removeFromLeft(100));
  area.removeFromTop(6);
  waveArea_ = area.removeFromTop(120);
  area.removeFromTop(6);
  patternArea_ = area.removeFromTop(230);
  area.removeFromTop(6);
  historyArea_ = area.removeFromTop(area.getHeight() - 26);
}

// ---------------------------------------------------------------------------------------------
// state and actions
// ---------------------------------------------------------------------------------------------

std::uint64_t ChopFractalEditor::currentSeed() const { return static_cast<std::uint64_t>(seed_.getText().getLargeIntValue()); }
void ChopFractalEditor::setSeed(std::uint64_t seed) { seed_.setText(juce::String(static_cast<juce::int64>(seed)), juce::dontSendNotification); }

void ChopFractalEditor::run(const std::function<cf::Status(cf::composition::ProjectSession&)>& action) {
  cf::Status status;
  juce::String notice;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    status = action(s);
    for (const auto& n : s.takeNotices()) notice = juce::String(n.text);
  });
  status_ = status.ok() ? notice : juce::String(status.error().message);
  proc_.setStatusMessage(status_);
  refresh(true);
}

void ChopFractalEditor::chooseFile() {
  chooser_ = std::make_unique<juce::FileChooser>("Choose a loop (WAV or AIFF, up to 60 s)", juce::File(), "*.wav;*.aif;*.aiff");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
    const juce::File f = fc.getResult();
    if (f.existsAsFile()) proc_.loadFileAsync(f);
  });
}

void ChopFractalEditor::doGenerate() {
  const cf::host::ParamValues params = proc_.currentParams();
  cf::pattern::Settings base;
  base.seed = currentSeed();
  const double v = params.get(cf::host::kVariationAmount);
  base.variation = {v, v, v, v};
  cf::pattern::Settings s = cf::composition::settingsFromParams(params, base);
  s.maxShiftTicks = std::min<cf::Ticks>(20, cf::gridTicks(s.grid) / 8);
  run([=](cf::composition::ProjectSession& session) { return session.generate(s); });
}

void ChopFractalEditor::doMutate() {
  const cf::host::ParamValues params = proc_.currentParams();
  const std::uint64_t seed = currentSeed() + 1;
  setSeed(seed);
  run([=](cf::composition::ProjectSession& session) {
    if (!session.pattern()) return cf::Status{cf::makeError(cf::ErrorCode::InvalidArgument, "Generate a pattern first.")};
    cf::pattern::Settings live = session.pattern()->settings;  // length, meter and grid stay; the automatable controls refresh
    live.density = params.get(cf::host::kDensity);
    live.swing = params.get(cf::host::kSwing);
    live.allowReverse = params.get(cf::host::kAllowReverse) >= 0.5;
    live.allowPitch = params.get(cf::host::kAllowPitch) >= 0.5;
    live.allowRetrigger = params.get(cf::host::kAllowRetrigger) >= 0.5;
    cf::pattern::MutateOptions opt;
    opt.seed = seed;
    opt.amount = cf::composition::mutateAmountFromParams(params);
    return session.mutate(opt, &live);
  });
}

int ChopFractalEditor::selectedBar() const {
  for (const auto& r : pattern_.rects)
    if (r.id == selected_) return r.bar;
  return 0;
}

void ChopFractalEditor::doToggleLock() {
  const int bar = selectedBar();
  run([=](cf::composition::ProjectSession& s) {
    return s.edit([=](const cf::pattern::Pattern& p, const cf::ChopSnapshot&) {
      const bool now = bar < static_cast<int>(p.bars.size()) && p.bars[static_cast<std::size_t>(bar)].locked;
      return cf::pattern::setLock(p, {cf::ScopeLevel::Bar, bar, 0, {}}, !now);
    });
  });
}

void ChopFractalEditor::refresh(bool force) {
  juce::String sig;
  proc_.withSession([&](cf::composition::ProjectSession& s) {
    sig << juce::String::toHexString(reinterpret_cast<std::uintptr_t>(s.playback().get())) << "|" << static_cast<int>(s.history().size()) << "|"
        << static_cast<juce::int64>(s.history().active()) << "|" << (s.chopMap() ? static_cast<int>(s.chopMap()->markers().size()) : -1);
    if (!force && sig == signature_) return;
    signature_ = sig;
    sourceFrames_ = 0;
    markers_.clear();
    chops_.clear();
    pattern_ = {};
    tree_.clear();
    const auto pb = s.playback();
    if (s.chopMap()) sourceFrames_ = s.chopMap()->info().frames;
    if (pb && pb->source && peaks_.min.empty()) {
      const auto ptrs = std::vector<const float*>{pb->source->channel(0), pb->source->channel(pb->source->channels - 1)};
      peaks_ = cf::ui::computePeaks(ptrs.data(), pb->source->channels, pb->source->frames, {0, pb->source->frames}, std::max(1, waveArea_.getWidth()));
    }
    if (!pb || !pb->source) peaks_ = {};
    if (s.chopMap()) {
      markers_ = cf::ui::buildMarkerViews(*s.chopMap(), cf::ui::ViewWindow{0, std::max<std::int64_t>(1, sourceFrames_), static_cast<double>(waveArea_.getWidth())});
      if (s.chops()) chops_ = s.chops()->chops;
      if (s.pattern() && s.chops()) {
        pattern_ = cf::ui::buildPatternView(*s.pattern(), *s.chops(), [&](cf::ChopId id) { return s.roleMap().roleOf(id); });
      }
    }
    tree_ = cf::ui::layoutHistoryTree(s.history().listAll(), s.history().active());
  });
  if (force || sig != signature_) repaint();
  else repaint();
}

// ---------------------------------------------------------------------------------------------
// drawing and mouse
// ---------------------------------------------------------------------------------------------

void ChopFractalEditor::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(0xff15181d));
  g.setColour(juce::Colours::white.withAlpha(0.85f));
  g.setFont(13.0f);

  // Source waveform with chop markers.
  g.setColour(juce::Colour(0xff20252d));
  g.fillRect(waveArea_);
  if (!peaks_.min.empty()) {
    g.setColour(juce::Colour(0xff6fb1ff));
    const int n = static_cast<int>(peaks_.min.size());
    const float cy = static_cast<float>(waveArea_.getCentreY());
    const float h = static_cast<float>(waveArea_.getHeight()) * 0.45f;
    for (int i = 0; i < n; ++i)
      g.drawVerticalLine(waveArea_.getX() + i, cy - peaks_.max[static_cast<std::size_t>(i)] * h, cy - peaks_.min[static_cast<std::size_t>(i)] * h);
  } else {
    g.setColour(juce::Colours::grey);
    g.drawText("Load a loop (WAV or AIFF). Until a pattern is generated, the input passes through unchanged.", waveArea_, juce::Justification::centred);
  }
  int markerNumber = 0;
  for (const auto& m : markers_) {
    ++markerNumber;
    g.setColour(m.enabled ? juce::Colour(0xffffc857) : juce::Colours::grey);
    const int x = waveArea_.getX() + static_cast<int>(m.x);
    g.drawVerticalLine(x, static_cast<float>(waveArea_.getY()), static_cast<float>(waveArea_.getBottom()));
    g.drawText(juce::String(markerNumber), x + 2, waveArea_.getY(), 28, 14, juce::Justification::left);
  }

  // Pattern grid: lanes by chop, numbered so colour is never the only indicator.
  g.setColour(juce::Colour(0xff20252d));
  g.fillRect(patternArea_);
  const int lanes = std::max(1, static_cast<int>(chops_.size()));
  const float laneH = static_cast<float>(patternArea_.getHeight()) / static_cast<float>(lanes);
  const float w = static_cast<float>(patternArea_.getWidth());
  g.setColour(juce::Colours::white.withAlpha(0.12f));
  for (double x : pattern_.beatLines) g.drawVerticalLine(patternArea_.getX() + static_cast<int>(x * w), static_cast<float>(patternArea_.getY()), static_cast<float>(patternArea_.getBottom()));
  for (std::size_t b = 0; b < pattern_.barLines.size(); ++b) {
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    const int x = patternArea_.getX() + static_cast<int>(pattern_.barLines[b] * w);
    g.drawVerticalLine(x, static_cast<float>(patternArea_.getY()), static_cast<float>(patternArea_.getBottom()));
    if (b < pattern_.barLocked.size() && pattern_.barLocked[b]) {
      g.setColour(juce::Colour(0xffff6b6b));
      g.drawText("locked", x + 3, patternArea_.getBottom() - 16, 60, 14, juce::Justification::left);
    }
  }
  for (const auto& r : pattern_.rects) {
    const float inset = static_cast<float>(r.depth) * 3.0f;
    const float y = static_cast<float>(patternArea_.getY()) + static_cast<float>(std::max(0, r.chopIndex)) * laneH + inset;
    juce::Rectangle<float> rect(static_cast<float>(patternArea_.getX()) + static_cast<float>(r.x0) * w, y, std::max(2.0f, static_cast<float>(r.x1 - r.x0) * w - 1.0f),
                                std::max(4.0f, laneH - 2.0f - 2.0f * inset));
    g.setColour(laneColour(r.chopIndex).withAlpha(r.depth > 0 ? 0.95f : 0.75f));
    g.fillRect(rect);
    if (r.id == selected_) {
      g.setColour(juce::Colours::white);
      g.drawRect(rect, 2.0f);
    } else if (r.locked || r.hasChild) {
      g.setColour(r.locked ? juce::Colour(0xffff6b6b) : juce::Colours::white);
      g.drawRect(rect, 1.0f);
    }
    if (rect.getWidth() > 14.0f && rect.getHeight() > 12.0f) {
      g.setColour(juce::Colours::black);
      g.drawText(juce::String(r.chopIndex + 1), rect.toNearestInt(), juce::Justification::centred);
    }
  }
  if (pattern_.rects.empty()) {
    g.setColour(juce::Colours::grey);
    g.drawText("Detect chops, then press Generate.", patternArea_, juce::Justification::centred);
  }

  // Variation family tree.
  g.setColour(juce::Colour(0xff20252d));
  g.fillRect(historyArea_);
  for (const auto& n : tree_) {
    const int x = historyArea_.getX() + 24 + n.column * 64;
    const int y = historyArea_.getY() + 22 + n.depth * 30;
    for (const auto& p : tree_)
      if (p.id == n.parent) {
        g.setColour(juce::Colours::grey);
        g.drawLine(static_cast<float>(historyArea_.getX() + 24 + p.column * 64), static_cast<float>(historyArea_.getY() + 22 + p.depth * 30), static_cast<float>(x), static_cast<float>(y));
      }
    g.setColour(n.active ? juce::Colour(0xff7cf29a) : (n.favorite ? juce::Colour(0xffffc857) : juce::Colour(0xff9aa4b2)));
    g.fillEllipse(static_cast<float>(x - 8), static_cast<float>(y - 8), 16.0f, 16.0f);
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.drawText(n.label.empty() ? juce::String(static_cast<juce::int64>(n.id)) : juce::String(n.label), x + 10, y - 7, 90, 14, juce::Justification::left);
  }
  if (tree_.empty()) {
    g.setColour(juce::Colours::grey);
    g.drawText("Each Generate, Mutate and Zoom is saved here as a branch you can return to.", historyArea_, juce::Justification::centred);
  }

  // Status line: user-visible messages and host-fallback hints.
  juce::String line = status_.isNotEmpty() ? status_ : proc_.statusMessage();
  if (proc_.usedFallbackTempo.load()) line << (line.isEmpty() ? "" : "   |   ") << "No host tempo: using " << juce::String(proc_.manualBpm.load(), 1) << " BPM";
  if (proc_.usedFallbackMeter.load()) line << "   |   Assuming 4/4";
  g.setColour(juce::Colours::white.withAlpha(0.8f));
  g.drawText(line, getLocalBounds().removeFromBottom(24).reduced(10, 0), juce::Justification::centredLeft, true);
}

void ChopFractalEditor::mouseDown(const juce::MouseEvent& e) {
  const auto pt = e.getPosition();
  if (waveArea_.contains(pt) && sourceFrames_ > 0) {
    const cf::ui::ViewWindow w{0, sourceFrames_, static_cast<double>(waveArea_.getWidth())};
    const std::int64_t frame = cf::ui::xToFrame(w, static_cast<double>(pt.x - waveArea_.getX()));
    if (e.mods.isAltDown()) {  // Alt-click removes the nearest marker
      cf::ChopId nearest;
      double best = 8.0;
      for (const auto& m : markers_)
        if (std::fabs(m.x - (pt.x - waveArea_.getX())) < best) {
          best = std::fabs(m.x - (pt.x - waveArea_.getX()));
          nearest = m.id;
        }
      if (nearest.valid()) run([=](cf::composition::ProjectSession& s) { return s.editMarkers([=](cf::source::ChopMap& m) { return m.deleteMarker(nearest); }); });
    } else {
      run([=](cf::composition::ProjectSession& s) {
        return s.editMarkers([=](cf::source::ChopMap& m) {
          auto r = m.addMarker(frame);
          return r.ok() ? cf::Status{} : cf::Status{r.error()};
        });
      });
    }
  } else if (patternArea_.contains(pt)) {
    selected_ = cf::EventId{};
    const float w = static_cast<float>(patternArea_.getWidth());
    const double fx = static_cast<double>(pt.x - patternArea_.getX()) / static_cast<double>(w);
    const float laneH = static_cast<float>(patternArea_.getHeight()) / static_cast<float>(std::max<std::size_t>(1, chops_.size()));
    const int lane = static_cast<int>(static_cast<float>(pt.y - patternArea_.getY()) / laneH);
    for (auto it = pattern_.rects.rbegin(); it != pattern_.rects.rend(); ++it)
      if (it->chopIndex == lane && fx >= it->x0 && fx <= it->x1) {
        selected_ = it->id;
        break;
      }
    repaint();
  } else if (historyArea_.contains(pt)) {
    for (const auto& n : tree_) {
      const int x = historyArea_.getX() + 24 + n.column * 64;
      const int y = historyArea_.getY() + 22 + n.depth * 30;
      if (std::abs(pt.x - x) < 11 && std::abs(pt.y - y) < 11) {
        const cf::history::NodeId id = n.id;
        const auto when = proc_.hostPlaying.load() ? cf::composition::Quantize::LoopBoundary : cf::composition::Quantize::Immediate;
        run([=](cf::composition::ProjectSession& s) { return s.activate(id, when); });
        break;
      }
    }
  }
}

void ChopFractalEditor::mouseDoubleClick(const juce::MouseEvent& e) {
  const auto pt = e.getPosition();
  if (!waveArea_.contains(pt) || sourceFrames_ <= 0) return;
  const cf::ui::ViewWindow w{0, sourceFrames_, static_cast<double>(waveArea_.getWidth())};
  const std::int64_t frame = cf::ui::xToFrame(w, static_cast<double>(pt.x - waveArea_.getX()));
  for (const auto& c : chops_)
    if (frame >= c.range.start && frame < c.range.end) {
      proc_.audition(c.id);  // plays through the output without touching the pattern playhead
      return;
    }
}
