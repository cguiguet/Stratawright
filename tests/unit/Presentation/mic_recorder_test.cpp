#include <catch2/catch_test_macros.hpp>

#include "support/bridge_controller_stubs.h"
#include "Presentation/views/chat/chat_references.h"
#include "Presentation/views/chat/mic_recorder.h"

#include <QCoreApplication>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using presentation::chat::MicRecorder;

namespace {

// 100 BPM in 4/4 at 48 kHz.
constexpr uint64_t kBeat = 28800;
constexpr uint64_t kBar = 4 * kBeat;
uint64_t barStart(uint32_t bar) { return (bar - 1) * kBar; }

// What the fakes were asked to do, in order.
std::vector<std::string> gCalls;

struct FakeArrangement : bridge::test_stubs::StubArrangementController {
    std::vector<bridge::VisualRegion> regions;

    uint32_t getRegionsInViewport(uint64_t, uint64_t, bridge::VisualRegion* out, uint32_t max) const override {
        const auto count = static_cast<uint32_t>(std::min<size_t>(regions.size(), max));
        std::copy_n(regions.begin(), count, out);
        return count;
    }
    bool getVisualRegion(bridge::RegionID id, bridge::VisualRegion& out) const override {
        for (const auto& region : regions) {
            if (region.id.id == id.id) {
                out = region;
                return true;
            }
        }
        return false;
    }
    void trimRegion(bridge::RegionID id, uint64_t position, uint64_t sourceStart, uint64_t duration) override {
        for (auto& region : regions) {
            if (region.id.id == id.id) {
                region.startFrame = position;
                region.fileOffsetFrames = sourceStart;
                region.durationFrames = duration;
            }
        }
    }
    void updateRegionMetadata(bridge::RegionID id, const char* name, const char*, uint32_t) override {
        for (auto& region : regions) {
            if (region.id.id == id.id) {
                std::strncpy(region.name, name, sizeof(region.name) - 1);
            }
        }
    }
    void deleteRegion(bridge::RegionID id) override {
        std::erase_if(regions, [id](const bridge::VisualRegion& region) { return region.id.id == id.id; });
    }
};

struct FakeTrack : bridge::test_stubs::StubTrackController {
    std::vector<bridge::TrackUIState> tracks;

    TrackID addAudioTrack(const char* name, uint32_t, uint32_t) override {
        bridge::TrackUIState track{};
        track.trackId = TrackID{static_cast<uint32_t>(tracks.size() + 1), 1};
        std::strncpy(track.name, name, sizeof(track.name) - 1);
        track.type = composition::TrackType::AUDIO;
        tracks.push_back(track);
        gCalls.push_back("create track " + std::string(name));
        return track.trackId;
    }
    bridge::TrackUIState* find(TrackID id) {
        for (auto& track : tracks) {
            if (track.trackId.id == id.id) return &track;
        }
        return nullptr;
    }
    std::vector<bridge::TrackUIState> getAllTracks() const override { return tracks; }
    bridge::TrackUIState getTrackState(TrackID id) const override {
        for (const auto& track : tracks) {
            if (track.trackId.id == id.id) return track;
        }
        return {};
    }
    std::vector<bridge::AudioInputChannelDescriptor> getAvailableAudioInputs() const override {
        return {{0, 1, "Input 1"}, {1, 1, "Input 2"}, {0, 2, "Input 1-2"}};
    }
    void setTrackInput(TrackID, uint32_t option, uint32_t channels) override {
        gCalls.push_back("input " + std::to_string(option) + "x" + std::to_string(channels));
    }
    void setInputMonitoring(TrackID id, bool on) override {
        find(id)->isInputMonitoring = on;
        gCalls.push_back(on ? "monitor on" : "monitor off");
    }
    void setMute(TrackID id, bool mute) override { find(id)->isMuted = mute; }
    void setRecordArmed(TrackID id, bool armed) override {
        find(id)->isRecordArmed = armed;
        gCalls.push_back(armed ? "arm track" : "disarm track");
    }
};

struct FakeTimeline : bridge::test_stubs::StubTimelineController {
    FakeArrangement* arrangement = nullptr;
    TrackID* recordingTrack = nullptr;
    uint64_t frame = 0;
    uint64_t takeStart = 0;
    bool playing = false, recording = false, armed = false, metronome = false, looping = true;

