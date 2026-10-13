#pragma once

#include "common/system_primitives.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <vector>

class QTimer;

namespace bridge {
class IArrangementController;
class IHardwareSettingsFacade;
class ITimelineController;
class ITrackController;
}

namespace presentation::chat {

// Records a take from one input of the sound card for the chat's /mic command, on a "Mic" audio
// track, so it can be handed to the assistant (e.g. a beatboxed rhythm to turn into drums). The
// take starts on the playhead's bar after a one-bar count-in with the metronome (never before bar
// 2), and the count-in is trimmed off the clip.
class MicRecorder : public QObject {
    Q_OBJECT

public:
    struct Controllers {
        bridge::ITrackController* track = nullptr;
        bridge::ITimelineController* timeline = nullptr;
        bridge::IArrangementController* arrangement = nullptr;
        bridge::IHardwareSettingsFacade* hardware = nullptr;
    };

    struct Input {
        uint32_t channel = 0;  // 0-based channel of the input device
        QString label;         // e.g. "Steinberg UR12 · Input 1"
    };

    explicit MicRecorder(QObject* parent = nullptr);

    void setControllers(const Controllers& controllers);
    // Mono inputs of the open input device; empty when there is none.
    std::vector<Input> inputs() const;
    bool isRecording() const { return recording_; }

    // Starts a take; returns why it cannot, or an empty string.
    QString start(const Input& input);
    // Ends the take (also noticed when the transport is stopped some other way).
    void stop();

    // The bar the take starts on, from the playhead's bar: there must be a bar before it for the
    // count-in.
    static uint32_t takeBar(uint32_t playheadBar) { return playheadBar < 2 ? 2 : playheadBar; }
    // "Yamaha Steinberg: Steinberg UR12" -> "Steinberg UR12".
    static QString shortDeviceName(const QString& name);
    // Built-in speakers would play the click into the microphone.
    static bool isBuiltInSpeakers(const QString& outputName);

signals:
    void started(const QString& description);
    // reference: the chat line for the new clip, empty if nothing was recorded.
    void finished(const QString& reference, const QString& description);

private:
    void finish();
    // Disarms the Mic track and puts back what start() changed.
    void restoreState();
    uint32_t lastRegionOnMicTrack() const;
    QString outputDeviceName() const;
    QString barsOf(uint64_t frame) const;

    Controllers c_;
    QTimer* watch_ = nullptr;
    bool recording_ = false;
    TrackID mic_ = TrackID::invalid();
    Input input_;
    uint64_t takeStart_ = 0;
    uint32_t regionsBefore_ = 0;
    bool savedLoop_ = false;
    bool savedMetronome_ = false;
    bool savedRecordArm_ = false;
    bool savedMute_ = false;
};

} // namespace presentation::chat
