#include <catch2/catch_test_macros.hpp>

#include "server/handlers/clip_handler.h"
#include "common/parsed_args.h"
#include "support/bridge_controller_stubs.h"

#include <string>
#include <vector>

namespace {

// 100 BPM in 4/4 at 48 kHz.
constexpr uint64_t kBar = 115200;

struct BarsTimeline : bridge::test_stubs::StubTimelineController {
    uint64_t bbtToFrame(uint32_t bar, uint32_t beat, uint32_t tick) const override {
        return (bar - 1) * kBar + (beat - 1) * (kBar / 4) + tick * 30;
    }
    double getSampleRate() const override { return 48000.0; }
};

// One clip; records where copies go and how they get cut.
struct CopyingArrangement : bridge::test_stubs::StubArrangementController {
    bridge::VisualRegion clip{};
    std::vector<uint64_t> copiesAt;
    std::vector<uint64_t> trimmedTo;  // durations of trimmed copies
    uint32_t nextId = 100;

    CopyingArrangement(composition::RegionType type, uint32_t bars) {
        clip.id = bridge::RegionID{7, 0};
        clip.clipType = type;
        clip.durationFrames = bars * kBar;
    }
    bool getVisualRegion(bridge::RegionID id, bridge::VisualRegion& out) const override {
        out = clip;
        out.id = id;
        return true;
    }
    bridge::RegionID duplicateMidiRegion(bridge::RegionID, uint64_t at) override {
        copiesAt.push_back(at);
        return {nextId++, 1};
    }
    bridge::RegionID duplicateAudioRegion(bridge::RegionID, uint64_t at) override {
        copiesAt.push_back(at);
        return {nextId++, 1};
    }
    void trimRegion(bridge::RegionID, uint64_t, uint64_t, uint64_t duration) override { trimmedTo.push_back(duration); }
};

agentic::ExecutionResult run(const std::string& command, CopyingArrangement& arrangement) {
    BarsTimeline timeline;
    return agentic::ClipHandler::handleCommand(agentic::ParsedArgs::parseCommandLine(command), &arrangement, nullptr, &timeline);
}

} // namespace

TEST_CASE("clip duplicate fills up to a bar", "[agentic][clip]") {
    // A 4-bar loop on bars 1-4, repeated until bar 16 included.
    CopyingArrangement loop(composition::RegionType::MIDI, 4);
    const auto result = run("clip duplicate --track 1 --clip 7 --until 17.1.0", loop);
    INFO(result.symbol << ": " << result.message);
    REQUIRE(result.isSuccess());
    CHECK(loop.copiesAt == std::vector<uint64_t>{4 * kBar, 8 * kBar, 12 * kBar});
    CHECK(loop.trimmedTo.empty());
    CHECK(result.fields.at("copies") == "100,101,102");

    // A 3-bar audio clip up to bar 8 included: the last copy is cut to 2 bars.
    CopyingArrangement take(composition::RegionType::AUDIO, 3);
    REQUIRE(run("clip duplicate --track 2 --clip 7 --until 9.1.0", take).isSuccess());
    CHECK(take.copiesAt == std::vector<uint64_t>{3 * kBar, 6 * kBar});
    CHECK(take.trimmedTo == std::vector<uint64_t>{2 * kBar});
}

TEST_CASE("clip duplicate makes a number of copies", "[agentic][clip]") {
    CopyingArrangement loop(composition::RegionType::MIDI, 2);
    REQUIRE(run("clip duplicate --track 1 --clip 7 --count 3", loop).isSuccess());
    CHECK(loop.copiesAt == std::vector<uint64_t>{2 * kBar, 4 * kBar, 6 * kBar});
}

TEST_CASE("clip duplicate rejects unclear requests", "[agentic][clip]") {
    CopyingArrangement loop(composition::RegionType::MIDI, 4);
    CHECK_FALSE(run("clip duplicate --track 1 --clip 7", loop).isSuccess());
    CHECK_FALSE(run("clip duplicate --track 1 --clip 7 --count 2 --until 9.1.0", loop).isSuccess());
    CHECK_FALSE(run("clip duplicate --track 1 --clip 7 --until 3.1.0", loop).isSuccess());  // inside the clip
    CHECK_FALSE(run("clip duplicate --track 1 --clip 7 --count 0", loop).isSuccess());
    CHECK(loop.copiesAt.empty());
}
