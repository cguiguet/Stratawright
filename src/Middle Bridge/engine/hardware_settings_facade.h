// src/Middle Bridge/hardware_settings_facade.h
#pragma once

#include "engine/ihardware_settings_facade.h"
#include "Hardware/OS abstraction/audio/iaudio_driver.h"
#include "Hardware/OS abstraction/midi/imidi_driver.h"
#include "Core audio engine/engine/iaudio_engine.h"
#include <functional>
#include <mutex>
#include <unordered_set>

namespace bridge {

class ISessionManager;

class HardwareSettingsFacade : public IHardwareSettingsFacade {
public:
    HardwareSettingsFacade(
        Layer1::IAudioDriver* audioDriver,
        Layer1::IMIDIDriver* midiDriver,
        Layer3::IAudioEngine* audioEngine,
        ISessionManager* sessionManager = nullptr
    );
    ~HardwareSettingsFacade() override = default;

    std::vector<AudioDeviceDescriptor> getAvailableDevices() override;
    HardwareConfig getCurrentConfig() override;
    bool applyConfig(const HardwareConfig& config) override;
    double getCpuLoad() override;
    double getLatencyMs() override;
    uint32_t getXrunCount() override;

    // Called when applyConfig changes the engine's sample rate, with the stream open but not yet
    // started, after the active project has been converted: for everything else that counts in
    // samples (tempo map, playhead, clocks, new plugins).
    using SampleRateChangedHandler = std::function<void(uint32_t oldRate, uint32_t newRate)>;
    void setSampleRateChangedHandler(SampleRateChangedHandler handler) { sampleRateChanged_ = std::move(handler); }

    // Called after every stream applyConfig opens, once the engine is prepared and before it
    // starts: the devices or the buffer size may have changed, and with them the latency.
    using StreamOpenedHandler = std::function<void()>;
    void setStreamOpenedHandler(StreamOpenedHandler handler) { streamOpened_ = std::move(handler); }

    std::vector<MidiPortDescriptor> getAvailableMidiPorts() override;
    bool isMidiPortOpen(uint32_t portIndex) override;
    bool setMidiPortOpen(uint32_t portIndex, bool open) override;

private:
    Layer1::IAudioDriver* audioDriver_ = nullptr;
    Layer1::IMIDIDriver* midiDriver_ = nullptr;
    Layer3::IAudioEngine* audioEngine_ = nullptr;
    ISessionManager* sessionManager_ = nullptr;
    std::mutex mutex_;
    std::unordered_set<uint32_t> openMidiPorts_;
    SampleRateChangedHandler sampleRateChanged_;
    StreamOpenedHandler streamOpened_;
};

} // namespace bridge