    void play() override {
        playing = true;
        recording = armed;
        takeStart = frame;
        gCalls.push_back("play");
    }
    void stop() override {
        if (recording) {
            // The take is committed as a clip from where recording started.
            bridge::VisualRegion take{};
            take.id = bridge::RegionID{7, 0};
            take.trackId = *recordingTrack;
            take.startFrame = takeStart;
            take.durationFrames = frame - takeStart;
            take.clipType = composition::RegionType::AUDIO;
            std::strcpy(take.name, "Track 1_Take_1.wav");
            arrangement->regions.push_back(take);
        }
        playing = recording = false;
        gCalls.push_back("stop");
    }
    void setRecordArmed(bool on) override { armed = on; }
    void seekToFrame(uint64_t position) override {
        frame = position;
        gCalls.push_back("seek bar " + std::to_string(position / kBar + 1));
    }
    void setLoopEnabled(bool on) override { looping = on; }
    void setMetronomeEnabled(bool on) override { metronome = on; }
    uint64_t getCurrentFrame() const override { return frame; }
    bool isPlaying() const override { return playing; }
    bool isRecording() const override { return recording; }
    bool isRecordArmed() const override { return armed; }
    bool isLooping() const override { return looping; }
    bool isMetronomeEnabled() const override { return metronome; }
    void frameToBBT(uint64_t f, uint32_t& bar, uint32_t& beat, uint32_t& tick) const override {
        bar = static_cast<uint32_t>(f / kBar) + 1;
        beat = static_cast<uint32_t>((f % kBar) / kBeat) + 1;
        tick = static_cast<uint32_t>((f % kBeat) / 30);
    }
    uint64_t bbtToFrame(uint32_t bar, uint32_t beat, uint32_t tick) const override {
        return barStart(bar) + (beat - 1) * kBeat + tick * 30;
    }
};

struct FakeHardware : bridge::test_stubs::StubHardwareSettingsFacade {
    std::string output = "Bose QC Ultra";
    bridge::HardwareConfig getCurrentConfig() override {
        bridge::HardwareConfig config;
        config.inputDeviceIndex = 3;
        config.outputDeviceIndex = 4;
        config.numInputChannels = 2;
        return config;
    }
    std::vector<bridge::AudioDeviceDescriptor> getAvailableDevices() override {
        bridge::AudioDeviceDescriptor input{}, out{};
        input.deviceIndex = 3;
        std::strcpy(input.name, "Yamaha Steinberg: Steinberg UR12");
        out.deviceIndex = 4;
        std::strncpy(out.name, output.c_str(), sizeof(out.name) - 1);
        return {input, out};
    }
};

struct Setup {
    FakeTrack track;
    FakeTimeline timeline;
    FakeArrangement arrangement;
    FakeHardware hardware;
    TrackID mic = TrackID::invalid();
    MicRecorder recorder;
    std::vector<QString> started;     // descriptions
    std::vector<QString> references;  // one per finished take

    Setup() {
        gCalls.clear();
        timeline.arrangement = &arrangement;
        timeline.recordingTrack = &mic;
        recorder.setControllers({&track, &timeline, &arrangement, &hardware});
        QObject::connect(&recorder, &MicRecorder::started, [this](const QString& description) { started.push_back(description); });
        QObject::connect(&recorder, &MicRecorder::finished, [this](const QString& reference, const QString&) { references.push_back(reference); });
    }
    TrackID micTrack() {
        for (const auto& t : track.tracks) {
            if (std::string(t.name) == "Mic") return t.trackId;
        }
        return TrackID::invalid();
    }
};

int argc = 1;
char appName[] = "presentation_unit_tests";
char* argv[] = {appName, nullptr};

} // namespace

