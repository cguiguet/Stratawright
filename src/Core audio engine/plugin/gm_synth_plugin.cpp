#include "gm_synth_plugin.h"

#include <fluidsynth.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace Layer3 {

namespace {

constexpr uint32_t kProgramParam = 0;
constexpr uint32_t kVolumeParam = 1;
constexpr int kDrumBank = 128;  // GM percussion bank in GS SoundFonts
constexpr const char* kSoundFontFile = "GeneralUser-GS.sf2";

std::filesystem::path executableDirectory() {
#ifdef __APPLE__
    char path[4096];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        std::error_code error;
        const auto resolved = std::filesystem::canonical(path, error);
        return error ? std::filesystem::path(path).parent_path() : resolved.parent_path();
    }
#endif
    return {};
}

} // namespace

std::string GMSynthPlugin::soundFontPath() {
    if (const char* overridePath = std::getenv("STRATAWRIGHT_SOUNDFONT")) {
        return overridePath;
    }
    std::vector<std::filesystem::path> candidates;
    const auto exeDir = executableDirectory();
    if (!exeDir.empty()) {
        candidates.push_back(exeDir / ".." / "Resources" / "soundfonts" / kSoundFontFile);  // app bundle
    }
#ifdef STRATAWRIGHT_SOURCE_DIR
    candidates.push_back(std::filesystem::path(STRATAWRIGHT_SOURCE_DIR) / "src" / "third_party" / "soundfonts" / kSoundFontFile);
#endif
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate.lexically_normal().string();
        }
    }
    return {};
}

GMSynthPlugin::GMSynthPlugin(Mode mode, double sampleRate) : mode_(mode), sampleRate_(sampleRate) {
    createSynth();
}

GMSynthPlugin::~GMSynthPlugin() {
    destroySynth();
}

void GMSynthPlugin::setSampleRate(double sampleRate) {
    if (sampleRate <= 0.0 || sampleRate == sampleRate_) {
        return;
    }
    destroySynth();
    sampleRate_ = sampleRate;
    createSynth();
}

void GMSynthPlugin::createSynth() {
    settings_ = new_fluid_settings();
    fluid_settings_setnum(settings_, "synth.sample-rate", sampleRate_);
    fluid_settings_setint(settings_, "synth.threadsafe-api", 0);  // only the audio thread drives it
    fluid_settings_setint(settings_, "synth.polyphony", 256);
    synth_ = new_fluid_synth(settings_);

    const std::string path = soundFontPath();
    if (synth_ && !path.empty()) {
        soundFontId_ = fluid_synth_sfload(synth_, path.c_str(), 1);
    }
    if (soundFontId_ < 0) {
        std::cerr << "GMSynthPlugin: no SoundFont loaded (looked for " << kSoundFontFile
                  << "; run scripts/setup_third_party.sh or set STRATAWRIGHT_SOUNDFONT)" << std::endl;
    }
    appliedProgram_ = -1;
    appliedVolume_ = -1.0f;
    applyProgramIfChanged();
}

void GMSynthPlugin::destroySynth() {
    if (synth_) {
        delete_fluid_synth(synth_);
        synth_ = nullptr;
    }
    if (settings_) {
        delete_fluid_settings(settings_);
        settings_ = nullptr;
    }
    soundFontId_ = -1;
}

bool GMSynthPlugin::getInfo(PluginInfo& outInfo) const {
    outInfo.numInputs = 0;
    outInfo.numOutputs = 2;
    outInfo.numParameters = 2;
    outInfo.latencySamples = 0;
    outInfo.hasEditor = false;
    outInfo.isInstrument = true;
    const char* name = mode_ == Mode::Drums ? "GM Drums" : "GM Synth";
    std::strncpy(outInfo.name, name, MAX_PLUGIN_NAME_LENGTH - 1);
    outInfo.name[MAX_PLUGIN_NAME_LENGTH - 1] = '\0';
    return true;
}

void GMSynthPlugin::applyProgramIfChanged() {
    if (!synth_ || soundFontId_ < 0) {
        return;
    }
    const int program = program_.load(std::memory_order_relaxed);
    if (program != appliedProgram_) {
        const int bank = mode_ == Mode::Drums ? kDrumBank : 0;
        fluid_synth_program_select(synth_, channel(), soundFontId_, bank, program);
        appliedProgram_ = program;
    }
    const float volume = volume_.load(std::memory_order_relaxed);
    if (volume != appliedVolume_) {
        fluid_synth_set_gain(synth_, volume);
        appliedVolume_ = volume;
    }
}

