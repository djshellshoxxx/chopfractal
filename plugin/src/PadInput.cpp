#include "PadInput.h"

#include <algorithm>

namespace cf = chopfractal;

bool PadInput::trigger(int note, int velocity, bool on) {
  UiEvent e;
  e.note = static_cast<std::uint8_t>(juce::jlimit(0, 127, note));
  e.velocity = static_cast<std::uint8_t>(juce::jlimit(1, 127, velocity));
  e.on = on;
  return ui_.push(e);
}

void PadInput::pushTake(double ppq, std::uint8_t note, std::uint8_t velocity, bool on) {
  TakeEvent t;
  t.ppq = ppq;
  t.note = note;
  t.velocity = velocity;
  t.on = on;
  if (!take_.push(t)) dropped_.fetch_add(1, std::memory_order_relaxed);
}

const cf::render::PadEvent* PadInput::collect(const juce::MidiBuffer& midi, int frames, double recordPpq, double bpm, double sampleRate, int& count) {
  count = 0;
  const bool recording = recordPpq >= 0.0;
  auto add = [&](int frame, cf::render::PadEvent::Type type, int note, int velocity) {
    if (count >= cf::render::kMaxPadEventsPerBlock) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    cf::render::PadEvent& e = events_[static_cast<std::size_t>(count++)];
    e.frame = frame;
    e.type = type;
    e.note = static_cast<std::uint8_t>(note);
    e.velocity = static_cast<std::uint8_t>(velocity);
  };
  UiEvent ui;
  while (ui_.pop(ui)) {
    add(0, ui.on ? cf::render::PadEvent::Type::NoteOn : cf::render::PadEvent::Type::NoteOff, ui.note, ui.velocity);
    if (ui.on) activity_[ui.note >> 5].fetch_or(1u << (ui.note & 31), std::memory_order_relaxed);
    if (recording) pushTake(recordPpq, ui.note, ui.velocity, ui.on);
  }
  const int channel = channel_.load(std::memory_order_relaxed);
  for (const auto meta : midi) {
    const juce::MidiMessage msg = meta.getMessage();
    if (channel != 0 && msg.getChannel() != 0 && msg.getChannel() != channel) continue;
    const int frame = juce::jlimit(0, std::max(0, frames - 1), meta.samplePosition);
    const double ppq = recordPpq + frame * bpm / (60.0 * sampleRate);
    if (msg.isNoteOn()) {
      const int note = msg.getNoteNumber(), vel = msg.getVelocity();
      add(frame, cf::render::PadEvent::Type::NoteOn, note, vel);
      activity_[static_cast<std::size_t>(note >> 5) & 3].fetch_or(1u << (note & 31), std::memory_order_relaxed);
      if (recording) pushTake(ppq, static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(vel), true);
    } else if (msg.isNoteOff()) {
      add(frame, cf::render::PadEvent::Type::NoteOff, msg.getNoteNumber(), 0);
      if (recording) pushTake(ppq, static_cast<std::uint8_t>(msg.getNoteNumber()), 0, false);
    } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
      add(frame, cf::render::PadEvent::Type::AllNotesOff, 0, 0);
    }
  }
  return events_.data();
}

void PadInput::pollTake() {
  if (!openInit_) {
    open_.fill(-1);
    openInit_ = true;
  }
  TakeEvent e;
  while (take_.pop(e)) {
    if (e.on) {
      if (takeBuffer_.size() >= kMaxTakeHits) {
        bufferFull_.store(true);
        continue;
      }
      cf::composition::TakeHit h;
      h.ppqOn = e.ppq;
      h.note = e.note;
      h.velocity = e.velocity;
      takeBuffer_.push_back(h);
      open_[e.note & 127] = static_cast<int>(takeBuffer_.size()) - 1;
    } else {
      int& idx = open_[e.note & 127];
      if (idx >= 0 && idx < static_cast<int>(takeBuffer_.size())) takeBuffer_[static_cast<std::size_t>(idx)].ppqOff = e.ppq;
      idx = -1;
    }
  }
}

std::vector<cf::composition::TakeHit> PadInput::takeHits() {
  pollTake();
  std::vector<cf::composition::TakeHit> out;
  out.swap(takeBuffer_);
  takeBuffer_.reserve(kMaxTakeHits);
  open_.fill(-1);
  return out;
}

void PadInput::discardTake() {
  pollTake();
  takeBuffer_.clear();
  open_.fill(-1);
}
