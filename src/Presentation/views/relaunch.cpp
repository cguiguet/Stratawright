#include "relaunch.h"

#include "Middle Bridge/project/iproject_lifecycle_controller.h"
#include "Middle Bridge/timeline/itimeline_controller.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <iostream>

namespace presentation::relaunch {

namespace {

const QString kSettingsName = QStringLiteral("Stratawright");
const QString kSnapshotKey = QStringLiteral("relaunch/snapshot");
const QString kProjectPathKey = QStringLiteral("relaunch/projectPath");
const QString kUnsavedKey = QStringLiteral("relaunch/unsavedChanges");
const QString kPlayheadKey = QStringLiteral("relaunch/playheadFrame");
const QString kAttemptsKey = QStringLiteral("relaunch/restoreAttempts");
const QString kMainBundleKey = QStringLiteral("health/mainBundle");
const QString kLastWorkingStampKey = QStringLiteral("health/lastWorkingStamp");
constexpr int kMaxRestoreAttempts = 2;

QString dataDirectory() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Stratawright");
    QDir().mkpath(directory);
    return directory;
}

// Written by markHealthy, watched by the relaunch watchdog.
QString healthFile() {
    return dataDirectory() + QStringLiteral("/launch-healthy");
}

QString currentBundle() {
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../..")));
}

QString executableOf(const QString& bundle) {
    return bundle + QStringLiteral("/Contents/MacOS/") + QFileInfo(QCoreApplication::applicationFilePath()).fileName();
}

// Identifies a build: its executable's size and modification time.
QString buildStamp(const QString& bundle) {
    const QFileInfo executable(executableOf(bundle));
    return QStringLiteral("%1-%2").arg(executable.size()).arg(executable.lastModified().toMSecsSinceEpoch());
}

} // namespace

QString snapshotFile() {
    return dataDirectory() + QStringLiteral("/relaunch-snapshot.agdaw");
}

void forgetSnapshot() {
    QSettings settings(kSettingsName, kSettingsName);
    const QString snapshot = settings.value(kSnapshotKey).toString();
    settings.remove(QStringLiteral("relaunch"));
    settings.sync();
    if (!snapshot.isEmpty()) {
        QFile::remove(snapshot);
    }
}

QString lastWorkingBundle() {
    return QDir::homePath() + QStringLiteral("/Applications/Stratawright (last working).app");
}

bool runningLastWorkingCopy() {
    return QDir::cleanPath(currentBundle()) == QDir::cleanPath(lastWorkingBundle());
}

QString targetBundle() {
    if (runningLastWorkingCopy()) {
        const QString main = QSettings(kSettingsName, kSettingsName).value(kMainBundleKey).toString();
        if (QFileInfo(main).isDir()) {
            return main;
        }
    }
    return currentBundle();
}

QString newestCrashReport() {
    const QDir reports(QDir::homePath() + QStringLiteral("/Library/Logs/DiagnosticReports"));
    const QString executable = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    const QFileInfoList files = reports.entryInfoList({executable + QStringLiteral("-*.ips")}, QDir::Files, QDir::Time);
    return files.isEmpty() ? QString() : files.first().absoluteFilePath();
}

