#include "MeterStrip.h"

#include "CdlLookAndFeel.h"

namespace cf = chopfractal;

MeterStrip::MeterStrip(EditorModel& m) : model_(m) {
  setComponentID("MeterStrip");
  setTitle("Output level");
  setDescription("Left and right output peak");
  setTooltip(cf::ui::meterTooltip(state_));
  chip_.setTitle("Clear clip indicator");
  chip_.setTooltip("CLIP lights when the output exceeds 0 dBFS, click it to reset");
  chip_.onClick = [this] { clearClip(); };
  addAndMakeVisible(chip_);
  lastTick_ = juce::Time::getMillisecondCounterHiRes() / 1000.0;
  startTimerHz(30);
}

MeterStrip::~MeterStrip() { stopTimer(); }

void MeterStrip::clearClip() {
  if (!state_.clipLatched) return;
  state_.clipLatched = false;
  announcedClip_ = false;
  chip_.latched = false;
  ++repaints_;
  chip_.repaint();
  repaint();
}

void MeterStrip::timerCallback() {
  const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
  const double dt = std::max(0.001, std::min(0.25, now - lastTick_));  // a throttled host must not make the bars jump
  lastTick_ = now;
  tickOnce(dt);
}

void MeterStrip::tickOnce(double dt) {
  const auto r = model_.takeMeter();
  voices_ = r.activeVoices;
  cf::ui::MeterInput in;
  in.peak[0] = r.peak[0];
  in.peak[1] = r.peak[1];
  in.clipSamples = r.clipSamples;
  const bool wasLatched = state_.clipLatched;
  if (cf::ui::updateMeter(state_, in, dt, cdl::reducedMotion())) {
    ++repaints_;
    repaint();
  }
  if (state_.clipLatched != wasLatched) {
    chip_.latched = state_.clipLatched;
    chip_.repaint();
    if (state_.clipLatched && !announcedClip_) {
      announcedClip_ = true;
      if (auto* h = chip_.getAccessibilityHandler()) h->postAnnouncement("Output clipped", juce::AccessibilityHandler::AnnouncementPriority::medium);
    }
  }
  refreshAccessibility();
}

void MeterStrip::refreshAccessibility() {
  // The description and tooltip follow the level at 2 Hz only, so a screen reader is not flooded.
  const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
  if (now - lastDescribe_ < 0.5) return;
  lastDescribe_ = now;
  const juce::String t = juce::String(cf::ui::meterTooltip(state_));
  if (t == lastTooltip_) return;
  lastTooltip_ = t;
  setTooltip(t);
  setDescription("Left " + juce::String(cf::ui::dbText(state_.db[0])) + ", right " + juce::String(cf::ui::dbText(state_.db[1])));
}

void MeterStrip::resized() {
  auto a = getLocalBounds();
  const int chipW = static_cast<int>(30 * scale_), chipH = std::min(a.getHeight(), static_cast<int>(16 * scale_));
  chip_.setBounds(a.removeFromRight(chipW).withSizeKeepingCentre(chipW, chipH));
  a.removeFromRight(static_cast<int>(4 * scale_));
  barsArea_ = a;
}

void MeterStrip::Chip::paintButton(juce::Graphics& g, bool over, bool) {
  const auto& p = cdl::pal();
  auto r = getLocalBounds().toFloat().reduced(0.5f);
  if (latched) {
    g.setColour(p.clip);
    g.fillRoundedRectangle(r, 2.0f);
    g.setColour(p.onSpray);
  } else {
    g.setColour(over ? p.ink : p.inkMuted);
    g.drawRoundedRectangle(r, 2.0f, 1.0f);
  }
  g.setFont(cdl::font(cdl::FontRole::MonoXs, std::max(0.7f, cdl::uiScale())));
  g.drawText("CLIP", getLocalBounds(), juce::Justification::centred, false);
  if (hasKeyboardFocus(false)) {
    g.setColour(p.spray);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 2.0f, 1.5f);
  }
}

void MeterStrip::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  const float s = scale_;
  const float barH = 7.0f * s, gapY = 2.0f * s;
  const float totalH = 2.0f * barH + gapY;
  const float top = static_cast<float>(barsArea_.getY()) + (static_cast<float>(barsArea_.getHeight()) - totalH) * 0.5f;
  const float left = static_cast<float>(barsArea_.getX()), width = static_cast<float>(barsArea_.getWidth());
  for (int c = 0; c < 2; ++c) {
    const float y = top + static_cast<float>(c) * (barH + gapY);
    g.setColour(p.bg);
    g.fillRect(left, y, width, barH);
    const float db = state_.db[c];
    const float fillW = cf::ui::dbToFraction(db) * width;
    g.setColour(db > -3.f ? p.clip : db > -12.f ? p.warn : p.ok);
    g.fillRect(left, y, fillW, barH);
    // the hold line: 2 px, only while it sits above the bar
    if (state_.holdDb[c] > db + 0.25f) {
      const float hx = left + cf::ui::dbToFraction(state_.holdDb[c]) * width;
      g.setColour(p.ink);
      g.fillRect(std::max(left, hx - 2.0f), y, 2.0f, barH);
    }
    g.setColour(p.edge);
    g.drawRect(left, y, width, barH, 1.0f);
  }
  const float zeroX = left + cf::ui::dbToFraction(0.f) * width;  // the 0 dB tick
  g.setColour(p.inkMuted);
  g.fillRect(zeroX, top - 1.0f, 1.0f, totalH + 2.0f);
}
