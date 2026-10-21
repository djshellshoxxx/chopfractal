#pragma once
// Private helpers shared by the MIDI / kit / drag-out code (features.cpp, export_job.cpp, midi_drag.cpp).
#include <chopfractal/composition/project_session.hpp>
#include <chopfractal/midi_export/midi.hpp>

namespace chopfractal::composition::midibuild {

// Option checks shared by every MIDI path; `chopCount` is the number of chops that get a note.
Status validate(const MidiExportOptions& o, std::size_t chopCount);

// The loop over `o.loops` repeats of `events`: note = baseNote + the chop's position in `chops`, start = loop*length + start.
// Events of unknown chops are skipped (counted in `skipped` when given). Output is encoded by the caller with midi::encode.
Result<midi::FileSpec> buildSpec(const FlatEventList& events, Ticks lengthTicks, TimeSignature ts, const ChopSnapshot& chops, const MidiExportOptions& o,
                                 int* skipped = nullptr);

MidiExportOptions fromKit(const KitOptions& k);

// slice_NN_<role>.wav for each chop, in chop order.
std::vector<std::string> sliceFileNames(const roles::RoleMap& roles, const ChopSnapshot& chops);

// One kit slice: the chop's frames with the 32/64 frame (or per-chop) fades, 24-bit PCM at the source rate. Shared by exportKit and drag-out.
Result<std::vector<std::uint8_t>> encodeSlice(const render::SourceData& source, const ChopInfo& chop);

// Offline render of `playback` and WAV encoding with the same settings exportWav uses.
Result<std::vector<std::uint8_t>> renderWavFromPlayback(const std::shared_ptr<const render::Playback>& playback, const ExportWavOptions& o,
                                                        const render::RenderParams& params, wav::EncodeReport* report = nullptr,
                                                        std::size_t* frames = nullptr);

}  // namespace chopfractal::composition::midibuild