QString saveSnapshot(bridge::IProjectLifecycleController* lifecycle, bridge::ITimelineController* timeline) {
    QSettings settings(kSettingsName, kSettingsName);
    settings.remove(QStringLiteral("relaunch"));
    if (!lifecycle || !lifecycle->hasActiveProject()) {
        return {};
    }

    const QString projectPath = QString::fromStdString(lifecycle->getCurrentProjectPath());
    const bool unsavedChanges = lifecycle->isProjectDirty();
    const qulonglong playhead = timeline ? static_cast<qulonglong>(timeline->getCurrentFrame()) : 0;
    const QString snapshot = snapshotFile();
    if (!lifecycle->saveProjectCopy(snapshot.toUtf8().constData())) {
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
    const int attempts = settings.value(kAttemptsKey, 0).toInt();
    if (attempts >= kMaxRestoreAttempts || !lifecycle || !QFile::exists(snapshot)) {
        // The snapshot itself may be what crashes: keep it aside rather than load it again.
        settings.remove(QStringLiteral("relaunch"));
        settings.sync();
        if (QFile::exists(snapshot)) {
            const QString kept = dataDirectory() + QStringLiteral("/unrestored-%1.agdaw")
                                     .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
            QFile::rename(snapshot, kept);
            std::cerr << "Relaunch: the project snapshot did not come back twice; kept as " << kept.toStdString() << std::endl;
        }
        return false;
    }
    settings.setValue(kAttemptsKey, attempts + 1);
    settings.sync();

    const QString projectPath = settings.value(kProjectPathKey).toString();
    const bool unsavedChanges = settings.value(kUnsavedKey).toBool();
    const qulonglong playhead = settings.value(kPlayheadKey).toULongLong();
    if (!lifecycle->restoreProject(snapshot.toUtf8().constData(), projectPath.toUtf8().constData(), unsavedChanges)) {
        std::cerr << "Relaunch: could not restore the project snapshot" << std::endl;
        return false;
    }
    if (timeline) {
        timeline->seekToFrame(playhead);
    }
    std::cout << "Relaunch: project restored" << (projectPath.isEmpty() ? "" : " (" + projectPath.toStdString() + ")") << std::endl;
    return true;
}

void runSmokeTest(const QString& projectFile, QObject* context, std::function<void(const SmokeTestResult&)> done) {
    auto* process = new QProcess(context);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    process->setProcessEnvironment(environment);
    process->setProcessChannelMode(QProcess::MergedChannels);
    QStringList arguments{QStringLiteral("--smoke-test")};
    if (!projectFile.isEmpty()) {
        arguments << QStringLiteral("--project") << projectFile;
    }
    const QDateTime started = QDateTime::currentDateTime();
    auto finish = std::make_shared<std::function<void(bool)>>();
    *finish = [process, started, done](bool passed) {
        SmokeTestResult result;
        result.passed = passed;
        result.output = QString::fromUtf8(process->readAll()).right(3000);
        const QString report = newestCrashReport();
        if (!passed && !report.isEmpty() && QFileInfo(report).lastModified() >= started.addSecs(-1)) {
            result.crashReport = report;
        }
        process->deleteLater();
        done(result);
    };
    QObject::connect(process, &QProcess::finished, context, [process, finish](int exitCode, QProcess::ExitStatus status) {
        const bool passed = status == QProcess::NormalExit && exitCode == 0;
        if (passed) {
            (*finish)(true);
        } else {
            // macOS writes the crash report a moment after the process died.
            QTimer::singleShot(3000, process, [finish]() { (*finish)(false); });
        }
    });
    QTimer::singleShot(120000, process, [process]() {
        if (process->state() != QProcess::NotRunning) {
            process->kill();  // reported as a failure by finished
        }
    });
    process->start(executableOf(targetBundle()), arguments);
}

void relaunchApplication() {
    // A detached shell waits for this process to exit (killing it after 10 s if teardown hangs),
    // then reopens the bundle. `open -n`: right after the exit, LaunchServices may still see the
    // app as running and a plain `open` would do nothing. It then watches the new launch until it
    // reports itself healthy; if it dies first, the last working copy is opened instead.
    const QString bundle = targetBundle();
    QFile::remove(healthFile());
    QProcess::startDetached(QStringLiteral("/bin/sh"),
                            {QStringLiteral("-c"), watchdogScript(), QStringLiteral("sh"),
                             QString::number(QCoreApplication::applicationPid()), bundle, executableOf(bundle),
                             lastWorkingBundle(), healthFile()});
    QApplication::quit();
}

QString watchdogScript() {
    return QStringLiteral(
        "pid=$1; app=$2; exe=$3; fallback=$4; health=$5; i=0; "
        "while kill -0 \"$pid\" 2>/dev/null && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done; "
        "kill -9 \"$pid\" 2>/dev/null; sleep 0.5; open -n \"$app\"; "
        "seen=; i=0; "
        "while [ $i -lt 600 ]; do "
        "  [ -f \"$health\" ] && exit 0; "
        "  if pgrep -f \"$exe\" >/dev/null; then seen=1; "
        "  elif [ -n \"$seen\" ] || [ $i -gt 150 ]; then break; fi; "
        "  sleep 0.2; i=$((i+1)); "
        "done; "
        "[ -f \"$health\" ] && exit 0; "
        "pgrep -f \"$exe\" >/dev/null && exit 0; "
        "[ -d \"$fallback\" ] && [ \"$fallback\" != \"$app\" ] && open -n \"$fallback\" --args --recovered");
}

void markHealthy() {
    QFile health(healthFile());
    if (health.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        health.write(QByteArray::number(QCoreApplication::applicationPid()));
    }
    forgetSnapshot();  // the project came back and the app keeps running
    if (runningLastWorkingCopy()) {
        return;
    }

    // This build works: remember it, and keep a copy of it to fall back on (an APFS clone, which
    // takes no time nor space until the build changes).
    QSettings settings(kSettingsName, kSettingsName);
    const QString bundle = currentBundle();
    settings.setValue(kMainBundleKey, bundle);
    const QString stamp = buildStamp(bundle);
    if (settings.value(kLastWorkingStampKey).toString() == stamp && QFileInfo(lastWorkingBundle()).isDir()) {
        return;
    }
    settings.setValue(kLastWorkingStampKey, stamp);
    const QString target = lastWorkingBundle();
    const QString script = QStringLiteral(
        "mkdir -p \"$(dirname \"$2\")\" && rm -rf \"$2.new\" && "
        "(cp -cR \"$1\" \"$2.new\" 2>/dev/null || cp -R \"$1\" \"$2.new\") && "
        "rm -rf \"$2\" && mv \"$2.new\" \"$2\"");
    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script, QStringLiteral("sh"), bundle, target});
}

} // namespace presentation::relaunch
