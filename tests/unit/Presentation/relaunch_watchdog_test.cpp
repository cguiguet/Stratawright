// tests/unit/Presentation/relaunch_watchdog_test.cpp
//
// After a relaunch, the watchdog opens the last working build if the new one dies before it
// reports itself healthy. Run here with a fake `open` and fake apps (shell scripts).

#include <catch2/catch_test_macros.hpp>

#include "Presentation/views/relaunch.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

namespace {

void writeExecutable(const QString& path, const QString& script) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(script.toUtf8());
    file.close();
    file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
}

// Runs the watchdog for a new build whose executable behaves as appScript; returns what `open` was
// asked to do, one call per line.
QStringList runWatchdog(const QString& appScript) {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString app = dir.filePath(QStringLiteral("New.app"));
    const QString executable = app + QStringLiteral("/Contents/MacOS/stratawright");
    const QString fallback = dir.filePath(QStringLiteral("Last working.app"));
    const QString health = dir.filePath(QStringLiteral("healthy"));
    const QString openLog = dir.filePath(QStringLiteral("open.log"));
    QDir().mkpath(fallback);
    writeExecutable(executable, appScript.arg(health));
    // `open -n <bundle> [--args ...]`: logs the call and starts the new build in the background.
    writeExecutable(dir.filePath(QStringLiteral("bin/open")), QStringLiteral(
        "#!/bin/sh\n"
        "echo \"$*\" >> '%1'\n"
        "[ \"$2\" = '%2' ] && ('%3' &)\n"
        "exit 0\n").arg(openLog, app, executable));

    QProcess watchdog;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), dir.filePath(QStringLiteral("bin")) + QStringLiteral(":/usr/bin:/bin"));
    watchdog.setProcessEnvironment(environment);
    watchdog.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), presentation::relaunch::watchdogScript(),
                                               QStringLiteral("sh"), QStringLiteral("99999999"), app, executable,
                                               fallback, health});
    REQUIRE(watchdog.waitForFinished(30000));

    QFile log(openLog);
    REQUIRE(log.open(QIODevice::ReadOnly));
    QStringList calls = QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString& call : calls) {
        call.replace(dir.path() + QLatin1Char('/'), QString());
    }
    return calls;
}

} // namespace

TEST_CASE("The relaunch watchdog falls back to the last working build when the new one crashes", "[presentation][relaunch]") {
    // Dies a second after starting, without reporting itself healthy.
    const QStringList calls = runWatchdog(QStringLiteral("#!/bin/sh\n# %1\nsleep 1\nexit 1\n"));
    CHECK(calls == QStringList{"-n New.app", "-n Last working.app --args --recovered"});
}

TEST_CASE("The relaunch watchdog leaves a healthy new build alone", "[presentation][relaunch]") {
    // Reports itself healthy, then keeps running a little.
    const QStringList calls = runWatchdog(QStringLiteral("#!/bin/sh\nsleep 1\necho $$ > '%1'\nsleep 2\n"));
    CHECK(calls == QStringList{"-n New.app"});
}
