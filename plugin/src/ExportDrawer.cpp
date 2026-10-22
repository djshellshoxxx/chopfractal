#include "ExportDrawer.h"

#include <cmath>
#include <initializer_list>

#include "CdlLookAndFeel.h"

namespace cf = chopfractal;

namespace {
constexpr int kBodyHeight = 560;
// A disabled control keeps its description and gains the reason, as one clause without a final period.
juce::String clause(const std::string& t) { return juce::String(t).trimEnd().trimCharactersAtEnd("."); }
juce::String withReason(const char* base, bool enabled, const std::string& reason) {
  juce::String r = juce::String(reason).trimEnd().trimCharactersAtEnd(".");
  if (enabled || r.isEmpty()) return base;
  return juce::String(base) + ", " + r.substring(0, 1).toLowerCase() + r.substring(1);
}
void tip(juce::Component& c, const juce::String& title, const juce::String& tooltip) {
  c.setTitle(title);
  if (auto* t = dynamic_cast<juce::SettableTooltipClient*>(&c)) t->setTooltip(tooltip);
}
}  // namespace

ExportDrawer::ExportDrawer(EditorModel& m, EditorCommands& c) : EditorPanelBase(m, c, "ExportDrawer") {
  addAndMakeVisible(viewport_);
  viewport_.setViewedComponent(&body_, false);
  viewport_.setScrollBarsShown(true, false);
  viewport_.setTitle("Export options");

  format_.addItemList(juce::StringArray(), 1);
  for (std::size_t i = 0; i < cf::ui::exportFormatItems().size(); ++i) format_.addItem(cf::ui::exportFormatItems()[i], static_cast<int>(i) + 1);
  for (std::size_t i = 0; i < cf::ui::exportRateItems().size(); ++i) rate_.addItem(cf::ui::exportRateItems()[i], static_cast<int>(i) + 1);
  for (std::size_t i = 0; i < cf::ui::exportLoopItems().size(); ++i) loops_.addItem(cf::ui::exportLoopItems()[i], static_cast<int>(i) + 1);
  tip(format_, "Export format", "Word size of the exported WAV, 24-bit suits most DAWs");
  tip(rate_, "Export sample rate", "Sample rate of the exported WAV, match your project");
  tip(loops_, "Export loops", "How many times the pattern repeats in the exported WAV");
  for (auto* cb : {&format_, &rate_, &loops_}) {
    cb->onChange = [this] {
      if (!syncing_) pushFormat();
    };
    body_.addAndMakeVisible(*cb);
  }
  normalize_.setTitle("Normalize");
  normalize_.setTooltip("Scale the export so its loudest peak sits at minus 1 dB");
  normalize_.onClick = [this] { pushFormat(); };
  body_.addAndMakeVisible(normalize_);
  for (auto* l : {&formatLabel_, &rateLabel_, &loopsLabel_, &baseNoteLabel_, &curveLabel_, &fixedLabel_, &midiLoopsLabel_, &channelLabel_}) {
    l->setMinimumHorizontalScale(0.8f);
    body_.addAndMakeVisible(*l);
  }
  tip(exportWav_, "Export WAV", "Render the pattern to a WAV file");
  tip(exportKit_, "Export Kit", "Write one WAV per chop and a MIDI file that replays the pattern");
  exportWav_.onClick = [this] { cmd_.exportWav(cmd_.exportUi()); };
  exportKit_.onClick = [this] { cmd_.exportKit(); };
  body_.addAndMakeVisible(exportWav_);
  body_.addAndMakeVisible(exportKit_);

  baseNote_.setSliderStyle(juce::Slider::LinearHorizontal);
  baseNote_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
  baseNote_.setRange(0, 127, 1);
  baseNote_.textFromValueFunction = [](double v) { return juce::String(cf::ui::midiNoteText(static_cast<int>(std::lround(v)))); };
  tip(baseNote_, "MIDI base note", "MIDI note that chop 1 plays, the other chops follow upwards");
  fixedVelocity_.setSliderStyle(juce::Slider::LinearHorizontal);
  fixedVelocity_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20);
  fixedVelocity_.setRange(1, 127, 1);
  tip(fixedVelocity_, "MIDI fixed velocity", "Velocity every note gets when the curve is Fixed");
  for (const char* n : {"Linear", "Soft", "Hard", "Fixed"}) curve_.addItem(n, curve_.getNumItems() + 1);
  for (int v : cf::ui::kMidiLoopChoices) midiLoops_.addItem(juce::String(v), midiLoops_.getNumItems() + 1);
  for (int ch = 1; ch <= 16; ++ch) channel_.addItem(juce::String(ch), ch);
  tip(curve_, "MIDI velocity curve", "How event levels map to MIDI velocity");
  tip(midiLoops_, "MIDI loops", "How many times the pattern repeats in the MIDI file");
  tip(channel_, "MIDI channel", "MIDI channel the notes are written on");
  baseNote_.onValueChange = [this] {
    if (!syncing_) pushMidi();
  };
  fixedVelocity_.onValueChange = [this] {
    if (!syncing_) pushMidi();
  };
  for (auto* cb : {&curve_, &midiLoops_, &channel_}) cb->onChange = [this] {
    if (!syncing_) pushMidi();
  };
  for (juce::Component* c : std::initializer_list<juce::Component*>{&baseNote_, &fixedVelocity_, &curve_, &midiLoops_, &channel_}) body_.addAndMakeVisible(*c);

  dragMidi_.setTitle("Drag MIDI");
  dragWav_.setTitle("Drag WAV");
  dragKit_.setTitle("Drag Kit");
  for (auto* d : {&dragMidi_, &dragWav_, &dragKit_}) body_.addAndMakeVisible(*d);

  cancel_.setTitle("Cancel export");
  cancel_.setTooltip("Stop the running export and remove its partial files");
  cancel_.onClick = [this] { cancelAny(); };
  addChildComponent(cancel_);
  progressBar_.setPercentageDisplay(false);
  progressBar_.setTitle("Export progress");
  addChildComponent(progressBar_);

  body_.setSize(360, kBodyHeight);
  refresh();
}

