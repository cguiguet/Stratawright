// tests/unit/Middle Bridge/midi_duplicate_test.cpp
//
// Duplicating a MIDI clip must copy its notes into a clip of its own (it used to create an empty
// clip), placed in musical time so that recomputing positions from bars and beats keeps them.

#include <catch2/catch_test_macros.hpp>
#include "Middle Bridge/timeline/arrangement_controller.h"
#include "Middle Bridge/project/session_manager.h"
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/interfaces/itrack_pipeline_builder.h"
#include "musical_composition/midi_sequencer/imidi_sequencer.h"
#include "Core infrastructure/tempo/itempo_service.h"
#include "Core infrastructure/memory/istring_registry.h"
#include <memory>
#include <vector>

namespace {

class TimelineOnlyBuilder : public composition::ITrackPipelineBuilder {
public:
    composition::TrackPipelineDescriptor buildPipeline(const composition::TrackCreateInfo&, IDSPKernel*) override { return {}; }
    void destroyPipeline(const composition::TrackPipelineDescriptor&, IDSPKernel*) override {}
};

// 100 BPM in 4/4 at 48 kHz.
constexpr uint64_t kBeat = 28800;
constexpr uint64_t kBar = 4 * kBeat;

std::vector<composition::MIDINote> notesIn(composition::IMIDISequencer* sequencer, uint64_t sourceRaw) {
    std::vector<composition::MIDINote> notes(64);
    notes.resize(sequencer->getNotesInClip(ClipID::fromRaw(sourceRaw), notes.data(), 64));
    return notes;
}

} // namespace

TEST_CASE("Duplicating a MIDI clip copies its notes into a clip of its own", "[MiddleBridge][Arrangement]") {
    bridge::SessionManager sessions;
    auto session = composition::IProjectSession::create(std::make_unique<TimelineOnlyBuilder>(), nullptr, nullptr, nullptr,
                                                        NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr);
    composition::ProjectMetadata meta = session->getMetadata();
    meta.sampleRate = 48000;
    meta.initialTempoBPM = 100.0f;
    meta.timeSignatureNumerator = 4;
    meta.timeSignatureDenominator = 4;
    session->setMetadata(meta);
    composition::TrackCreateInfo info{};
    info.type = composition::TrackType::INSTRUMENT;
    info.audioChannelCount = 2;
    const TrackID track = session->getTrackManager()->createTrack(info);
    auto* tracks = session->getTrackManager();
    sessions.setActiveSession(std::move(session));

    auto tempo = Layer2::ITempoService::create();
    tempo->setSampleRate(48000.0);
    tempo->setTempoAtPosition(100.0, 0);
    auto strings = Layer2::IStringRegistry::create();
    bridge::ArrangementController arrangement(&sessions, strings.get());
    arrangement.setTempoService(tempo.get());

    // A 4-bar clip with a note on bar 1 and one on bar 4, beat 3, placed as the editor does.
    const bridge::RegionID original = arrangement.insertMidiClip(track, 0, 4 * kBar);
    REQUIRE(original.isValid());
    std::vector<composition::IPlaylist::RegionInfo> regions(8);
    REQUIRE(tracks->getPlaylist(track)->getAllRegions(regions.data(), 8) == 1);
    const uint64_t originalSource = regions[0].region.sourceId.toRaw();
    auto* sequencer = tracks->getMIDISequencer(track);
    for (const uint64_t start : {uint64_t{0}, 3 * kBar + 2 * kBeat}) {
        composition::MIDINote note{};
        note.pitch = 36;
        note.velocity = 100;
        note.offsetSample = start;
        note.durationSample = kBeat / 2;
        note.startSample = start;
        note.endSample = start + kBeat / 2;
        const Layer2::BBTPosition begin = tempo->samplesToBBT(note.startSample);
        const Layer2::BBTPosition end = tempo->samplesToBBT(note.endSample);
        note.startPosition = {begin.bar, static_cast<uint16_t>(begin.beat), begin.tick, 0};
        note.endPosition = {end.bar, static_cast<uint16_t>(end.beat), end.tick, 0};
        sequencer->addNote(ClipID::fromRaw(originalSource), note);
    }

    const bridge::RegionID copy = arrangement.duplicateMidiRegion(original, 4 * kBar);
    REQUIRE(copy.isValid());

    REQUIRE(tracks->getPlaylist(track)->getAllRegions(regions.data(), 8) == 2);
    uint64_t copySource = 0;
    for (uint32_t i = 0; i < 2; ++i) {
        if (regions[i].id.id == copy.id) {
            copySource = regions[i].region.sourceId.toRaw();
            CHECK(regions[i].region.positionSample == 4 * kBar);
            CHECK(regions[i].region.sourceLength == 4 * kBar);
        }
    }
    REQUIRE(copySource != 0);
    CHECK(copySource != originalSource);  // its own notes, not shared with the original

    const auto checkCopy = [&]() {
        const auto copied = notesIn(sequencer, copySource);
        REQUIRE(copied.size() == 2);
        CHECK(copied[0].offsetSample == 0);
        CHECK(copied[0].startPosition.bar == 5);
        CHECK(copied[1].offsetSample == 3 * kBar + 2 * kBeat);
        CHECK(copied[1].startPosition.bar == 8);
        CHECK(copied[1].startPosition.beat == 3);
        CHECK(notesIn(sequencer, originalSource).size() == 2);
    };
    checkCopy();
    // Positions recomputed from bars and beats (on a tempo change, a project load) stay put.
    tracks->recalculateTimeCaches(tempo.get());
    checkCopy();
}
