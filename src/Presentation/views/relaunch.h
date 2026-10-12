#pragma once

#include <QString>

namespace bridge {
class IProjectLifecycleController;
class ITimelineController;
}

namespace presentation::relaunch {

// Saves the open project to a snapshot, with its real path, whether it had unsaved changes and
// the playhead, so the next launch can put everything back. Returns an error message, or an
// empty string on success (also when there is nothing to save).
QString saveSnapshot(bridge::IProjectLifecycleController* lifecycle, bridge::ITimelineController* timeline);

// Restores, then forgets, the snapshot left by saveSnapshot. Returns true if one was restored.
bool restoreSnapshot(bridge::IProjectLifecycleController* lifecycle, bridge::ITimelineController* timeline);

// Quits, then reopens the app bundle (which may hold a freshly built binary) once this process
// has exited.
void relaunchApplication();

} // namespace presentation::relaunch