void ExportDrawer::lookAndFeelChanged() { repaint(); }

void ExportDrawer::modelChanged(Change c) {
  if (c == Change::Extras) syncWidgets(true);
  else if (c == Change::Flags || c == Change::View) syncWidgets(false);
}

void ExportDrawer::refresh() { syncWidgets(true); }

void ExportDrawer::pushFormat() {
  cmd_.setExportUi({format_.getSelectedId() - 1, rate_.getSelectedId() - 1, loops_.getSelectedId() - 1, normalize_.getToggleState()});
  syncWidgets(false);
}

void ExportDrawer::pushMidi() {
  cf::composition::MidiExportSettings m;
  m.baseNote = static_cast<int>(std::lround(baseNote_.getValue()));
  m.curve = static_cast<cf::composition::VelocityCurve>(std::max(0, curve_.getSelectedId() - 1));
  m.fixedVelocity = static_cast<int>(std::lround(fixedVelocity_.getValue()));
  m.loops = cf::ui::kMidiLoopChoices[std::max(0, std::min(4, midiLoops_.getSelectedId() - 1))];
  m.channel = std::max(0, channel_.getSelectedId() - 1);
  cmd_.setMidiSettings(m);
  syncWidgets(false);
}

void ExportDrawer::syncWidgets(bool settings) {
  const bool wasSyncing = syncing_;
  syncing_ = true;
  const auto& e = model_.extras();
  const ExportUi ui = cmd_.exportUi();
  if (settings) {  // only when the saved values changed: a pending combo notification must not be overwritten by a status refresh
    format_.setSelectedId(e.exportUi.formatIndex + 1, juce::dontSendNotification);
    rate_.setSelectedId(e.exportUi.rateIndex + 1, juce::dontSendNotification);
    loops_.setSelectedId(e.exportUi.loopsIndex + 1, juce::dontSendNotification);
    normalize_.setToggleState(ui.normalize, juce::dontSendNotification);
  }

  cf::ui::MidiOptionsInput mi;
  mi.baseNote = e.exportUi.midi.baseNote;
  mi.curve = static_cast<int>(e.exportUi.midi.curve);
  mi.fixedVelocity = e.exportUi.midi.fixedVelocity;
  mi.loops = e.exportUi.midi.loops;
  mi.channel = e.exportUi.midi.channel;
  mi.chopCount = static_cast<int>(model_.view().chops.size());
  mi.bpm = model_.activeBpm();
  mi.fallbackTempo = model_.usedFallbackTempo();
  midiView_ = cf::ui::buildMidiOptionsView(mi);
  baseNote_.setRange(0, midiView_.baseNoteMax, 1);
  if (settings) {
    baseNote_.setValue(midiView_.baseNote, juce::dontSendNotification);
    curve_.setSelectedId(midiView_.curve + 1, juce::dontSendNotification);
    fixedVelocity_.setValue(midiView_.fixedVelocity, juce::dontSendNotification);
    midiLoops_.setSelectedId(midiView_.loopsIndex + 1, juce::dontSendNotification);
    channel_.setSelectedId(midiView_.channelIndex + 1, juce::dontSendNotification);
  }
  fixedVelocity_.setEnabled(midiView_.fixedVelocityEnabled);
  fixedLabel_.setEnabled(midiView_.fixedVelocityEnabled);
  tempoText_ = juce::String(midiView_.tempoLine);

  const auto& v = model_.view();
  cf::ui::ExportViewInput in;
  in.hasSource = v.hasSource;
  in.hasPattern = v.hasPattern;
  in.sourceMissing = model_.sourceMissing();
  in.exporting = model_.exportRunning();
  in.bars = std::max(1, v.barCount);
  in.bpm = model_.activeBpm();
  in.formatIndex = e.exportUi.formatIndex;
  in.rateIndex = e.exportUi.rateIndex;
  in.loopsIndex = e.exportUi.loopsIndex;
  exportView_ = cf::ui::buildExportView(in);
  exportWav_.setEnabled(exportView_.wavEnabled);
  exportWav_.setTooltip(withReason("Render the pattern to a WAV file", exportView_.wavEnabled, exportView_.wavTooltip));
  exportKit_.setEnabled(exportView_.kitEnabled);
  exportKit_.setTooltip(withReason("Write one WAV per chop and a MIDI file that replays the pattern", exportView_.kitEnabled, exportView_.kitTooltip));
  for (auto* c : {&format_, &rate_, &loops_}) c->setEnabled(exportView_.formatEnabled);
  normalize_.setEnabled(exportView_.formatEnabled);
  estimateText_ = juce::String(exportView_.estimate);

  cf::ui::DragOutInput di;
  di.hasPattern = v.hasPattern;
  di.sourceMissing = model_.sourceMissing();
  di.chopCount = static_cast<int>(v.chops.size());
  di.baseNote = e.exportUi.midi.baseNote;
  const double secs = cf::ui::exportSeconds(std::max(1, v.barCount), 4, model_.activeBpm(), cf::ui::exportLoopCount(e.exportUi.loopsIndex));
  di.wavSeconds = secs;
  const auto dv = cf::ui::buildDragOutView(di);
  dragMidi_.setEnabled(dv.midi.enabled);
  dragMidi_.setTooltip(clause(dv.midi.tooltip));
  dragWav_.setEnabled(dv.wav.enabled);
  dragWav_.setTooltip(clause(dv.wav.tooltip));
  dragKit_.setEnabled(dv.kit.enabled);
  dragKit_.setTooltip(clause(dv.kit.tooltip));
  hintText_ = juce::String(dv.hint.empty() ? cf::ui::dragHintText(dragout::folder().getFullPathName().toStdString()) : dv.hint);
  syncing_ = wasSyncing;
  repaint();
}

