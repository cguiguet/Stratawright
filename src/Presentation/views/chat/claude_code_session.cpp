#include "claude_code_session.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>

namespace presentation::chat {

namespace {


// Apps launched from Finder get a minimal PATH; Claude Code, daw-cli and the build tools live here.
QStringList toolDirectories() {
    const QString home = QDir::homePath();
    return {home + QStringLiteral("/.local/bin"), home + QStringLiteral("/.claude/local"),
            QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"),
            QStringLiteral("/usr/bin"), QStringLiteral("/bin"), QStringLiteral("/usr/sbin"), QStringLiteral("/sbin")};
}

} // namespace

ClaudeCodeSession::ClaudeCodeSession(Options options, QObject* parent)
    : QObject(parent), options_(std::move(options)) {}

ClaudeCodeSession::~ClaudeCodeSession() {
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(2000);
    }
}

QStringList ClaudeCodeSession::arguments(const Settings& settings, const QString& systemPrompt, const QString& sessionId) {
    QStringList arguments{
        QStringLiteral("-p"),
        QStringLiteral("--input-format"), QStringLiteral("stream-json"),
        QStringLiteral("--output-format"), QStringLiteral("stream-json"),
        QStringLiteral("--verbose"),
    };
    if (settings.readOnly) {
        // Plan mode, with only tools that read.
        arguments << QStringLiteral("--permission-mode") << QStringLiteral("plan")
                  << QStringLiteral("--allowedTools") << QStringLiteral("Read,Glob,Grep,TodoWrite,WebFetch,WebSearch");
    } else {
        // The panel has no approval UI: edits and shell commands run without prompts, like an
        // accepted session. The system prompt asks to confirm anything destructive in the chat.
        arguments << QStringLiteral("--permission-mode") << QStringLiteral("acceptEdits")
                  << QStringLiteral("--allowedTools") << QStringLiteral("Bash,Read,Edit,Write,Glob,Grep,TodoWrite,WebFetch,WebSearch");
    }
    if (!settings.model.isEmpty()) {
        arguments << QStringLiteral("--model") << settings.model;
    }
    if (!settings.effort.isEmpty()) {
        arguments << QStringLiteral("--effort") << settings.effort;
    }
    if (!systemPrompt.isEmpty()) {
        arguments << QStringLiteral("--append-system-prompt") << systemPrompt;
    }
    if (!sessionId.isEmpty()) {
        arguments << QStringLiteral("--resume") << sessionId;
    }
    return arguments;
}

QProcessEnvironment ClaudeCodeSession::toolEnvironment() {
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QStringList path = toolDirectories();
    path << environment.value(QStringLiteral("PATH")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    path.removeDuplicates();
    environment.insert(QStringLiteral("PATH"), path.join(QLatin1Char(':')));
    return environment;
}

void ClaudeCodeSession::setSettings(const Settings& settings) {
    if (settings == settings_) {
        return;
    }
    settings_ = settings;
    restartNeeded_ = true;
}

QString ClaudeCodeSession::findClaudeExecutable() {
    const QString found = QStandardPaths::findExecutable(QStringLiteral("claude"), toolDirectories());
    return found.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("claude")) : found;
}

bool ClaudeCodeSession::ensureProcess() {
    if (process_ && process_->state() != QProcess::NotRunning) {
        return true;
    }

    const QString claude = findClaudeExecutable();
    if (claude.isEmpty()) {
        emit failed(QStringLiteral("Claude Code was not found. Install it (https://claude.com/claude-code), "
                                   "sign in once with `claude` in a terminal, then try again."));
        return false;
    }

    delete process_;
    process_ = new QProcess(this);
    process_->setProgram(claude);
    process_->setArguments(arguments(settings_, options_.systemPrompt, sessionId_));
    process_->setProcessEnvironment(toolEnvironment());
    restartNeeded_ = false;
    if (QFileInfo(options_.workingDirectory).isDir()) {
        process_->setWorkingDirectory(options_.workingDirectory);
    }
    connect(process_, &QProcess::readyReadStandardOutput, this, &ClaudeCodeSession::onStandardOutput);
    connect(process_, &QProcess::readyReadStandardError, this, [this]() {
        recentErrors_ += process_->readAllStandardError();
        recentErrors_ = recentErrors_.right(4000);
    });
    connect(process_, &QProcess::finished, this, &ClaudeCodeSession::onFinished);

    pendingOutput_.clear();
    recentErrors_.clear();
    processCostUsd_ = 0.0;
    stopping_ = false;
    process_->start();
    if (!process_->waitForStarted(5000)) {
        emit failed(QStringLiteral("Could not start Claude Code (%1).").arg(process_->errorString()));
        return false;
    }
    return true;
}

void ClaudeCodeSession::send(const QString& text) {
    if (text.trimmed().isEmpty() || busy_) {
        return;
    }
    if (restartNeeded_ && process_ && process_->state() != QProcess::NotRunning) {
        // New settings: restart Claude Code, which resumes the same conversation.
        stopping_ = true;
        process_->kill();
        process_->waitForFinished(3000);
    }
    if (!ensureProcess()) {
        return;
    }
    setBusy(true);
    process_->write(userMessageLine(text));
}

void ClaudeCodeSession::stop() {
    if (process_ && process_->state() != QProcess::NotRunning) {
        stopping_ = true;
        process_->kill();
    }
    setBusy(false);
}

void ClaudeCodeSession::setConversation(const QString& sessionId) {
    stop();  // the running process belongs to the previous conversation
    sessionId_ = sessionId;
}

void ClaudeCodeSession::onStandardOutput() {
    pendingOutput_ += process_->readAllStandardOutput();
    qsizetype newline = pendingOutput_.indexOf('\n');
    while (newline >= 0) {
        const QByteArray line = pendingOutput_.left(newline);
        pendingOutput_.remove(0, newline + 1);
        for (StreamEvent event : parseStreamLine(line)) {
            if (event.kind == StreamEvent::Kind::SessionStarted || event.kind == StreamEvent::Kind::TurnFinished) {
                rememberSession(event.sessionId);
            }
            if (event.kind == StreamEvent::Kind::TurnFinished) {
                // Report what this turn cost, not the running total of the process.
                const double total = event.costUsd;
                event.costUsd = std::max(0.0, total - processCostUsd_);
                processCostUsd_ = total;
                setBusy(false);  // before notifying, so a handler can send the next message right away
            }
            emit streamEvent(event);
        }
        newline = pendingOutput_.indexOf('\n');
    }
}

void ClaudeCodeSession::onFinished(int exitCode, QProcess::ExitStatus status) {
    const bool unexpected = !stopping_ && (busy_ || status != QProcess::NormalExit || exitCode != 0);
    if (unexpected) {
        QString details = QString::fromUtf8(recentErrors_).trimmed();
        if (details.isEmpty()) {
            details = QStringLiteral("exit code %1").arg(exitCode);
        }
        emit failed(QStringLiteral("Claude Code stopped: %1").arg(details.right(600)));
    }
    stopping_ = false;
    setBusy(false);
}

void ClaudeCodeSession::setBusy(bool busy) {
    if (busy_ != busy) {
        busy_ = busy;
        emit busyChanged(busy);
    }
}

void ClaudeCodeSession::rememberSession(const QString& sessionId) {
    if (sessionId.isEmpty() || sessionId == sessionId_) {
        return;
    }
    sessionId_ = sessionId;
    emit sessionIdChanged(sessionId);
}

} // namespace presentation::chat
