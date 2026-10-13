#pragma once

#include "claude_code_session.h"
#include "claude_stream.h"
#include "conversation_store.h"
#include "mic_recorder.h"

#include <QWidget>

class QAction;
class QLabel;
class QMenu;
class QToolButton;
class QTimer;
class QPlainTextEdit;
class QTextCursor;
class QTextFrame;
class QPushButton;
class QTextBrowser;

namespace bridge {
class IProjectLifecycleController;
class ISessionManager;
}

namespace presentation::chat {

class ClaudeAccount;

// Chat with Claude Code from inside the app: it can drive the open project through daw-cli and
// change the app's own code. See ClaudeCodeSession for how Claude Code is run. Each project has its
// own conversation (ConversationStore): opening another project switches to its conversation, and
// saving a project under a new path takes the conversation along.
class ChatPanel : public QWidget {
    Q_OBJECT

public:
    explicit ChatPanel(QWidget* parent = nullptr);

    void focusInput();
    void showError(const QString& message);
    // Follows the open project to show its conversation.
    void setProjectContext(bridge::IProjectLifecycleController* lifecycle, bridge::ISessionManager* sessions);
    // Adds a reference to something in the project (e.g. the selection) to the message being typed.
    void insertReference(const QString& reference);
    // What /mic records with.
    void setRecordingContext(const MicRecorder::Controllers& controllers);

signals:
    // The user asked to restart the app (to load a rebuilt version).
    void relaunchRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void sendInput();
    // /mic [N]: records input N of the sound card (asks which without N); a second /mic stops.
    void handleMicCommand(const QString& argument);
    void startMic(const MicRecorder::Input& input);
    void updateStopButton();
    // A line in the transcript between the bubbles, for what the panel itself did.
    void appendNote(const QString& text);
    void onStreamEvent(const StreamEvent& event);
    void onBusyChanged(bool busy);
    void appendHtml(const QString& html);
    // Each message is a bubble: the user's, then one for Claude's whole answer (its text and the
    // tools it ran), shown as soon as the message is sent with a placeholder until output arrives.
    void appendUserMessage(const QString& text);
    void startReply();
    QTextCursor replyCursor();
    void finishReply();
    void scrollToBottom();
    void checkProject();
    void switchTo(const Conversation& conversation);
    void scheduleSave();
    void saveConversation();
    void updateProjectLabel();
    // The "⋯" menu: account, model, effort, read-only mode, export and terminal.
    QToolButton* buildMenuButton();
    void applySettings(const ClaudeCodeSession::Settings& settings);
    void updateAccountActions();
    void exportConversation();
    void continueInTerminal();

    ClaudeCodeSession* session_ = nullptr;
    QTextBrowser* transcript_ = nullptr;
    QPlainTextEdit* input_ = nullptr;
    QPushButton* sendButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* projectLabel_ = nullptr;
    ClaudeAccount* account_ = nullptr;
    MicRecorder* mic_ = nullptr;
    QAction* accountAction_ = nullptr;
    QAction* signAction_ = nullptr;
    QAction* readOnlyAction_ = nullptr;
    QAction* terminalAction_ = nullptr;
    QMenu* modelMenu_ = nullptr;
    QMenu* effortMenu_ = nullptr;
    ConversationStore store_;
    Conversation conversation_;
    bridge::IProjectLifecycleController* lifecycle_ = nullptr;
    bridge::ISessionManager* sessions_ = nullptr;
    const void* watchedSession_ = nullptr;
    QTimer* projectWatch_ = nullptr;
    QTimer* saveTimer_ = nullptr;
    QTextFrame* replyFrame_ = nullptr;   // Claude's bubble for the current message
    bool replyPlaceholder_ = false;      // it still only shows the "working" placeholder
    QString model_;
    double sessionCostUsd_ = 0.0;
};

} // namespace presentation::chat
