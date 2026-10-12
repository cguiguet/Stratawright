#pragma once

#include "iplugin.h"

#include <atomic>
#include <string>

// FluidSynth's opaque types (fluidsynth/types.h), so this header does not pull fluidsynth.h in.
typedef struct _fluid_hashtable_t fluid_settings_t;
typedef struct _fluid_synth_t fluid_synth_t;

namespace Layer3 {

// Built-in General MIDI instrument: FluidSynth playing the GeneralUser GS SoundFont, so MIDI
// tracks make sound without any third-party plugin. "GM Synth" plays the 128 melodic programs
// (parameter 0 picks the program); "GM Drums" plays the drum kits on the GM percussion map
// (36 kick, 38 snare, 42 closed hi-hat, 46 open hi-hat, 49 crash, 51 ride...).
class GMSynthPlugin : public IPlugin {
public:
    enum class Mode { Melodic, Drums };

    // PluginDescriptor::filePath values identifying the two built-in instruments.
    static constexpr const char* kSynthPath = "builtin:gm-synth";
    static constexpr const char* kDrumsPath = "builtin:gm-drums";

    explicit GMSynthPlugin(Mode mode);
    ~GMSynthPlugin() override;

    GMSynthPlugin(const GMSynthPlugin&) = delete;
    GMSynthPlugin& operator=(const GMSynthPlugin&) = delete;

    bool getInfo(PluginInfo& outInfo) const override;
    void processAudio(float* const* inputs, uint32_t numInputs,
                      float* const* outputs, uint32_t numOutputs,
                      uint32_t numSamples, const EventData* events,
                      uint32_t numEvents,
                      EventData* outEvents, uint32_t* outCount,
                      const ProcessContext* context,
                      const bool* inputSilence = nullptr) override;

    float getParameterValue(uint32_t paramIndex) const override;
    void setParameterValue(uint32_t paramIndex, float value) override;
    bool getParameterInfo(uint32_t paramIndex, ::ParameterInfo& outInfo) const override;
    void setParameterTweakedCallback(ParameterTweakedCallback cb) override { tweaked_ = std::move(cb); }

    std::vector<uint8_t> getState() const override;
    bool loadState(const uint8_t* buffer, uint64_t bufferSize) override;

    bool openEditor(void* /*parentWindow*/, int& /*outWidth*/, int& /*outHeight*/) override { return false; }
    void closeEditor() override {}

    // SoundFont used by both instruments: $STRATAWRIGHT_SOUNDFONT, then the app bundle's
    // Resources/soundfonts, then the source checkout's src/third_party/soundfonts.
    static std::string soundFontPath();

private:
    void render(float* const* outputs, uint32_t from, uint32_t to);
    void applyProgramIfChanged();
    int channel() const { return mode_ == Mode::Drums ? 9 : 0; }

    Mode mode_;
    fluid_settings_t* settings_ = nullptr;
    fluid_synth_t* synth_ = nullptr;
    int soundFontId_ = -1;
    std::atomic<int> program_{0};       // GM program, or drum kit preset in bank 128
    std::atomic<float> volume_{0.7f};   // 0..1, mapped to the synth gain
    int appliedProgram_ = -1;
    float appliedVolume_ = -1.0f;
    ParameterTweakedCallback tweaked_;
};

} // namespace Layer3