void ExportDrawer::cancelAny() {
  if (model_.exportRunning()) cmd_.cancelExport();
}

void ExportDrawer::tick() {
  const bool exporting = model_.exportRunning();
  if (exporting) {
    progressValue_ = static_cast<double>(model_.exportFraction());
    progressText_ = "Exporting";
  }
  if (cancel_.isVisible() != exporting) {
    cancel_.setVisible(exporting);
    progressBar_.setVisible(exporting);
    resized();
    syncWidgets(false);
  }
  if (exporting) progressBar_.repaint();
}

// ---- layout ----
void ExportDrawer::paintBody(juce::Graphics& g) {
  const auto& p = cdl::pal();
  auto section = [&](juce::Rectangle<int> r, const char* title) { cdl::paintSectionLabel(g, title, r.removeFromTop(20)); };
  section(formatArea_, "WAV");
  section(midiArea_, "MIDI");
  section(dragArea_, "Drag out");
  g.setColour(p.inkMuted);
  g.setFont(cdl::font(cdl::FontRole::Hint, cdl::uiScale()));
  g.drawText(estimateText_, estimateArea_, juce::Justification::centredLeft, true);
  g.drawText(tempoText_, tempoArea_, juce::Justification::centredLeft, true);
  g.drawFittedText(hintText_, dragArea_.withTrimmedTop(54), juce::Justification::topLeft, 2);
}

