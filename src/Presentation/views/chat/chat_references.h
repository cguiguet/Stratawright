#pragma once

#include "Middle Bridge/timeline/iarrangement_controller.h"

#include <QString>

namespace presentation::chat {

// How the chat refers to a clip, e.g. `@clip 12 · audio "Mic take" · track 4 "Mic" · bars 5.1.0 → 9.1.0`
// (bars as bar.beat.tick). The assistant is told to read these lines.
QString clipReference(const bridge::VisualRegion& clip, const QString& trackName,
                      const QString& startBars, const QString& endBars);

} // namespace presentation::chat
