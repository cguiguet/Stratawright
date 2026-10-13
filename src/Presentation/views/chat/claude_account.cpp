#include "claude_account.h"

#include "claude_code_session.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace presentation::chat {

namespace {

QString shellQuoted(const QString& text) {
    QString quoted = text;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

} // namespace

QString AccountStatus::description() const {
    if (!known) {
        return QStringLiteral("Claude Code account: unknown");
    }
    if (!loggedIn) {
        return QStringLiteral("Not signed in to Claude Code");
    }
    QString line = QStringLiteral("Signed in as %1").arg(email.isEmpty() ? QStringLiteral("(unknown)") : email);
    if (!plan.isEmpty()) {
        line += QStringLiteral(" · ") + plan.left(1).toUpper() + plan.mid(1);
    }
    return line;
}

AccountStatus parseAuthStatus(const QByteArray& json) {
    AccountStatus status;
    const QJsonDocument document = QJsonDocument::fromJson(json.trimmed());
    if (!document.isObject()) {
        return status;
    }
    const QJsonObject object = document.object();
    status.known = object.contains(QStringLiteral("loggedIn"));
    status.loggedIn = object.value(QStringLiteral("loggedIn")).toBool();
    status.email = object.value(QStringLiteral("email")).toString();
    status.plan = object.value(QStringLiteral("subscriptionType")).toString();
    if (status.plan.isEmpty()) {
        status.plan = object.value(QStringLiteral("authMethod")).toString();
    }
    return status;
}

bool runInTerminal(const QString& directory, const QStringList& command) {
    // A .command file opens in Terminal without asking to control it.
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                             .filePath(QStringLiteral("stratawright-%1.command").arg(QDateTime::currentMSecsSinceEpoch()));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QStringList quoted;
    for (const QString& part : command) {
        quoted << shellQuoted(part);
    }
    QString script = QStringLiteral("#!/bin/zsh -l\n");
    if (!directory.isEmpty()) {
        script += QStringLiteral("cd %1\n").arg(shellQuoted(directory));
    }
    script += QStringLiteral("rm -f %1\n").arg(shellQuoted(path));
    script += quoted.join(QLatin1Char(' ')) + QLatin1Char('\n');
    file.write(script.toUtf8());
    file.close();
    file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

ClaudeAccount::ClaudeAccount(QObject* parent) : QObject(parent) {}

void ClaudeAccount::refresh() {
    runClaude({QStringLiteral("auth"), QStringLiteral("status"), QStringLiteral("--json")}, false);
}

void ClaudeAccount::signOut() {
    runClaude({QStringLiteral("auth"), QStringLiteral("logout")}, true);
}

void ClaudeAccount::signInInTerminal() {
    const QString claude = ClaudeCodeSession::findClaudeExecutable();
    if (claude.isEmpty() || !runInTerminal(QDir::homePath(), {claude, QStringLiteral("auth"), QStringLiteral("login")})) {
        emit failed(QStringLiteral("Could not open Terminal to sign in. Run `claude auth login` in a terminal."));
    }
}

void ClaudeAccount::runClaude(const QStringList& arguments, bool refreshAfter) {
    const QString claude = ClaudeCodeSession::findClaudeExecutable();
    if (claude.isEmpty()) {
        status_ = AccountStatus{};
        emit statusChanged();
        return;
    }
    auto* process = new QProcess(this);
    process->setProcessEnvironment(ClaudeCodeSession::toolEnvironment());
    connect(process, &QProcess::finished, this, [this, process, refreshAfter](int exitCode, QProcess::ExitStatus) {
        process->deleteLater();
        if (refreshAfter) {
            if (exitCode != 0) {
                emit failed(QString::fromUtf8(process->readAllStandardError()).trimmed());
            }
            refresh();
            return;
        }
        status_ = parseAuthStatus(process->readAllStandardOutput());
        emit statusChanged();
    });
    process->start(claude, arguments);
}

} // namespace presentation::chat
