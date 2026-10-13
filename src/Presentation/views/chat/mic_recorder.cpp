#include "mic_recorder.h"

#include "chat_references.h"

#include "Middle Bridge/engine/ihardware_settings_facade.h"
#include "Middle Bridge/timeline/iarrangement_controller.h"
#include "Middle Bridge/timeline/itimeline_controller.h"
#include "Middle Bridge/tracks/itrack_controller.h"

#include <QTimer>

#include <algorithm>
#include <cstring>
#include <limits>

namespace presentation::chat {

namespace {

constexpr const char* kMicTrackName = "Mic";
constexpr uint32_t kMicTrackColor = 0xFF2ECC71;
constexpr const char* kTakeName = "Mic take";

} // namespace

MicRecorder::MicRecorder(QObject* parent) : QObject(parent) {
    watch_ = new QTimer(this);
    watch_->setInterval(100);
    connect(watch_, &QTimer::timeout, this, [this]() {
        if (recording_ && c_.timeline && !c_.timeline->isRecording()) {
            finish();  // stopped from the transport, the space bar...
        }
    });
}

void MicRecorder::setControllers(const Controllers& controllers) {
    c_ = controllers;
}

QString MicRecorder::shortDeviceName(const QString& name) {
    const qsizetype colon = name.indexOf(QStringLiteral(": "));
    return colon >= 0 ? name.mid(colon + 2) : name;
}

bool MicRecorder::isBuiltInSpeakers(const QString& outputName) {
    return outputName.contains(QStringLiteral("Speakers"), Qt::CaseInsensitive);
}

std::vector<MicRecorder::Input> MicRecorder::inputs() const {
    std::vector<Input> inputs;
    if (!c_.track || !c_.hardware) {
        return inputs;
    }
    const bridge::HardwareConfig config = c_.hardware->getCurrentConfig();
    if (config.inputDeviceIndex == 0xFFFFFFFF || config.numInputChannels == 0) {
        return inputs;
    }
    QString device;
    for (const auto& descriptor : c_.hardware->getAvailableDevices()) {
        if (descriptor.deviceIndex == config.inputDeviceIndex) {
            device = shortDeviceName(QString::fromUtf8(descriptor.name));
        }
    }
    for (const auto& option : c_.track->getAvailableAudioInputs()) {
        if (option.numChannels == 1) {
            const QString name = QString::fromStdString(option.name);
            inputs.push_back({option.optionId, device.isEmpty() ? name : device + QStringLiteral(" · ") + name});
        }
    }
    return inputs;
}

QString MicRecorder::outputDeviceName() const {
    if (!c_.hardware) {
        return {};
    }
    const bridge::HardwareConfig config = c_.hardware->getCurrentConfig();
    for (const auto& descriptor : c_.hardware->getAvailableDevices()) {
        if (descriptor.deviceIndex == config.outputDeviceIndex) {
            return QString::fromUtf8(descriptor.name);
        }
    }
    return {};
}

QString MicRecorder::barsOf(uint64_t frame) const {
    uint32_t bar = 1, beat = 1, tick = 0;
    c_.timeline->frameToBBT(frame, bar, beat, tick);
    return QStringLiteral("%1.%2.%3").arg(bar).arg(beat).arg(tick);
}

uint32_t MicRecorder::lastRegionOnMicTrack() const {
    std::vector<bridge::VisualRegion> regions(4096);
    const uint32_t count = c_.arrangement->getRegionsInViewport(0, std::numeric_limits<uint64_t>::max(), regions.data(),
                                                                static_cast<uint32_t>(regions.size()));
    uint32_t last = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (regions[i].trackId.id == mic_.id) {
            last = std::max(last, regions[i].id.id);
        }
    }
    return last;
}

