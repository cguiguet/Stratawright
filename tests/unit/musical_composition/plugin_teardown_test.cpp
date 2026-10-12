// tests/unit/musical_composition/plugin_teardown_test.cpp
//
// The audio thread may be running a plugin at any time, so the track manager must take a plugin
// out of the audio graph, wait for the audio blocks in flight, and only then free it.

#include <catch2/catch_test_macros.hpp>
#include "musical_composition/track_manager/track_manager_impl.h"
#include "musical_composition/interfaces/itrack_pipeline_builder.h"
#include "Core audio engine/scheduler/idsp_kernel.h"
#include "Core audio engine/plugin/iplugin.h"
#include "Core audio engine/plugin/iplugin_manager.h"
#include "DSP nodes/plugins/insert_plugin_node.h"
#include "DSP nodes/plugins/instrument_slot_node.h"
#include "DSP nodes/tracks/instrument_track_node.h"
#include <cstring>
#include <functional>
#include <memory>
#include <set>

using namespace composition;

namespace {

std::set<const void*> gLivePlugins;

class TrackedPlugin : public Layer3::IPlugin {
public:
    TrackedPlugin() { gLivePlugins.insert(this); }
    ~TrackedPlugin() override { gLivePlugins.erase(this); }

    void processAudio(float* const*, uint32_t, float* const*, uint32_t, uint32_t, const EventData*,
                      uint32_t, EventData*, uint32_t*, const ProcessContext*, const bool* = nullptr) override {}
    bool getInfo(PluginInfo&) const override { return true; }
    float getParameterValue(uint32_t) const override { return 0.0f; }
    void setParameterValue(uint32_t, float) override {}
    bool getParameterInfo(uint32_t, ::ParameterInfo&) const override { return false; }
    void setParameterTweakedCallback(ParameterTweakedCallback) override {}
    bool openEditor(void*, int&, int&) override { return false; }
    void closeEditor() override {}
    std::vector<uint8_t> getState() const override { return {}; }
    bool loadState(const uint8_t*, uint64_t) override { return true; }
};

class TrackedPluginManager : public Layer3::IPluginManager {
public:
    std::vector<PluginDescriptor> getAvailablePlugins() const override {
        PluginDescriptor plugin{};
        plugin.pluginId = 7;
        std::strncpy(plugin.name, "Tracked", MAX_PLUGIN_NAME_LENGTH);
        return {plugin};
    }
    std::unique_ptr<Layer3::IPlugin> instantiatePlugin(const PluginDescriptor&) override {
        return std::make_unique<TrackedPlugin>();
    }
    void scanForPlugins(const std::vector<std::string>&) override {}
    bool isScanning() const override { return false; }
    float getScanProgress() const override { return 0.0f; }
    std::string getCurrentlyScanningPlugin() const override { return ""; }
};

// Builds instrument tracks the way the real builder does (the instrument slot is embedded in the
// track node) and reports what the graph still referenced when it was torn down.
class InstrumentPipelineBuilder : public ITrackPipelineBuilder {
public:
    TrackPipelineDescriptor buildPipeline(const TrackCreateInfo&, IDSPKernel*) override {
        TrackPipelineDescriptor desc{};
        if (auto node = DSP::InstrumentTrackFactory::getRegistry().allocate()) {
            desc.trackNode = *node;
            desc.instrumentSlotNode = *node;
            DSP::InstrumentTrackFactory::getRegistry().get(*node)->instrumentSlot.reset();
            DSP::InstrumentTrackFactory::getRegistry().get(*node)->pluginSlot.reset();
        }
        lastPipeline = desc;
        return desc;
    }
    void destroyPipeline(const TrackPipelineDescriptor& desc, IDSPKernel*) override {
        if (auto* track = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            instrumentStillInGraph = track->instrumentSlot.pluginInstance != nullptr;
            DSP::InstrumentTrackFactory::getRegistry().deallocate(desc.trackNode);
        }
    }

    TrackPipelineDescriptor lastPipeline{};
    bool instrumentStillInGraph = false;
};

// Stands in for the audio kernel: lets the test look at the state while it "waits for the audio
// thread".
class WaitObservingKernel : public Layer3::IDSPKernel {
public:
    void attachMutationBridge(Layer2::IMutationBridge*) override {}
    void attachTelemetryBridge(Layer2::ITelemetryBridge*) override {}
    void attachEventQueue(Layer2::IEventQueue*) override {}
    void attachSidechainManager(Layer3::ISidechainManager*) override {}
    void registerProcessor(uint32_t, ::DSPProcessFunc) override {}
    void unregisterProcessor(uint32_t) override {}
    void registerFactory(uint32_t, DSP::IDSPNodeFactory*) override {}
    uint32_t getNodeLatency(NodeID) const override { return 0; }
    void setNodeLatency(NodeID, uint32_t) override {}
    uint32_t getTotalLatency() const override { return 0; }
    void applyPDC() override {}
    void setPDCCrossfade(uint32_t) override {}
    void process(float* const*, float* const*, uint32_t, uint32_t, const ProcessContext*) override {}
    void setLiveMIDITargets(const NodeID*, uint32_t) override {}
    void publishTimelineSnapshot(const TimelineSnapshot&) override {}
    const TimelineSnapshot* getActiveTimelineSnapshot() const override { return nullptr; }
    uint32_t getTopologyVersion() const override { return 0; }
    uint32_t getNodeCount() const override { return 0; }
    uint32_t getConnectionCount() const override { return 0; }
    bool hasCycles() const override { return false; }
    bool waitForBlocksInFlight(uint32_t) override {
        ++waits;
        if (onWait) onWait();
        return true;
    }

