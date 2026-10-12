#include "relaunch.h"

#include "Middle Bridge/project/iproject_lifecycle_controller.h"
#include "Middle Bridge/timeline/itimeline_controller.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#include <iostream>

namespace presentation::relaunch {

namespace {

const QString kSettingsName = QStringLiteral("Stratawright");
const QString kSnapshotKey = QStringLiteral("relaunch/snapshot");
const QString kProjectPathKey = QStringLiteral("relaunch/projectPath");
const QString kUnsavedKey = QStringLiteral("relaunch/unsavedChanges");
const QString kPlayheadKey = QStringLiteral("relaunch/playheadFrame");

QString snapshotPath() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Stratawright");
    QDir().mkpath(directory);
    return directory + QStringLiteral("/relaunch-snapshot.agdaw");
}

} // namespace

QString saveSnapshot(bridge::IProjectLifecycleController* lifecycle, bridge::ITimelineController* timeline) {
    QSettings settings(kSettingsName, kSettingsName);
    settings.remove(QStringLiteral("relaunch"));
    if (!lifecycle || !lifecycle->hasActiveProject()) {
        return {};
    }

    const QString projectPath = QString::fromStdString(lifecycle->getCurrentProjectPath());
    const bool unsavedChanges = lifecycle->isProjectDirty();
    const qulonglong playhead = timeline ? static_cast<qulonglong>(timeline->getCurrentFrame()) : 0;
    const QString snapshot = snapshotPath();
    if (!lifecycle->saveProject(snapshot.toUtf8().constData())) {
        return QStringLiteral("Could not save the project before relaunching.");
    }

    settings.setValue(kSnapshotKey, snapshot);
    settings.setValue(kProjectPathKey, projectPath);
    settings.setValue(kUnsavedKey, unsavedChanges);
    settings.setValue(kPlayheadKey, playhead);
    settings.sync();
    return {};
}

bool restoreSnapshot(bridge::IProjectLifecycleController* lifecycle, bridge::ITimelineController* timeline) {
    QSettings settings(kSettingsName, kSettingsName);
    const QString snapshot = settings.value(kSnapshotKey).toString();
    if (snapshot.isEmpty()) {
        return false;
    }
    const QString projectPath = settings.value(kProjectPathKey).toString();
    const bool unsavedChanges = settings.value(kUnsavedKey).toBool();
    const qulonglong playhead = settings.value(kPlayheadKey).toULongLong();
    // Forget it first: a snapshot that crashes the app on load must not do so on every launch.
    settings.remove(QStringLiteral("relaunch"));
    settings.sync();

    if (!lifecycle || !QFile::exists(snapshot)) {
        return false;
    }
    const bool restored = lifecycle->restoreProject(snapshot.toUtf8().constData(), projectPath.toUtf8().constData(), unsavedChanges);
    QFile::remove(snapshot);
    if (!restored) {
        std::cerr << "Relaunch: could not restore the project snapshot" << std::endl;
        return false;
    }
    if (timeline) {
        timeline->seekToFrame(playhead);
    }
    std::cout << "Relaunch: project restored" << (projectPath.isEmpty() ? "" : " (" + projectPath.toStdString() + ")") << std::endl;
    return true;
}

void relaunchApplication() {
    // A detached shell waits for this process to exit (killing it after 10 s if teardown hangs),
    // then reopens the bundle. `open -n`: right after the exit, LaunchServices may still see the
    // app as running and a plain `open` would do nothing.
    const QString bundle = QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../..")));
    const QString script = QStringLiteral(
        "pid=$1; app=$2; i=0; "
        "while kill -0 \"$pid\" 2>/dev/null && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done; "
        "kill -9 \"$pid\" 2>/dev/null; sleep 0.5; open -n \"$app\"");
    QProcess::startDetached(QStringLiteral("/bin/sh"),
                            {QStringLiteral("-c"), script, QStringLiteral("sh"),
                             QString::number(QCoreApplication::applicationPid()), bundle});
    QApplication::quit();
}

} // namespace presentation::relaunch
