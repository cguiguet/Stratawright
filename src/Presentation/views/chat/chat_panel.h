#pragma once

#include "claude_stream.h"

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;

namespace presentation::chat {

class ClaudeCodeSession;

// Chat with Claude Code from inside the app: it can drive the open project through daw-cli and
// change the app's own code. See ClaudeCodeSession for how Claude Code is run.
class ChatPanel : public QWidget {
    Q_OBJECT

public:
    explicit ChatPanel(QWidget* parent = nullptr);

    void focusInput();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void sendInput();
    void onStreamEvent(const StreamEvent& event);
    void onBusyChanged(bool busy);
    void showError(const QString& message);
    void appendHtml(const QString& html);
    void appendMarkdown(const QString& markdown);
    void relaunchApplication();

    ClaudeCodeSession* session_ = nullptr;
    QTextBrowser* transcript_ = nullptr;
    QPlainTextEdit* input_ = nullptr;
    QPushButton* sendButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QString model_;
    double sessionCostUsd_ = 0.0;
};

} // namespace presentation::chat