    int waits = 0;
    std::function<void()> onWait;
};

struct Fixture {
    WaitObservingKernel kernel;
    TrackedPluginManager plugins;
    InstrumentPipelineBuilder* builder = nullptr;
    std::unique_ptr<TrackManagerImpl> tracks;
    TrackID track = TrackID::invalid();

    Fixture() {
        gLivePlugins.clear();
        auto ownedBuilder = std::make_unique<InstrumentPipelineBuilder>();
        builder = ownedBuilder.get();
        tracks = std::make_unique<TrackManagerImpl>(std::move(ownedBuilder), nullptr, nullptr, &kernel, nullptr,
                                                    &plugins, NodeID::invalid(), NodeID::invalid(),
                                                    NodeID::invalid(), nullptr);
        TrackCreateInfo info{};
        info.type = TrackType::INSTRUMENT;
        info.audioChannelCount = 2;
        track = tracks->createTrack(info);
    }

    DSP::InstrumentTrackNodeState* trackNode() const {
        return DSP::InstrumentTrackFactory::getRegistry().get(builder->lastPipeline.trackNode);
    }
};

} // namespace

TEST_CASE("Deleting a track frees its plugins only after the audio thread let go", "[Layer5][PluginTeardown]") {
    Fixture f;
    f.tracks->insertTrackInstrument(f.track, 7);
    f.tracks->insertTrackPlugin(f.track, 0, 7);
    REQUIRE(f.trackNode() != nullptr);
    const void* instrument = f.trackNode()->instrumentSlot.pluginInstance;
    REQUIRE(instrument != nullptr);
    REQUIRE(gLivePlugins.size() == 2);

    bool aliveDuringWait = false;
    f.kernel.onWait = [&] { aliveDuringWait = gLivePlugins.size() == 2; };
    f.tracks->deleteTrack(f.track);

    CHECK_FALSE(f.builder->instrumentStillInGraph);  // taken out before the graph was torn down
    CHECK(f.kernel.waits >= 1);
    CHECK(aliveDuringWait);                          // still alive while a block may run them
    CHECK(gLivePlugins.empty());                     // freed afterwards
}

TEST_CASE("Replacing an instrument frees the old one only after the audio thread let go", "[Layer5][PluginTeardown]") {
    Fixture f;
    f.tracks->insertTrackInstrument(f.track, 7);
    const void* oldInstrument = f.trackNode()->instrumentSlot.pluginInstance;
    REQUIRE(oldInstrument != nullptr);

    const void* slotDuringWait = nullptr;
    bool oldAliveDuringWait = false;
    f.kernel.onWait = [&] {
        slotDuringWait = f.trackNode()->instrumentSlot.pluginInstance;
        oldAliveDuringWait = gLivePlugins.count(oldInstrument) == 1;
    };
    f.tracks->insertTrackInstrument(f.track, 7);

    const void* newInstrument = f.trackNode()->instrumentSlot.pluginInstance;
    CHECK(newInstrument != oldInstrument);
    CHECK(slotDuringWait == newInstrument);  // the audio thread already sees the new one
    CHECK(oldAliveDuringWait);
    CHECK(gLivePlugins.count(oldInstrument) == 0);
    CHECK(gLivePlugins.count(newInstrument) == 1);
}

TEST_CASE("Removing an insert plugin frees it only after the audio thread let go", "[Layer5][PluginTeardown]") {
    Fixture f;
    f.tracks->insertTrackPlugin(f.track, 0, 7);
    const NodeID pluginNode = f.trackNode()->pluginSlot.slots[0];
    REQUIRE(pluginNode.isValid());
    REQUIRE(gLivePlugins.size() == 1);

    bool unlinkedDuringWait = false;
    bool aliveDuringWait = false;
    f.kernel.onWait = [&] {
        unlinkedDuringWait = !f.trackNode()->pluginSlot.slots[0].isValid();
        aliveDuringWait = gLivePlugins.size() == 1;
    };
    f.tracks->removeTrackPlugin(f.track, 0);

    CHECK(unlinkedDuringWait);
    CHECK(aliveDuringWait);
    CHECK(gLivePlugins.empty());
    CHECK(DSP::InsertPluginFactory::getRegistry().get(pluginNode) == nullptr);  // its node is released too
}
