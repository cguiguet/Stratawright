#pragma once

#include "claude_stream.h"

#include <QObject>
#include <QProcess>
#include <QString>

namespace presentation::chat {

// Drives Claude Code in headless mode (`claude -p` with stream-json input and output) as a child
// process, using the user's own Claude Code login. One process serves the whole conversation; the
// session id is remembered so a stopped process, or a relaunched app, resumes the conversation.
class ClaudeCodeSession : public QObject {
    Q_OBJECT

public:
    struct Options {
        QString workingDirectory;   // where Claude Code runs (the Stratawright repository)
        QString systemPrompt;       // appended to Claude Code's own system prompt
    };

    explicit ClaudeCodeSession(Options options, QObject* parent = nullptr);
    ~ClaudeCodeSession() override;

    bool isBusy() const { return busy_; }

    // Sends a user message; starts (or resumes) the Claude Code process when needed.
    void send(const QString& text);
    // Kills the running turn. The next message resumes the same conversation.
    void stop();
    // Stops and forgets the conversation: the next message starts a new one.
    void newConversation();

signals:
    void streamEvent(const presentation::chat::StreamEvent& event);
    void busyChanged(bool busy);
    void failed(const QString& message);

private:
    bool ensureProcess();
    void onStandardOutput();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void setBusy(bool busy);
    void rememberSession(const QString& sessionId);
    static QString findClaudeExecutable();

    Options options_;
    QProcess* process_ = nullptr;
    QByteArray pendingOutput_;
    QByteArray recentErrors_;
    QString sessionId_;
    double processCostUsd_ = 0.0;  // Claude Code reports the process's cumulative cost
    bool busy_ = false;
    bool stopping_ = false;
};

} // namespace presentation::chat
