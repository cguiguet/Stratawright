#include <catch2/catch_test_macros.hpp>

#include "Core audio engine/plugin/gm_synth_plugin.h"

#include <cmath>
#include <string>
#include <vector>

using Layer3::GMSynthPlugin;

namespace {

// RMS of the left channel over ~0.23 s, optionally starting a note on the first sample.
double renderRms(GMSynthPlugin& synth, bool playNote, uint8_t pitch) {
    std::vector<float> left(512), right(512);
    float* outputs[2] = {left.data(), right.data()};
    EventData noteOn{};
    noteOn.sampleOffset = 0;
    noteOn.eventType = EventType::MIDI_NOTE_ON;
    noteOn.payload.midiNote.pitch = pitch;
    noteOn.payload.midiNote.velocity = 110;

    double sum = 0.0;
    size_t count = 0;
    for (int block = 0; block < 20; ++block) {
        const bool first = block == 0 && playNote;
        synth.processAudio(nullptr, 0, outputs, 2, 512, first ? &noteOn : nullptr, first ? 1 : 0, nullptr, nullptr, nullptr);
        for (float value : left) {
            sum += static_cast<double>(value) * static_cast<double>(value);
            ++count;
        }
    }
    return std::sqrt(sum / static_cast<double>(count));
}

} // namespace

TEST_CASE("GM Synth: melodic and drum instruments make sound", "[Layer3][GMSynth]") {
    if (GMSynthPlugin::soundFontPath().empty()) {
        SKIP("GeneralUser GS SoundFont missing: run scripts/setup_third_party.sh");
    }

    GMSynthPlugin piano(GMSynthPlugin::Mode::Melodic);
    CHECK(renderRms(piano, false, 60) < 1e-6);   // silent until a note arrives
    CHECK(renderRms(piano, true, 60) > 0.005);   // middle C on the default program (piano)

    GMSynthPlugin drums(GMSynthPlugin::Mode::Drums);
    CHECK(renderRms(drums, true, 36) > 0.005);   // GM kick

    Layer3::IPlugin::PluginInfo info{};
    REQUIRE(drums.getInfo(info));
    CHECK(info.isInstrument);
    CHECK(std::string(info.name) == "GM Drums");
}

TEST_CASE("GM Synth: parameters survive a state round trip", "[Layer3][GMSynth]") {
    GMSynthPlugin synth(GMSynthPlugin::Mode::Melodic);
    synth.setParameterValue(0, 33.0f / 127.0f);  // GM program 33: Fingered Bass
    synth.setParameterValue(1, 0.5f);

    GMSynthPlugin restored(GMSynthPlugin::Mode::Melodic);
    const std::vector<uint8_t> state = synth.getState();
    REQUIRE(restored.loadState(state.data(), state.size()));
    CHECK(std::lround(restored.getParameterValue(0) * 127.0f) == 33);
    CHECK(restored.getParameterValue(1) == 0.5f);
}
