#include "core_audio_latency.h"

#include <CoreAudio/CoreAudio.h>

#include <vector>

namespace app {

namespace {

uint64_t readU32(AudioObjectID object, AudioObjectPropertySelector selector, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
    UInt32 value = 0;
    UInt32 valueSize = static_cast<UInt32>(sizeof(value));
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, &valueSize, &value) == noErr ? value : 0;
}

std::string deviceName(AudioObjectID device) {
    AudioObjectPropertyAddress address{kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef cfName = nullptr;
    UInt32 nameSize = static_cast<UInt32>(sizeof(cfName));
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &nameSize, &cfName) != noErr || cfName == nullptr) {
        return {};
    }
    char buffer[256] = {};
    CFStringGetCString(cfName, buffer, static_cast<CFIndex>(sizeof(buffer)), kCFStringEncodingUTF8);
    CFRelease(cfName);
    return buffer;
}

bool sameDevice(const std::string& rtAudioName, const std::string& coreAudioName) {
    if (coreAudioName.empty()) return false;
    if (rtAudioName == coreAudioName) return true;
    const std::string suffix = ": " + coreAudioName;
    return rtAudioName.size() > suffix.size()
        && rtAudioName.compare(rtAudioName.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

uint64_t coreAudioLatencyFrames(const std::string& rtAudioName, bool inputScope) {
    const AudioObjectPropertyScope scope = inputScope ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;

    AudioObjectPropertyAddress devicesAddress{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devicesAddress, 0, nullptr, &size) != noErr) return 0;
    std::vector<AudioObjectID> devices(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &devicesAddress, 0, nullptr, &size, devices.data()) != noErr) return 0;

    for (AudioObjectID device : devices) {
        if (!sameDevice(rtAudioName, deviceName(device))) continue;

        uint64_t frames = readU32(device, kAudioDevicePropertyLatency, scope)
                        + readU32(device, kAudioDevicePropertySafetyOffset, scope)
                        + readU32(device, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal);

        AudioObjectPropertyAddress streamsAddress{kAudioDevicePropertyStreams, scope, kAudioObjectPropertyElementMain};
        UInt32 streamsSize = 0;
        if (AudioObjectGetPropertyDataSize(device, &streamsAddress, 0, nullptr, &streamsSize) == noErr
            && streamsSize >= sizeof(AudioStreamID)) {
            std::vector<AudioStreamID> streams(streamsSize / sizeof(AudioStreamID));
            if (AudioObjectGetPropertyData(device, &streamsAddress, 0, nullptr, &streamsSize, streams.data()) == noErr) {
                frames += readU32(streams[0], kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal);
            }
        }
        return frames;
    }
    return 0;
}

} // namespace app