TEST_CASE("/mic records the chosen input after a count-in and hands back the clip", "[presentation][chat][mic]") {
    QCoreApplication app(argc, argv);
    Setup s;
    s.timeline.frame = barStart(5) + 2 * kBeat;  // playhead on bar 5, beat 3

    const auto inputs = s.recorder.inputs();
    REQUIRE(inputs.size() == 2);  // the mono inputs only
    CHECK(inputs[0].label == QStringLiteral("Steinberg UR12 · Input 1"));
    CHECK(inputs[1].channel == 1);

    REQUIRE(s.recorder.start(inputs[1]).isEmpty());
    s.mic = s.micTrack();
    REQUIRE(s.mic.isValid());
    CHECK(s.recorder.isRecording());
    REQUIRE(s.started.size() == 1);
    CHECK(s.started[0].contains(QStringLiteral("count-in on bar 4, the take starts on bar 5")));

    // Input chosen before arming, no monitoring, one bar of count-in, then recording.
    const std::vector<std::string> expected{"create track Mic", "input 1x1", "monitor off", "seek bar 4", "arm track", "play"};
    CHECK(gCalls == expected);
    CHECK(s.track.find(s.mic)->isMuted);
    CHECK(s.timeline.metronome);
    CHECK_FALSE(s.timeline.looping);

    s.timeline.frame = barStart(9);  // four bars after the count-in
    s.recorder.stop();

    CHECK_FALSE(s.recorder.isRecording());
    REQUIRE(s.references.size() == 1);
    CHECK(s.references[0]
          == QStringLiteral("@clip 7 · audio \"Mic take\" · track %1 \"Mic\" · bars 5.1.0 → 9.1.0").arg(s.mic.id));
    const auto& clip = s.arrangement.regions.front();
    CHECK(clip.startFrame == barStart(5));  // count-in trimmed off
    CHECK(clip.fileOffsetFrames == kBar);
    CHECK(clip.durationFrames == 4 * kBar);

    // Everything put back, the track disarmed, the playhead on the take.
    CHECK_FALSE(s.track.find(s.mic)->isRecordArmed);
    CHECK_FALSE(s.track.find(s.mic)->isMuted);
    CHECK_FALSE(s.timeline.armed);
    CHECK_FALSE(s.timeline.metronome);
    CHECK(s.timeline.looping);
    CHECK(s.timeline.frame == barStart(5));
}

TEST_CASE("/mic on bar 1 counts in on bar 1 and records from bar 2", "[presentation][chat][mic]") {
    QCoreApplication app(argc, argv);
    Setup s;
    REQUIRE(s.recorder.start(s.recorder.inputs().front()).isEmpty());
    s.mic = s.micTrack();
    CHECK(s.timeline.frame == barStart(1));
    s.timeline.frame = barStart(3);
    s.recorder.stop();
    REQUIRE(s.references.size() == 1);
    CHECK(s.references[0].endsWith(QStringLiteral("bars 2.1.0 → 3.1.0")));
}

TEST_CASE("/mic plays no click through the computer's speakers", "[presentation][chat][mic]") {
    QCoreApplication app(argc, argv);
    Setup s;
    s.hardware.output = "Apple Inc.: MacBook Air Speakers";
    REQUIRE(s.recorder.start(s.recorder.inputs().front()).isEmpty());
    CHECK_FALSE(s.timeline.metronome);
    REQUIRE(s.started.size() == 1);
    CHECK(s.started[0].contains(QStringLiteral("No click")));
    s.recorder.stop();
}

TEST_CASE("/mic refuses what would go wrong, and keeps nothing from a count-in only", "[presentation][chat][mic]") {
    QCoreApplication app(argc, argv);
    {
        Setup s;
        s.timeline.playing = true;
        CHECK_FALSE(s.recorder.start(s.recorder.inputs().front()).isEmpty());
        CHECK(gCalls.empty());
    }
    {
        Setup s;
        s.track.addAudioTrack("Guitar", 2, 0);
        s.track.tracks.back().isRecordArmed = true;
        gCalls.clear();
        CHECK(s.recorder.start(s.recorder.inputs().front()).contains(QStringLiteral("\"Guitar\" is armed")));
        CHECK(gCalls.empty());
    }
    {
        Setup s;
        s.timeline.frame = barStart(5);
        REQUIRE(s.recorder.start(s.recorder.inputs().front()).isEmpty());
        s.mic = s.micTrack();
        s.timeline.frame = barStart(4) + kBeat;  // stopped during the count-in
        s.recorder.stop();
        REQUIRE(s.references.size() == 1);
        CHECK(s.references[0].isEmpty());
        CHECK(s.arrangement.regions.empty());
    }
}

TEST_CASE("/mic helpers", "[presentation][chat][mic]") {
    CHECK(MicRecorder::takeBar(1) == 2);
    CHECK(MicRecorder::takeBar(2) == 2);
    CHECK(MicRecorder::takeBar(7) == 7);
    CHECK(MicRecorder::shortDeviceName(QStringLiteral("Yamaha Steinberg: Steinberg UR12")) == QStringLiteral("Steinberg UR12"));
    CHECK(MicRecorder::shortDeviceName(QStringLiteral("Bose QC Ultra")) == QStringLiteral("Bose QC Ultra"));
    CHECK(MicRecorder::isBuiltInSpeakers(QStringLiteral("Apple Inc.: MacBook Air Speakers")));
    CHECK_FALSE(MicRecorder::isBuiltInSpeakers(QStringLiteral("Bose QC Ultra 2 Clem")));
}
