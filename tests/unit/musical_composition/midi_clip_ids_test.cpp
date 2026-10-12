// tests/unit/musical_composition/midi_clip_ids_test.cpp
//
// MIDI clips are identified by ids from a process-wide counter. After a relaunch, a project's
// clips must not see their ids handed out again: the new clip would share their notes.

#include <catch2/catch_test_macros.hpp>
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/interfaces/itrack_pipeline_builder.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/playlist/iplaylist.h"
#include "musical_composition/midi_sequencer/imidi_sequencer.h"
#include "musical_composition/musical_primitives.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace composition;

namespace {

class TimelineOnlyBuilder : public ITrackPipelineBuilder {
public:
    TrackPipelineDescriptor buildPipeline(const TrackCreateInfo&, IDSPKernel*) override { return {}; }
    void destroyPipeline(const TrackPipelineDescriptor&, IDSPKernel*) override {}
};

std::unique_ptr<IProjectSession> newSession() {
    auto session = IProjectSession::create(std::make_unique<TimelineOnlyBuilder>(), nullptr, nullptr, nullptr,
                                           NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr);
    ProjectMetadata meta = session->getMetadata();
    meta.sampleRate = 48000;
    meta.initialTempoBPM = 100.0f;
    session->setMetadata(meta);
    return session;
}

} // namespace

TEST_CASE("A reopened project's MIDI clip ids are not handed out again", "[Layer5][ClipIds]") {
    const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/clip_ids_test.agdaw";

    uint32_t savedClipId = 0;
    {
        auto session = newSession();
        TrackCreateInfo info{};
        info.type = TrackType::INSTRUMENT;
        info.audioChannelCount = 2;
        const TrackID track = session->getTrackManager()->createTrack(info);
        const ClipID clip{++getGlobalClipIdCounter(), 1};
        savedClipId = clip.id;
        TimelineRegion region{};
        region.type = RegionType::MIDI;
        region.sourceId = SourceID::fromRaw(clip.toRaw());
        region.sourceLength = 115200;
        session->getTrackManager()->getPlaylist(track)->addRegion(region);
        session->getTrackManager()->getMIDISequencer(track)->updateClipPosition(clip, 0, 115200);
        REQUIRE(session->saveToFile(path));
    }

    // A new launch: the counter starts over.
    getGlobalClipIdCounter() = 1000;
    REQUIRE(savedClipId > 1000);

    auto reopened = newSession();
    REQUIRE(reopened->loadFromFile(path));
    const uint32_t nextClipId = ++getGlobalClipIdCounter();
    CHECK(nextClipId > savedClipId);

    std::remove(path.c_str());
}
