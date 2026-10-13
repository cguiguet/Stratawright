#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

namespace presentation::chat {

// The Claude Code login the chat runs with, as `claude auth status` reports it.
struct AccountStatus {
    bool known = false;     // the status could be read
    bool loggedIn = false;
    QString email;
    QString plan;           // subscription ("max", "pro"...) or the auth method

    // One line for the menu, e.g. "Signed in as me@example.com · Max".
    QString description() const;
};

// Reads the JSON printed by `claude auth status --json`.
AccountStatus parseAuthStatus(const QByteArray& json);

// Opens Terminal in directory running command (each element quoted for the shell), so the user
// can finish there what needs a terminal: signing in, continuing a conversation by hand.
bool runInTerminal(const QString& directory, const QStringList& command);

// Keeps the account status up to date and signs out. Signing in needs a browser and a terminal,
// so it goes through runInTerminal; the status is refreshed when asked (e.g. when the menu opens).
class ClaudeAccount : public QObject {
    Q_OBJECT

public:
    explicit ClaudeAccount(QObject* parent = nullptr);

    const AccountStatus& status() const { return status_; }
    void refresh();
    // Signs Claude Code out on this Mac (the terminal's Claude Code too).
    void signOut();
    void signInInTerminal();

signals:
    void statusChanged();
    void failed(const QString& message);

private:
    void runClaude(const QStringList& arguments, bool refreshAfter);

    AccountStatus status_;
};

} // namespace presentation::chat