void ExportDrawer::layoutBody(int width) {
  const int pad = 6, gap = 4;
  int y = 0;
  auto row = [&](int h = 26) {
    juce::Rectangle<int> r(pad, y, width - 2 * pad, h);
    y += h + gap;
    return r;
  };
  // WAV
  formatArea_ = {0, y, width, 150};
  y += 22;
  {
    auto r = row();
    formatLabel_.setBounds(r.removeFromLeft(78));
    format_.setBounds(r.removeFromLeft(120));
  }
  {
    auto r = row();
    rateLabel_.setBounds(r.removeFromLeft(78));
    rate_.setBounds(r.removeFromLeft(120));
  }
  {
    auto r = row();
    loopsLabel_.setBounds(r.removeFromLeft(78));
    loops_.setBounds(r.removeFromLeft(70));
  }
  normalize_.setBounds(row());
  estimateArea_ = row(18);
  {
    auto r = row();
    exportWav_.setBounds(r.removeFromLeft(r.getWidth() / 2 - 2));
    r.removeFromLeft(4);
    exportKit_.setBounds(r);
  }
  y += 8;
  // MIDI
  midiArea_ = {0, y, width, 190};
  y += 22;
  {
    auto r = row();
    baseNoteLabel_.setBounds(r.removeFromLeft(78));
    baseNote_.setBounds(r);
  }
  {
    auto r = row();
    curveLabel_.setBounds(r.removeFromLeft(78));
    curve_.setBounds(r.removeFromLeft(120));
  }
  {
    auto r = row();
    fixedLabel_.setBounds(r.removeFromLeft(78));
    fixedVelocity_.setBounds(r);
  }
  {
    auto r = row();
    midiLoopsLabel_.setBounds(r.removeFromLeft(78));
    midiLoops_.setBounds(r.removeFromLeft(70));
    r.removeFromLeft(8);
    channelLabel_.setBounds(r.removeFromLeft(62));
    channel_.setBounds(r.removeFromLeft(70));
  }
  tempoArea_ = row(18);
  y += 8;
  // Drag out
  dragArea_ = {0, y, width, 96};
  y += 22;
  {
    auto r = row(28);
    const int w = (r.getWidth() - 8) / 3;
    dragMidi_.setBounds(r.removeFromLeft(w));
    r.removeFromLeft(4);
    dragWav_.setBounds(r.removeFromLeft(w));
    r.removeFromLeft(4);
    dragKit_.setBounds(r);
  }
  y = dragArea_.getBottom() + 8;
  body_.setSize(width, y);
}

void ExportDrawer::resized() {
  auto a = getLocalBounds();
  if (cancel_.isVisible()) {
    auto strip = a.removeFromBottom(30);
    cancel_.setBounds(strip.removeFromRight(80).reduced(2));
    progressBar_.setBounds(strip.reduced(2, 5));
  }
  viewport_.setBounds(a);
  const int w = std::max(240, a.getWidth() - viewport_.getScrollBarThickness());
  layoutBody(w);
}
