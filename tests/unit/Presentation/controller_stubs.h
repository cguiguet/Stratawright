// tests/unit/Presentation/controller_stubs.h
//
// Do-nothing implementations of the bridge controllers the chat panel uses, generated from their
// interfaces: tests derive from them and override only what they look at.
#pragma once

#include "Middle Bridge/engine/ihardware_settings_facade.h"
#include "Middle Bridge/timeline/iarrangement_controller.h"
#include "Middle Bridge/timeline/itimeline_controller.h"
#include "Middle Bridge/tracks/itrack_controller.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"

namespace bridge::test_stubs {

class StubTrackController : public ITrackController {
public:
    std::vector<ParameterDescriptorCacheItem> getCachedParameters(TrackID trackId) const override { return {}; }
    TrackID addAudioTrack(const char* name, uint32_t channels, uint32_t colorARGB) override { return {}; }
    TrackID addInstrumentTrack(const char* name, uint32_t colorARGB) override { return {}; }
    TrackID addAuxTrack(const char* name, uint32_t colorARGB) override { return {}; }
    TrackID addFolderTrack(const char* name, uint32_t colorARGB) override { return {}; }
    void removeTrack(TrackID trackId) override {}
    void renameTrack(TrackID trackId, const char* name) override {}
    void setTrackComments(TrackID trackId, const char* comments) override {}
    void setTrackOutputRouting(TrackID trackId, TrackID destinationTrackId) override {}
    void setTrackColor(TrackID trackId, uint32_t colorARGB) override {}
    void moveTrack(TrackID trackId, uint32_t newPositionIndex, TrackID newParentFolderId) override {}
    void setTrackParentFolder(TrackID childTrackId, TrackID parentFolderId) override {}
    void setTrackMode(TrackID trackId, composition::TrackType mode) override {}
    TrackID cloneTrack(TrackID sourceId) override { return {}; }
    void muteAllClips(TrackID trackId, bool mute) override {}
    void setFaderGain(TrackID trackId, float gainLinear) override {}
    void setPan(TrackID trackId, float panPosition) override {}
    void setMute(TrackID trackId, bool mute) override {}
    void setSolo(TrackID trackId, bool solo) override {}
    void setRecordArmed(TrackID trackId, bool armed) override {}
    void setInputMonitoring(TrackID trackId, bool enabled) override {}
    void setTrackInput(TrackID trackId, uint32_t optionId, uint32_t numChannels) override {}
    std::vector<TrackInputOption> getAvailableTrackInputs(TrackID trackId) const override { return {}; }
    void setTrackAudioInput(TrackID trackId, uint32_t mappedPhysicalInputIndex, uint32_t numChannels) override {}
    std::vector<AudioInputChannelDescriptor> getAvailableAudioInputs() const override { return {}; }
    void setSendGain(TrackID trackId, bool isPreFader, uint32_t sendIndex, float gainLinear) override {}
    void setSendPan(TrackID trackId, bool isPreFader, uint32_t sendIndex, float panPosition) override {}
    void setSendEnabled(TrackID trackId, bool isPreFader, uint32_t sendIndex, bool enabled) override {}
    void setSendDestination(TrackID trackId, bool isPreFader, uint32_t sendIndex, NodeID destinationNodeId) override {}
    void insertInstrument(TrackID trackId, uint32_t pluginId) override {}
    void removeInstrument(TrackID trackId) override {}
    void setInstrumentBypassed(TrackID trackId, bool bypassed) override {}
    bool openInstrumentEditor(TrackID trackId, void* parentWindow, int& outWidth, int& outHeight) override { return {}; }
    void closeInstrumentEditor(TrackID trackId) override {}
    void insertPlugin(TrackID trackId, uint32_t slotIndex, uint32_t pluginId) override {}
    void removePlugin(TrackID trackId, uint32_t slotIndex) override {}
    void setPluginBypassed(TrackID trackId, uint32_t slotIndex, bool bypassed) override {}
    std::vector<uint8_t> getPluginState(TrackID trackId, uint32_t slotIndex) const override { return {}; }
    void setPluginState(TrackID trackId, uint32_t slotIndex, const std::vector<uint8_t>& state) override {}
    void setPluginParameter(TrackID trackId, uint32_t slotIndex, uint32_t paramIndex, float value) override {}
    void setInstrumentParameter(TrackID trackId, uint32_t paramIndex, float value) override {}
    uint32_t getPluginIdInSlot(TrackID trackId, uint32_t slotIndex) const override { return {}; }
    uint32_t findPluginIdByName(std::string_view name) const override { return {}; }
    bool openPluginEditor(TrackID trackId, uint32_t slotIndex, void* parentWindow, int& outWidth, int& outHeight) override { return {}; }
    void closePluginEditor(TrackID trackId, uint32_t slotIndex) override {}
    void subscribeToPluginParameterTweaks(TrackID trackId, uint32_t slotIndex, bool isInstrument, PluginParameterTweakedCallback cb) override {}
    void unsubscribeFromPluginParameterTweaks(TrackID trackId, uint32_t slotIndex, bool isInstrument) override {}
    void setPluginSidechainSource(TrackID targetTrackId, uint32_t slotIndex, TrackID sourceTrackId, float sendGaindB) override {}
    void clearPluginSidechainSource(TrackID targetTrackId, uint32_t slotIndex) override {}
    std::vector<TrackInputOption> getAvailableSidechainSources(TrackID targetTrackId) const override { return {}; }
    SidechainSlotUIState getPluginSidechainState(TrackID targetTrackId, uint32_t slotIndex) const override { return {}; }
    uint32_t getTrackCount() const override { return {}; }
    TrackUIState getTrackState(TrackID trackId) const override { return {}; }
    std::vector<TrackUIState> getAllTracks() const override { return {}; }
    TrackDynamicState getDynamicState(NodeID channelStripNode) const override { return {}; }
    std::vector<PluginDescriptor> getAvailablePlugins() const override { return {}; }
    void setTrackLocked(TrackID id, bool locked) override {}
    void setTrackSelected(TrackID id, bool selected) override {}
    void clearTrackSelection() override {}
    void setAutomationExpanded(TrackID id, bool expanded) override {}
    void setTakesExpanded(TrackID id, bool expanded) override {}
    void setAutomationSubLaneExpanded(TrackID id, uint32_t subLaneIndex, bool expanded) override {}
    void setAutomationSubLaneHeight(TrackID id, uint32_t subLaneIndex, uint32_t heightPx) override {}
    void setAutomationLaneRequestCallback(AutomationLaneRequestCallback cb) override {}
    void requestAutomationLaneForLastTweaked(TrackID trackId) override {}
    NodeID getMasterChannelStripNode() const override { return {}; }
};

class StubTimelineController : public ITimelineController {
public:
    void togglePlay() override {}
    void play() override {}
    void stop() override {}
    void setRecordArmed(bool armed) override {}
    void seekToFrame(uint64_t framePosition) override {}
    void seekToTimeSeconds(double seconds) override {}
    void seekToMusicalGrid(double bar, double beat) override {}
    void setBPM(double bpm) override {}
    void setTimeSignature(VisualTimeSignature timeSig) override {}
    void setLoopRange(uint64_t startFrame, uint64_t endFrame) override {}
    void setLoopEnabled(bool enabled) override {}
    bool isTempoAutomated() const override { return {}; }
    void removeTempoPoint(uint64_t framePosition) override {}
    void addTempoPoint(uint64_t framePosition, double bpm) override {}
    uint32_t getTempoPoints(uint64_t startFrame, uint64_t endFrame, VisualTempoPoint* outPoints, uint32_t maxCount) const override { return {}; }
    uint64_t getCurrentFrame() const override { return {}; }
    double getCurrentSeconds() const override { return {}; }
    bool isPlaying() const override { return {}; }
    bool isRecording() const override { return {}; }
    bool isRecordArmed() const override { return {}; }
    bool isLooping() const override { return {}; }
    double getBPM() const override { return {}; }
    void addMarker(uint64_t framePosition, const char* label, uint32_t colorARGB) override {}
    void removeMarker(const MarkerUUID& uuid) override {}
    void updateMarker(const MarkerUUID& uuid, uint64_t framePosition, const char* label, uint32_t colorARGB) override {}
    uint32_t getMarkersInRange(uint64_t startFrame, uint64_t endFrame, VisualMarker* outMarkers, uint32_t maxCount) const override { return {}; }
    double pixelsToFrames(float pixels, float zoomFactor) const override { return {}; }
    float framesToPixels(uint64_t frames, float zoomFactor) const override { return {}; }
    double getSampleRate() const override { return {}; }
    uint64_t getLoopStart() const override { return {}; }
    uint64_t getLoopEnd() const override { return {}; }
    void setPlaybackMode(PlaybackMode mode) override {}
    PlaybackMode getPlaybackMode() const override { return {}; }
    void getCurrentBBT(uint32_t& bar, uint32_t& beat, uint32_t& tick) const override {}
    uint64_t samplesToTicks(uint64_t samples) const override { return {}; }
    uint64_t ticksToSamples(uint64_t ticks) const override { return {}; }
    uint32_t getTicksPerBeat() const override { return {}; }
    void frameToBBT(uint64_t frame, uint32_t& bar, uint32_t& beat, uint32_t& tick) const override {}
    uint64_t bbtToFrame(uint32_t bar, uint32_t beat, uint32_t tick) const override { return {}; }
    void getTimeSignatureAtFrame(uint64_t frame, uint8_t& numerator, uint8_t& denominator) const override {}
    void setMetronomeEnabled(bool enabled) override {}
    bool isMetronomeEnabled() const override { return {}; }
    void setCountInEnabled(bool enabled) override {}
    bool isCountInEnabled() const override { return {}; }
    void setCountInBars(uint8_t bars) override {}
    uint8_t getCountInBars() const override { return {}; }
};

class StubArrangementController : public IArrangementController {
public:
    RegionID importAudioClip(TrackID targetTrack, const char* filePath, uint64_t startFrame) override { return {}; }
    RegionID insertMidiClip(TrackID targetTrack, uint64_t startFrame, uint64_t durationFrames) override { return {}; }
    RegionID insertAutomationClip( TrackID targetTrack, NodeID dspNode, uint32_t parameterIndex, uint64_t startFrame, uint64_t durationFrames ) override { return {}; }
    void deleteRegion(RegionID regionId) override {}
    void splitRegion(RegionID regionId, uint64_t splitFrame) override {}
    RegionID moveRegion(RegionID regionId, TrackID destTrack, int64_t newStartFrame, uint32_t destLayer) override { return {}; }
    void trimRegion(RegionID regionId, uint64_t newPosition, uint64_t newSourceStart, uint64_t newDuration) override {}
    bool getVisualRegion(RegionID regionId, VisualRegion& outRegion) const override { return {}; }
    void setRegionGain(RegionID id, float gainLinear) override {}
    void setRegionFades(RegionID id, uint32_t fadeInFrames, uint32_t fadeOutFrames) override {}
    void setRegionMuted(RegionID id, bool muted) override {}
    void setRegionWarpMode(RegionID id, WarpMode mode) override {}
    void setRegionPlaybackRatio(RegionID id, float ratio) override {}
    void setRegionSourceBpm(RegionID id, float bpm) override {}
    void updateRegionMetadata(RegionID regionId, const char* name, const char* comment, uint32_t colorARGB) override {}
    void initializeRegionMetadata(RegionID regionId, const char* defaultName, uint32_t colorARGB) override {}
    void mergePatternClips(TrackID trackId, uint64_t startFrame, uint64_t endFrame) override {}
    void swapTakeLayer(TrackID trackId, uint32_t laneIndex, uint64_t startFrame, uint64_t length) override {}
    void autoNameClips(TrackID trackId) override {}
    bool undo() override { return {}; }
    bool redo() override { return {}; }
    void consolidateTrack(TrackID trackId, uint64_t startFrame, uint64_t endFrame) override {}
    uint64_t getArrangementLength() const override { return {}; }
    uint32_t getRegionsInViewport( uint64_t viewStartFrame, uint64_t viewEndFrame, VisualRegion* outRegions, uint32_t maxCount ) const override { return {}; }
    uint32_t getActiveRecordings(VisualActiveRecording* outRecordings, uint32_t maxCount) override { return {}; }
};

class StubHardwareSettingsFacade : public IHardwareSettingsFacade {
public:
    std::vector<AudioDeviceDescriptor> getAvailableDevices() override { return {}; }
    HardwareConfig getCurrentConfig() override { return {}; }
    bool applyConfig(const HardwareConfig& config) override { return {}; }
    double getCpuLoad() override { return {}; }
    double getLatencyMs() override { return {}; }
    uint32_t getXrunCount() override { return {}; }
    std::vector<MidiPortDescriptor> getAvailableMidiPorts() override { return {}; }
    bool isMidiPortOpen(uint32_t portIndex) override { return {}; }
    bool setMidiPortOpen(uint32_t portIndex, bool open) override { return {}; }
};

} // namespace bridge::test_stubs

#pragma clang diagnostic pop
