#include "chat_references.h"

namespace presentation::chat {

QString clipReference(const bridge::VisualRegion& clip, const QString& trackName,
                      const QString& startBars, const QString& endBars) {
    const bool audio = clip.clipType == composition::RegionType::AUDIO;
    const bool midi = clip.clipType == composition::RegionType::MIDI;
    QString kind = audio ? QStringLiteral("audio") : midi ? QStringLiteral("MIDI") : QStringLiteral("automation");
    if (audio && clip.name[0] != '\0') {
        kind += QStringLiteral(" \"%1\"").arg(QString::fromUtf8(clip.name));
    }
    return QStringLiteral("@clip %1 · %2 · track %3 \"%4\" · bars %5 → %6")
        .arg(clip.id.id)
        .arg(kind)
        .arg(clip.trackId.id)
        .arg(trackName, startBars, endBars);
}

} // namespace presentation::chat
