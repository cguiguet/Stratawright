// tests/unit/musical_composition/sample_rate_conversion_test.cpp
//
// Timeline positions are stored in samples, so moving a project to another sample rate must
// convert them or everything drifts off the beat.

#include <catch2/catch_test_macros.hpp>
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/interfaces/itrack_pipeline_builder.h"
#include "musical_composition/interfaces/imarker_manager.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/playlist/iplaylist.h"
#include "musical_composition/midi_sequencer/midi_sequencer_impl.h"
#include "musical_composition/automation/iautomation_lane.h"
#include "musical_composition/automation/iautomation_lane_manager.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/sample_rate_conversion.h"
#include <memory>
#include <vector>

using namespace composition;

namespace {

class TimelineOnlyBuilder : public ITrackPipelineBuilder {
public:
    TrackPipelineDescriptor buildPipeline(const TrackCreateInfo&, IDSPKernel*) override { return {}; }
    void destroyPipeline(const TrackPipelineDescriptor&, IDSPKernel*) override {}
};

// At 100 BPM in 4/4 a bar lasts 2.4 s.
constexpr uint64_t kBarAt44k = 105840;
constexpr uint64_t kBarAt48k = 115200;

} // namespace

TEST_CASE("Sample rate conversion rounds to the nearest sample and keeps touching items touching", "[Layer5][SampleRate]") {
    const SampleRateConversion up{44100, 48000};
    CHECK(up(8 * kBarAt44k) == 8 * kBarAt48k);
    CHECK(up(uint64_t{1}) == 1);  // 1.088 -> 1
    CHECK(up(uint64_t{6}) == 7);  // 6.53 -> 7
    CHECK(up.length(3, 5) == up(uint64_t{8}) - up(uint64_t{3}));
    CHECK(up(std::numeric_limits<uint64_t>::max()) == std::numeric_limits<uint64_t>::max());
    CHECK(up(uint32_t{441}) == 480u);
    const SampleRateConversion same{48000, 48000};
    CHECK_FALSE(same.changesAnything());
    CHECK(same(uint64_t{12345}) == 12345);
}

TEST_CASE("Converting a project to another sample rate keeps everything on the same beat", "[Layer5][SampleRate]") {
    auto session = IProjectSession::create(std::make_unique<TimelineOnlyBuilder>(), nullptr, nullptr, nullptr,
                                           NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr);
    REQUIRE(session != nullptr);
    ProjectMetadata meta = session->getMetadata();
    meta.sampleRate = 44100;
    session->setMetadata(meta);

    auto* tracks = session->getTrackManager();
    TrackCreateInfo info{};
    info.type = TrackType::INSTRUMENT;
    info.audioChannelCount = 2;
    const TrackID track = tracks->createTrack(info);

    // A MIDI clip on bars 9-16 with a note on its second beat.
    const ClipID clip = ClipID::fromRaw((uint64_t{1} << 32) | 5);
    auto* sequencer = dynamic_cast<MIDISequencerImpl*>(tracks->getMIDISequencer(track));
    REQUIRE(sequencer != nullptr);
    sequencer->updateClipPosition(clip, 8 * kBarAt44k, 8 * kBarAt44k, composition::MusicalPosition{});
    composition::MIDINote note{};
    note.offsetSample = kBarAt44k / 4;
    note.durationSample = kBarAt44k / 8;
    note.startSample = 8 * kBarAt44k + note.offsetSample;
    note.endSample = note.startSample + note.durationSample;
    note.pitch = 60;
    note.velocity = 100;
    sequencer->addNote(clip, note);

    // A region for that clip, and an audio region on bar 3 (its source is measured in samples
    // of its file, which keeps its own rate).
    auto* playlist = tracks->getPlaylist(track);
    REQUIRE(playlist != nullptr);
    TimelineRegion midiRegion{};
    midiRegion.type = RegionType::MIDI;
    midiRegion.positionSample = 8 * kBarAt44k;
    midiRegion.sourceLength = 8 * kBarAt44k;
    playlist->addRegion(midiRegion);
    TimelineRegion audioRegion{};
    audioRegion.type = RegionType::AUDIO;
    audioRegion.positionSample = 2 * kBarAt44k;
    audioRegion.sourceStartSample = 1000;
    audioRegion.sourceLength = 44100;
    audioRegion.fadeInSamples = 441;
    playlist->addRegion(audioRegion);

    // Automation on bar 2 and a marker on bar 5.
    AutomationTarget target{};
    target.nodeId = NodeID{1, 1};
    target.cachedParameterIndex = 3;
    IAutomationLane* lane = tracks->getAutomationManager(track)->createLane(target);
    REQUIRE(lane != nullptr);
    lane->addPoint(kBarAt44k, 0.5f);
    MarkerUUID markerId{};
    markerId.bytes[0] = 1;
    session->getMarkerManager()->addMarker(markerId, 4 * kBarAt44k, "Solo", 0xFFFFFFFF);

    REQUIRE(session->getCommandHistory()->isDirty());

    session->convertSampleRate(48000);

    CHECK(session->getMetadata().sampleRate == 48000);

    const auto& clips = sequencer->getRawClipPositions();
    REQUIRE(clips.size() == 1);
    CHECK(clips[0].positionSample == 8 * kBarAt48k);
    CHECK(clips[0].sourceLength == 8 * kBarAt48k);
    composition::MIDINote notes[4];
    REQUIRE(sequencer->getNotesInClip(clip, notes, 4) == 1);
    CHECK(notes[0].offsetSample == kBarAt48k / 4);
    CHECK(notes[0].durationSample == kBarAt48k / 8);
    CHECK(notes[0].startSample == 8 * kBarAt48k + kBarAt48k / 4);

    std::vector<IPlaylist::RegionInfo> regions(8);
    const uint32_t regionCount = playlist->getAllRegions(regions.data(), 8);
    REQUIRE(regionCount == 2);
    for (uint32_t i = 0; i < regionCount; ++i) {
        const TimelineRegion& region = regions[i].region;
        if (region.type == RegionType::MIDI) {
            CHECK(region.positionSample == 8 * kBarAt48k);
            CHECK(region.sourceLength == 8 * kBarAt48k);
        } else {
            CHECK(region.positionSample == 2 * kBarAt48k);
            CHECK(region.sourceStartSample == 1000);
            CHECK(region.sourceLength == 44100);
            CHECK(region.fadeInSamples == 480);
        }
    }

    ::AutomationPoint points[4];
    REQUIRE(lane->getPoints(points, 4) == 1);
    CHECK(points[0].positionSample == kBarAt48k);

    MarkerInfo markers[4];
    REQUIRE(session->getMarkerManager()->getMarkersInRange(0, std::numeric_limits<uint64_t>::max(), markers, 4) == 1);
    CHECK(markers[0].framePosition == 4 * kBarAt48k);

    // The undo steps held positions at the old rate: they are gone, but the project still has
    // unsaved changes.
    CHECK_FALSE(session->getCommandHistory()->canUndo());
    CHECK(session->getCommandHistory()->isDirty());
}
