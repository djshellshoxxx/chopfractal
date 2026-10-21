#include "ExportDrawer.h"

#include <cmath>
#include <initializer_list>

#include "CdlLookAndFeel.h"

namespace cf = chopfractal;

namespace {
constexpr int kBodyHeight = 786;
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
  for (auto* l : {&formatLabel_, &rateLabel_, &loopsLabel_, &baseNoteLabel_, &curveLabel_, &fixedLabel_, &midiLoopsLabel_, &channelLabel_, &sceneLabel_, &barsLabel_, &repeatsLabel_, &transitionLabel_}) {
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

  list_.setRowHeight(24);
  tip(list_, "Arrangement sections", "Sections of the arrangement, each plays a scene for a number of bars");
  list_.setTooltip("Arrangement sections, each plays a scene for a number of bars");
  body_.addAndMakeVisible(list_);
  tip(scene_, "Scene to add", "Scene the Add button appends as a new section");
  tip(sectionBars_, "Section bars", "Bars of the selected section");
  tip(repeats_, "Section repeats", "Times the selected section repeats");
  tip(transition_, "Arrangement transition", "How one section blends into the next");
  for (const char* n : {"Cut", "Crossfade 10%", "Crossfade 50%"}) transition_.addItem(n, transition_.getNumItems() + 1);
  for (int r = 1; r <= 8; ++r) repeats_.addItem(juce::String(r), r);
  for (auto* b : {&add_, &remove_, &up_, &down_, &chain_, &autoFill_, &exportArr_}) b->setTitle(b->getButtonText());
  add_.setTooltip("Append the chosen scene as a new section");
  remove_.setTooltip("Remove the selected section from the arrangement");
  up_.setTooltip("Move the selected section earlier");
  down_.setTooltip("Move the selected section later");
  chain_.setTooltip("Fill the arrangement from the scene chain");
  autoFill_.setTooltip("Fill about 16 bars from the scenes in turn");
  exportArr_.setTooltip("Render the arrangement to a WAV, MIDI and cue list");
  add_.onClick = [this] {
    const int i = scene_.getSelectedId() - 1;
    if (i < 0 || i >= static_cast<int>(arrState_.scenes.size())) return;
    const auto id = arrState_.scenes[static_cast<std::size_t>(i)].first;
    cmd_.editArrangement([id](cf::composition::Arrangement& a) {
      cf::composition::ArrangementSection s;
      s.sceneId = id;
      s.bars = 4;
      s.repeats = 1;
      a.sections.push_back(s);
    });
    syncArrangement();
    list_.selectRow(static_cast<int>(arrState_.arrangement.sections.size()) - 1);
  };
  remove_.onClick = [this] {
    const int r = list_.getSelectedRow();
    if (r < 0) return;
    cmd_.editArrangement([r](cf::composition::Arrangement& a) {
      if (r < static_cast<int>(a.sections.size())) a.sections.erase(a.sections.begin() + r);
    });
    syncArrangement();
  };
  up_.onClick = [this] {
    const int r = list_.getSelectedRow();
    if (r <= 0) return;
    cmd_.editArrangement([r](cf::composition::Arrangement& a) { std::swap(a.sections[static_cast<std::size_t>(r)], a.sections[static_cast<std::size_t>(r - 1)]); });
    syncArrangement();
    list_.selectRow(r - 1);
  };
  down_.onClick = [this] {
    const int r = list_.getSelectedRow();
    if (r < 0) return;
    cmd_.editArrangement([r](cf::composition::Arrangement& a) {
      if (r + 1 < static_cast<int>(a.sections.size())) std::swap(a.sections[static_cast<std::size_t>(r)], a.sections[static_cast<std::size_t>(r + 1)]);
    });
    syncArrangement();
    list_.selectRow(r + 1);
  };
  chain_.onClick = [this] {
    cmd_.arrangementFromChain();
    syncArrangement();
  };
  autoFill_.onClick = [this] {
    cmd_.autoFillArrangement();
    syncArrangement();
  };
  exportArr_.onClick = [this] { cmd_.exportArrangement(); };
  sectionBars_.onChange = [this] {
    if (syncing_) return;
    const int bars = sectionBars_.getText().getIntValue();
    editSelectedSection([bars](cf::composition::ArrangementSection& s) { s.bars = static_cast<std::uint8_t>(bars); });
  };
  repeats_.onChange = [this] {
    if (syncing_) return;
    const int n = repeats_.getSelectedId();
    editSelectedSection([n](cf::composition::ArrangementSection& s) { s.repeats = static_cast<std::uint8_t>(n); });
  };
  transition_.onChange = [this] {
    if (syncing_) return;
    const int t = transition_.getSelectedId() - 1;
    cmd_.editArrangement([t](cf::composition::Arrangement& a) { a.transition = static_cast<cf::composition::Transition>(t); });
    syncArrangement();
  };
  auto toggleHook = [this](juce::ToggleButton& b, const char* title, const char* tooltip, bool cf::composition::Arrangement::*field) {
    tip(b, title, tooltip);
    b.onClick = [this, &b, field] {
      if (syncing_) return;
      const bool on = b.getToggleState();
      cmd_.editArrangement([on, field](cf::composition::Arrangement& a) { a.*field = on; });
      syncArrangement();
    };
    body_.addAndMakeVisible(b);
  };
  toggleHook(fadeOut_, "Fade out", "End with a cosine fade over the last bar", &cf::composition::Arrangement::fadeOut);
  toggleHook(tail_, "Tail", "Append the decay of the last hit after the last bar", &cf::composition::Arrangement::tail);
  toggleHook(midi_, "Write MIDI", "Also write a MIDI file of the arrangement", &cf::composition::Arrangement::writeMidi);
  toggleHook(markers_, "Write markers", "Write section markers to the WAV, MIDI and a cue list", &cf::composition::Arrangement::writeMarkers);
  for (juce::Component* c : std::initializer_list<juce::Component*>{&scene_, &sectionBars_, &repeats_, &transition_, &add_, &remove_, &up_, &down_, &chain_, &autoFill_, &exportArr_})
    body_.addAndMakeVisible(*c);

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

void ExportDrawer::refresh() {
  syncWidgets(true);
  syncArrangement();
}

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
  in.arrangementRunning = arrState_.running;
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

void ExportDrawer::syncArrangement() {
  const bool wasSyncing = syncing_;
  syncing_ = true;
  arrState_ = cmd_.arrangementState();
  cf::ui::ArrangementViewInput in;
  for (const auto& r : arrState_.rows) {
    cf::ui::ArrangementRowInput ri;
    ri.sceneNumber = r.sceneNumber;
    ri.sceneName = r.sceneName;
    ri.patternBars = r.patternBars;
    ri.bars = r.bars;
    ri.repeats = r.repeats;
    ri.deleted = r.deleted;
    ri.fits = r.fits;
    in.rows.push_back(ri);
  }
  in.selected = list_.getSelectedRow();
  in.totalBars = arrState_.arrangement.totalBars();
  in.bpm = model_.activeBpm();
  in.sceneCount = static_cast<int>(arrState_.scenes.size());
  in.hasSource = arrState_.hasSource;
  in.running = arrState_.running;
  in.jobSection = arrState_.jobSection;
  in.jobSections = arrState_.jobSections;
  in.jobFraction = arrState_.jobFraction;
  in.fadeOut = arrState_.arrangement.fadeOut;
  arrangementView_ = cf::ui::buildArrangementView(in);
  list_.updateContent();
  list_.repaint();
  const int keep = scene_.getSelectedId();
  scene_.clear(juce::dontSendNotification);
  for (std::size_t i = 0; i < arrState_.scenes.size(); ++i) scene_.addItem(juce::String(arrState_.scenes[i].second), static_cast<int>(i) + 1);
  scene_.setSelectedId(keep > 0 && keep <= scene_.getNumItems() ? keep : (scene_.getNumItems() > 0 ? 1 : 0), juce::dontSendNotification);
  transition_.setSelectedId(static_cast<int>(arrState_.arrangement.transition) + 1, juce::dontSendNotification);
  fadeOut_.setToggleState(arrState_.arrangement.fadeOut, juce::dontSendNotification);
  tail_.setToggleState(arrState_.arrangement.tail, juce::dontSendNotification);
  midi_.setToggleState(arrState_.arrangement.writeMidi, juce::dontSendNotification);
  markers_.setToggleState(arrState_.arrangement.writeMarkers, juce::dontSendNotification);
  totalText_ = juce::String(arrangementView_.totalText);
  juce::String notes = juce::String(arrangementView_.statusText);
  for (const auto& n : arrangementView_.notes) notes << (notes.isEmpty() ? "" : "\n") << juce::String(n);
  notesText_ = notes;
  syncing_ = wasSyncing;
  updateArrangementButtons();
  repaint();
}

void ExportDrawer::updateArrangementButtons() {
  const bool wasSyncing = syncing_;
  syncing_ = true;
  const int row = list_.getSelectedRow();
  cf::ui::ArrangementViewInput in;
  for (const auto& r : arrState_.rows) {
    cf::ui::ArrangementRowInput ri;
    ri.sceneNumber = r.sceneNumber;
    ri.patternBars = r.patternBars;
    ri.bars = r.bars;
    ri.deleted = r.deleted;
    ri.fits = r.fits;
    in.rows.push_back(ri);
  }
  in.selected = row;
  in.totalBars = arrState_.arrangement.totalBars();
  in.sceneCount = static_cast<int>(arrState_.scenes.size());
  in.hasSource = arrState_.hasSource;
  in.running = arrState_.running;
  arrangementView_ = cf::ui::buildArrangementView(in);
  add_.setEnabled(arrangementView_.addEnabled);
  remove_.setEnabled(arrangementView_.removeEnabled);
  up_.setEnabled(arrangementView_.moveUpEnabled);
  down_.setEnabled(arrangementView_.moveDownEnabled);
  chain_.setEnabled(arrangementView_.useChainEnabled);
  autoFill_.setEnabled(arrangementView_.autoFillEnabled);
  exportArr_.setEnabled(arrangementView_.exportEnabled);
  exportArr_.setTooltip(arrangementView_.exportEnabled ? "Render the arrangement to a WAV, MIDI and cue list" : withReason("Render the arrangement to a WAV, MIDI and cue list", false, arrangementView_.exportTooltip));
  scene_.setEnabled(arrangementView_.addEnabled || !arrState_.scenes.empty());
  const bool sel = row >= 0 && row < static_cast<int>(arrState_.arrangement.sections.size());
  sectionBars_.setEnabled(sel && arrangementView_.editable);
  repeats_.setEnabled(sel && arrangementView_.editable);
  for (auto* t : {&fadeOut_, &tail_, &midi_, &markers_}) t->setEnabled(arrangementView_.editable);
  transition_.setEnabled(arrangementView_.editable);
  sectionBars_.clear(juce::dontSendNotification);
  if (sel) {
    const auto& r = arrState_.rows[static_cast<std::size_t>(row)];
    for (int b : cf::ui::arrangementBarChoices(r.patternBars)) sectionBars_.addItem(juce::String(b), sectionBars_.getNumItems() + 1);
    for (int i = 0; i < sectionBars_.getNumItems(); ++i)
      if (sectionBars_.getItemText(i).getIntValue() == r.bars) sectionBars_.setSelectedItemIndex(i, juce::dontSendNotification);
    repeats_.setSelectedId(r.repeats, juce::dontSendNotification);
  }
  syncing_ = wasSyncing;
}

void ExportDrawer::editSelectedSection(const std::function<void(cf::composition::ArrangementSection&)>& edit) {
  const int row = list_.getSelectedRow();
  if (row < 0) return;
  cmd_.editArrangement([&](cf::composition::Arrangement& a) {
    if (row < static_cast<int>(a.sections.size())) edit(a.sections[static_cast<std::size_t>(row)]);
  });
  syncArrangement();
}

void ExportDrawer::cancelAny() {
  if (model_.exportRunning()) cmd_.cancelExport();
  if (arrState_.running) cmd_.cancelArrangement();
}

void ExportDrawer::tick() {
  const bool arrWas = arrState_.running;
  const bool arrNow = cmd_.pollArrangement();
  if (arrNow || arrWas) {
    arrState_ = cmd_.arrangementState();
    if (arrNow != arrWas) syncArrangement();
  }
  const bool exporting = model_.exportRunning();
  const bool any = exporting || arrNow;
  if (any) {
    progressValue_ = exporting ? static_cast<double>(model_.exportFraction()) : arrState_.jobFraction;
    progressText_ = exporting ? "Exporting" : juce::String(cf::ui::arrangementProgressMessage(std::max(1, arrState_.jobSection), std::max(1, arrState_.jobSections), arrState_.jobFraction));
  }
  if (cancel_.isVisible() != any) {
    cancel_.setVisible(any);
    progressBar_.setVisible(any);
    resized();
    syncWidgets(false);
  }
  if (any) progressBar_.repaint();
}

// ---- list ----
void ExportDrawer::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
  if (row < 0 || row >= static_cast<int>(arrangementView_.rows.size())) return;
  const auto& p = cdl::pal();
  if (selected) {
    g.setColour(p.spray.withAlpha(0.20f));
    g.fillRect(0, 0, w, h);
    g.setColour(p.spray);
    g.fillRect(0, 0, 2, h);
  }
  g.setColour(p.ink);
  g.setFont(cdl::font(cdl::FontRole::Hint, cdl::uiScale()));
  g.drawText(juce::String(arrangementView_.rows[static_cast<std::size_t>(row)]), 8, 0, w - 12, h, juce::Justification::centredLeft, true);
}

juce::String ExportDrawer::getNameForRow(int row) { return row >= 0 && row < static_cast<int>(arrangementView_.rows.size()) ? juce::String(arrangementView_.rows[static_cast<std::size_t>(row)]) : juce::String(); }

// ---- layout ----
void ExportDrawer::paintBody(juce::Graphics& g) {
  const auto& p = cdl::pal();
  auto section = [&](juce::Rectangle<int> r, const char* title) { cdl::paintSectionLabel(g, title, r.removeFromTop(20)); };
  section(formatArea_, "WAV");
  section(midiArea_, "MIDI");
  section(dragArea_, "Drag out");
  section(arrArea_, "Arrangement");
  g.setColour(p.inkMuted);
  g.setFont(cdl::font(cdl::FontRole::Hint, cdl::uiScale()));
  g.drawText(estimateText_, estimateArea_, juce::Justification::centredLeft, true);
  g.drawText(tempoText_, tempoArea_, juce::Justification::centredLeft, true);
  g.drawText(totalText_, totalArea_, juce::Justification::centredLeft, true);
  g.drawFittedText(hintText_, dragArea_.withTrimmedTop(54), juce::Justification::topLeft, 2);
  g.drawFittedText(notesText_, arrArea_.withTrimmedTop(arrArea_.getHeight() - 86).withTrimmedBottom(36), juce::Justification::topLeft, 3);
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
  // Arrangement
  arrArea_ = {0, y, width, 380};
  y += 22;
  list_.setBounds(pad, y, width - 2 * pad, 96);
  y += 96 + gap;
  {
    auto r = row();
    sceneLabel_.setBounds(r.removeFromLeft(78));
    scene_.setBounds(r.removeFromLeft(r.getWidth() - 56));
    r.removeFromLeft(4);
    add_.setBounds(r);
  }
  {
    auto r = row();
    const int w = (r.getWidth() - 8) / 3;
    remove_.setBounds(r.removeFromLeft(w));
    r.removeFromLeft(4);
    up_.setBounds(r.removeFromLeft(w));
    r.removeFromLeft(4);
    down_.setBounds(r);
  }
  {
    auto r = row();
    barsLabel_.setBounds(r.removeFromLeft(78));
    sectionBars_.setBounds(r.removeFromLeft(60));
    r.removeFromLeft(8);
    repeatsLabel_.setBounds(r.removeFromLeft(56));
    repeats_.setBounds(r.removeFromLeft(60));
  }
  {
    auto r = row();
    transitionLabel_.setBounds(r.removeFromLeft(78));
    transition_.setBounds(r.removeFromLeft(130));
  }
  {
    auto r = row();
    fadeOut_.setBounds(r.removeFromLeft(r.getWidth() / 2));
    tail_.setBounds(r);
  }
  {
    auto r = row();
    midi_.setBounds(r.removeFromLeft(r.getWidth() / 2));
    markers_.setBounds(r);
  }
  {
    auto r = row();
    chain_.setBounds(r.removeFromLeft(r.getWidth() / 2 - 2));
    r.removeFromLeft(4);
    autoFill_.setBounds(r);
  }
  totalArea_ = row(18);
  y += 40;  // the notes block
  exportArr_.setBounds(pad, y, width - 2 * pad, 28);
  y += 36;
  arrArea_.setHeight(y - arrArea_.getY());
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
