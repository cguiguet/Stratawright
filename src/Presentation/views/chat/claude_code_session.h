#pragma once

#include "claude_stream.h"

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace presentation::chat {

// Drives Claude Code in headless mode (`claude -p` with stream-json input and output) as a child
// process, using the user's own Claude Code login. One process serves the whole conversation; its
// session id (kept by the caller, see sessionIdChanged) lets a stopped process resume it.
class ClaudeCodeSession : public QObject {
    Q_OBJECT

public:
    struct Options {
        QString workingDirectory;   // where Claude Code runs (the Stratawright repository)
        QString systemPrompt;       // appended to Claude Code's own system prompt
    };

    // How the agent runs, chosen in the panel's menu. Empty strings keep Claude Code's defaults.
    struct Settings {
        QString model;          // alias ("opus", "sonnet"...) or full model name
        QString effort;         // low, medium, high, xhigh, max
        bool readOnly = false;  // plan mode: answers and plans, changes nothing

        bool operator==(const Settings& other) const = default;
    };

    explicit ClaudeCodeSession(Options options, QObject* parent = nullptr);
    ~ClaudeCodeSession() override;

    // Takes effect from the next message: Claude Code is restarted on the same conversation.
    void setSettings(const Settings& settings);
    const Settings& settings() const { return settings_; }

    // Command line of the Claude Code process for these settings.
    static QStringList arguments(const Settings& settings, const QString& systemPrompt, const QString& sessionId);
    // Where Claude Code is installed (empty if not found), and an environment where it and the
    // tools it uses are found even when the app was launched from Finder.
    static QString findClaudeExecutable();
    static QProcessEnvironment toolEnvironment();

    bool isBusy() const { return busy_; }

    // Sends a user message; starts (or resumes) the Claude Code process when needed.
    void send(const QString& text);
    // Kills the running turn. The next message resumes the same conversation.
    void stop();
    // Switches to another conversation (empty: a new one); the next message resumes it.
    void setConversation(const QString& sessionId);
    void newConversation() { setConversation(QString()); }
    QString sessionId() const { return sessionId_; }

signals:
    void streamEvent(const presentation::chat::StreamEvent& event);
    void busyChanged(bool busy);
    void failed(const QString& message);
    // Claude Code assigned (or changed) the session id of the conversation.
    void sessionIdChanged(const QString& sessionId);

private:
    bool ensureProcess();
    void onStandardOutput();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void setBusy(bool busy);
    void rememberSession(const QString& sessionId);

    Options options_;
    Settings settings_;
    bool restartNeeded_ = false;  // settings changed since the process started
    QProcess* process_ = nullptr;
    QByteArray pendingOutput_;
    QByteArray recentErrors_;
    QString sessionId_;
    double processCostUsd_ = 0.0;  // Claude Code reports the process's cumulative cost
    bool busy_ = false;
    bool stopping_ = false;
};

} // namespace presentation::chat
