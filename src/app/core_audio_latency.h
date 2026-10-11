#pragma once

#include <cstdint>
#include <string>

namespace app {

// Device latency + safety offset + stream latency + buffer size, in frames, of the CoreAudio
// device RtAudio reports as `rtAudioName` ("Manufacturer: Device name"). Returns 0 if unknown.
// Lives in its own translation unit: CoreAudio's AudioBuffer clashes with the DAW's own type.
uint64_t coreAudioLatencyFrames(const std::string& rtAudioName, bool inputScope);

} // namespace app