void GMSynthPlugin::render(float* const* outputs, uint32_t from, uint32_t to) {
    if (to <= from || !outputs[0] || !outputs[1]) {
        return;
    }
    const int length = static_cast<int>(to - from);
    if (synth_) {
        fluid_synth_write_float(synth_, length, outputs[0], static_cast<int>(from), 1, outputs[1], static_cast<int>(from), 1);
    } else {
        std::memset(outputs[0] + from, 0, static_cast<size_t>(length) * sizeof(float));
        std::memset(outputs[1] + from, 0, static_cast<size_t>(length) * sizeof(float));
    }
}

void GMSynthPlugin::processAudio(float* const* /*inputs*/, uint32_t /*numInputs*/,
                                 float* const* outputs, uint32_t numOutputs,
                                 uint32_t numSamples, const EventData* events,
                                 uint32_t numEvents,
                                 EventData* /*outEvents*/, uint32_t* outCount,
                                 const ProcessContext* /*context*/,
                                 const bool* /*inputSilence*/) {
    if (outCount) {
        *outCount = 0;
    }
    if (!outputs || numOutputs < 2 || numSamples == 0) {
        return;
    }
    applyProgramIfChanged();

    // Render up to each event, then apply it: notes start and stop on their exact sample.
    uint32_t rendered = 0;
    for (uint32_t i = 0; i < numEvents && events; ++i) {
        const EventData& event = events[i];
        const uint32_t offset = std::min(event.sampleOffset, numSamples);
        render(outputs, rendered, offset);
        rendered = std::max(rendered, offset);
        if (!synth_) {
            continue;
        }
        switch (event.eventType) {
        case EventType::MIDI_NOTE_ON:
            if (event.payload.midiNote.velocity == 0) {
                fluid_synth_noteoff(synth_, channel(), event.payload.midiNote.pitch);
            } else {
                fluid_synth_noteon(synth_, channel(), event.payload.midiNote.pitch, event.payload.midiNote.velocity);
            }
            break;
        case EventType::MIDI_NOTE_OFF:
            fluid_synth_noteoff(synth_, channel(), event.payload.midiNote.pitch);
            break;
        case EventType::MIDI_CC:
            fluid_synth_cc(synth_, channel(), event.payload.midiCC.controllerNumber, event.payload.midiCC.value);
            break;
        case EventType::MIDI_PITCH:
            fluid_synth_pitch_bend(synth_, channel(), event.payload.midiPitch.value);
            break;
        default:
            break;
        }
    }
    render(outputs, rendered, numSamples);
    for (uint32_t c = 2; c < numOutputs; ++c) {
        if (outputs[c]) {
            std::memset(outputs[c], 0, numSamples * sizeof(float));
        }
    }
}

float GMSynthPlugin::getParameterValue(uint32_t paramIndex) const {
    if (paramIndex == kProgramParam) {
        return static_cast<float>(program_.load()) / 127.0f;
    }
    if (paramIndex == kVolumeParam) {
        return volume_.load();
    }
    return 0.0f;
}

void GMSynthPlugin::setParameterValue(uint32_t paramIndex, float value) {
    const float normalized = std::clamp(value, 0.0f, 1.0f);
    if (paramIndex == kProgramParam) {
        program_.store(static_cast<int>(std::lround(normalized * 127.0f)));
    } else if (paramIndex == kVolumeParam) {
        volume_.store(normalized);
    } else {
        return;
    }
    if (tweaked_) {
        tweaked_(paramIndex, normalized);
    }
}

bool GMSynthPlugin::getParameterInfo(uint32_t paramIndex, ::ParameterInfo& outInfo) const {
    if (paramIndex > kVolumeParam) {
        return false;
    }
    outInfo = {};
    outInfo.index = paramIndex;
    const char* name = paramIndex == kProgramParam ? (mode_ == Mode::Drums ? "Kit" : "Program") : "Volume";
    std::strncpy(outInfo.name, name, sizeof(outInfo.name) - 1);
    outInfo.minValue = 0.0f;
    outInfo.maxValue = 1.0f;
    outInfo.defaultValue = paramIndex == kProgramParam ? 0.0f : 0.7f;
    outInfo.flags = ::ParameterInfo::IS_AUTOMATABLE;
    return true;
}

std::vector<uint8_t> GMSynthPlugin::getState() const {
    const int program = program_.load();
    const float volume = volume_.load();
    std::vector<uint8_t> state(1 + sizeof(float));
    state[0] = static_cast<uint8_t>(program);
    std::memcpy(state.data() + 1, &volume, sizeof(float));
    return state;
}

bool GMSynthPlugin::loadState(const uint8_t* buffer, uint64_t bufferSize) {
    if (!buffer || bufferSize < 1 + sizeof(float)) {
        return false;
    }
    float volume = 0.0f;
    std::memcpy(&volume, buffer + 1, sizeof(float));
    program_.store(std::min<int>(buffer[0], 127));
    volume_.store(std::clamp(volume, 0.0f, 1.0f));
    return true;
}

} // namespace Layer3
