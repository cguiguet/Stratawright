#pragma once

#include <QString>

#include <functional>

class QObject;

namespace bridge {
class IProjectLifecycleController;
class ITimelineController;
}

namespace presentation::relaunch {

// Saves a copy of the open project to a snapshot, with its real path, whether it had unsaved
// changes and the playhead, so the next launch can put everything back. The project itself keeps
// its path. Returns an error message, or an empty string on success (also when there is nothing to
// save).
QString saveSnapshot(bridge::IProjectLifecycleController* lifecycle, bridge::ITimelineController* timeline);

// Restores the snapshot left by saveSnapshot. It is kept until this launch proves healthy
// (markHealthy), so that the last working build can restore it if this one crashes; a snapshot
// that already failed to come back twice is set aside instead. Returns true if one was restored.
bool restoreSnapshot(bridge::IProjectLifecycleController* lifecycle, bridge::ITimelineController* timeline);

// Drops the snapshot and its markers.
void forgetSnapshot();
QString snapshotFile();

// The app bundle a relaunch opens: this one, or the rebuilt one when this is the last working
// copy that took over after a crash.
QString targetBundle();

// Result of starting a build with --smoke-test.
struct SmokeTestResult {
    bool passed = false;
    QString output;       // what it printed (the end of it)
    QString crashReport;  // the macOS crash report it left, if it crashed
};

// Runs targetBundle()'s executable with --smoke-test, loading projectFile if given, and calls done
// with the result (on the GUI thread). The running app is not disturbed: the test opens no audio
// device, no IPC socket and no window on screen.
void runSmokeTest(const QString& projectFile, QObject* context, std::function<void(const SmokeTestResult&)> done);

// Quits, then reopens targetBundle() (which may hold a freshly built binary) once this process has
// exited. A watchdog follows the new launch: if it dies before calling markHealthy, the last
// working copy (lastWorkingBundle) is opened instead, with --recovered.
void relaunchApplication();

// The detached shell script behind relaunchApplication, run as
// `sh -c <script> sh <pid> <bundle> <executable> <fallback bundle> <health file>`: waits for pid to
// exit, opens bundle, then watches executable until the health file appears; if it dies first (or
// never starts), opens the fallback bundle with --recovered.
QString watchdogScript();

// Called once this launch has been up for a while: it starts fine, so its project snapshot can go
// and this build becomes the last working copy.
void markHealthy();

// Copy of the last build that started fine, opened when a rebuilt one crashes at startup:
// ~/Applications/Stratawright (last working).app.
QString lastWorkingBundle();
// True when this process is that copy (reopened by the watchdog, or by hand after a crash).
bool runningLastWorkingCopy();
// The newest crash report of the app, if any.
QString newestCrashReport();

} // namespace presentation::relaunch