QString MicRecorder::start(const Input& input) {
    if (!c_.track || !c_.timeline || !c_.arrangement || !c_.hardware) {
        return QStringLiteral("Recording is not available yet.");
    }
    if (recording_) {
        return QStringLiteral("Already recording: type /mic to stop.");
    }
    if (c_.timeline->isPlaying() || c_.timeline->isRecording()) {
        return QStringLiteral("Stop playback first, then type /mic again.");
    }

    // Every armed track records along; a reused Mic track is found by name.
    mic_ = TrackID::invalid();
    for (const auto& track : c_.track->getAllTracks()) {
        const bool isMic = track.type == composition::TrackType::AUDIO && std::strcmp(track.name, kMicTrackName) == 0;
        if (isMic && !mic_.isValid()) {
            mic_ = track.trackId;
        } else if (track.isRecordArmed) {
            return QStringLiteral("Track \"%1\" is armed and would record too: disarm it first.").arg(QString::fromUtf8(track.name));
        }
    }
    if (!mic_.isValid()) {
        mic_ = c_.track->addAudioTrack(kMicTrackName, 2, kMicTrackColor);
        if (!mic_.isValid()) {
            return QStringLiteral("Could not create the Mic track.");
        }
    }
    input_ = input;

    // Mono from the chosen channel, set before arming (arming opens the file with the input's
    // channel count). No monitoring: the input must not come out of the speakers. Muted so that
    // earlier takes on the track stay silent; the recording is taken before the fader.
    c_.track->setTrackInput(mic_, input.channel, 1);
    c_.track->setInputMonitoring(mic_, false);
    savedMute_ = c_.track->getTrackState(mic_).isMuted;
    c_.track->setMute(mic_, true);

    savedLoop_ = c_.timeline->isLooping();
    savedMetronome_ = c_.timeline->isMetronomeEnabled();
    savedRecordArm_ = c_.timeline->isRecordArmed();
    const QString output = outputDeviceName();
    const bool click = !isBuiltInSpeakers(output);
    c_.timeline->setLoopEnabled(false);  // a wrapped take comes out empty
    c_.timeline->setMetronomeEnabled(click);

    uint32_t bar = 1, beat = 1, tick = 0;
    c_.timeline->frameToBBT(c_.timeline->getCurrentFrame(), bar, beat, tick);
    const uint32_t take = takeBar(bar);
    takeStart_ = c_.timeline->bbtToFrame(take, 1, 0);
    regionsBefore_ = lastRegionOnMicTrack();

    c_.timeline->seekToFrame(c_.timeline->bbtToFrame(take - 1, 1, 0));
    c_.track->setRecordArmed(mic_, true);
    c_.timeline->setRecordArmed(true);
    c_.timeline->play();
    if (!c_.timeline->isRecording()) {
        if (c_.timeline->isPlaying()) {
            c_.timeline->stop();
        }
        restoreState();
        c_.timeline->seekToFrame(takeStart_);
        return QStringLiteral("Recording did not start: is the audio interface connected?");
    }
    recording_ = true;
    watch_->start();

    QString description = QStringLiteral("Recording %1: count-in on bar %2, the take starts on bar %3.")
                              .arg(input.label).arg(take - 1).arg(take);
    if (!click) {
        description += QStringLiteral(" No click: the sound comes out of %1, the mic would hear it.")
                           .arg(shortDeviceName(output));
    }
    emit started(description + QStringLiteral(" Type /mic or press Stop to finish."));
    return {};
}

void MicRecorder::stop() {
    if (recording_) {
        finish();
    }
}

void MicRecorder::restoreState() {
    // Disarmed after every take: an armed track keeps writing into a buffer freed at stop.
    c_.track->setRecordArmed(mic_, false);
    c_.timeline->setRecordArmed(savedRecordArm_);
    c_.timeline->setMetronomeEnabled(savedMetronome_);
    c_.timeline->setLoopEnabled(savedLoop_);
    c_.track->setMute(mic_, savedMute_);
}

void MicRecorder::finish() {
    if (!recording_) {
        return;
    }
    recording_ = false;
    watch_->stop();
    if (c_.timeline->isRecording() || c_.timeline->isPlaying()) {
        c_.timeline->stop();  // commits the take
    }
    restoreState();

    // The take is the newest clip on the Mic track.
    std::vector<bridge::VisualRegion> regions(4096);
    const uint32_t count = c_.arrangement->getRegionsInViewport(0, std::numeric_limits<uint64_t>::max(), regions.data(),
                                                                static_cast<uint32_t>(regions.size()));
    const bridge::VisualRegion* newest = nullptr;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& region = regions[i];
        if (region.trackId.id == mic_.id && region.id.id > regionsBefore_ && (!newest || region.id.id > newest->id.id)) {
            newest = &region;
        }
    }
    c_.timeline->seekToFrame(takeStart_);
    if (!newest) {
        emit finished({}, QStringLiteral("Nothing was recorded."));
        return;
    }

    bridge::VisualRegion clip = *newest;
    const uint64_t end = clip.startFrame + clip.durationFrames;
    if (end <= takeStart_) {
        c_.arrangement->deleteRegion(clip.id);
        emit finished({}, QStringLiteral("Stopped during the count-in: nothing was recorded."));
        return;
    }
    if (clip.startFrame < takeStart_) {
        // Trim the count-in off.
        const uint64_t countIn = takeStart_ - clip.startFrame;
        c_.arrangement->trimRegion(clip.id, takeStart_, clip.fileOffsetFrames + countIn, clip.durationFrames - countIn);
    }
    c_.arrangement->updateRegionMetadata(clip.id, kTakeName, "", clip.colorARGB);
    c_.arrangement->getVisualRegion(clip.id, clip);

    const QString reference = clipReference(clip, QString::fromLatin1(kMicTrackName), barsOf(clip.startFrame),
                                            barsOf(clip.startFrame + clip.durationFrames));
    emit finished(reference, QStringLiteral("Recorded from %1.").arg(input_.label));
}

} // namespace presentation::chat
