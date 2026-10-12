#include "chat_panel.h"

#include "assistant_instructions.h"
#include "claude_code_session.h"

#include "Middle Bridge/project/iproject_lifecycle_controller.h"
#include "Middle Bridge/project/isession_manager.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

namespace presentation::chat {

namespace {

const char* const kPanelStyle = R"(
    QWidget#chatPanel { background-color: #1b1b1b; }
    QTextBrowser { background-color: #1b1b1b; color: #e8e8e8; border: none; font-size: 13px; }
    QPlainTextEdit { background-color: #262626; color: #e8e8e8; border: 1px solid #323232; border-radius: 8px; padding: 6px; font-size: 13px; }
    QPlainTextEdit:focus { border-color: #a78bfa; }
    QPushButton { background-color: #323232; color: #e8e8e8; border: none; border-radius: 6px; padding: 5px 10px; }
    QPushButton:hover { background-color: #3a4454; }
    QPushButton:disabled { color: #666666; }
    QPushButton#sendButton { background-color: #a78bfa; color: #161616; font-weight: bold; }
    QLabel#chatStatus { color: #888888; font-size: 11px; }
    QLabel#chatProject { color: #a78bfa; font-size: 12px; font-weight: bold; }
)";

QString escaped(const QString& text) {
    return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

} // namespace

ChatPanel::ChatPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("chatPanel"));
    setStyleSheet(QString::fromUtf8(kPanelStyle));

    const QString source = sourceDirectory();
    session_ = new ClaudeCodeSession({source.isEmpty() ? QDir::homePath() : source, assistantInstructions()}, this);

    transcript_ = new QTextBrowser(this);
    transcript_->setOpenExternalLinks(true);
    transcript_->setPlaceholderText(QStringLiteral(
        "Ask Claude to change the project (\"add a rock drum beat on bars 1-8\") or the app itself "
        "(\"the mixer crashes when...\"). It runs with your Claude Code login."));

    input_ = new QPlainTextEdit(this);
    input_->setPlaceholderText(QStringLiteral("Message Claude… (Enter to send, Shift+Enter for a new line)"));
    input_->setFixedHeight(72);
    input_->installEventFilter(this);

    sendButton_ = new QPushButton(QStringLiteral("Send"), this);
    sendButton_->setObjectName(QStringLiteral("sendButton"));
    stopButton_ = new QPushButton(QStringLiteral("Stop"), this);
    stopButton_->setEnabled(false);
    auto* newButton = new QPushButton(QStringLiteral("New chat"), this);
    auto* relaunchButton = new QPushButton(QStringLiteral("Relaunch"), this);
    relaunchButton->setToolTip(QStringLiteral("Restart Stratawright to load a rebuilt version; the project and the conversation are kept."));

    statusLabel_ = new QLabel(QStringLiteral("Claude Code"), this);
    statusLabel_->setObjectName(QStringLiteral("chatStatus"));
    projectLabel_ = new QLabel(this);
    projectLabel_->setObjectName(QStringLiteral("chatProject"));

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(newButton);
    buttons->addWidget(relaunchButton);
    buttons->addStretch();
    buttons->addWidget(stopButton_);
    buttons->addWidget(sendButton_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(projectLabel_);
    layout->addWidget(transcript_, 1);
    layout->addWidget(input_);
    layout->addLayout(buttons);
    layout->addWidget(statusLabel_);

    connect(sendButton_, &QPushButton::clicked, this, &ChatPanel::sendInput);
    connect(stopButton_, &QPushButton::clicked, session_, &ClaudeCodeSession::stop);
    connect(newButton, &QPushButton::clicked, this, [this]() {
        session_->newConversation();
        transcript_->clear();
        conversation_.sessionId.clear();
        sessionCostUsd_ = 0.0;
        statusLabel_->setText(QStringLiteral("New conversation"));
        saveConversation();
    });
    connect(relaunchButton, &QPushButton::clicked, this, &ChatPanel::relaunchRequested);
    connect(session_, &ClaudeCodeSession::streamEvent, this, &ChatPanel::onStreamEvent);
    connect(session_, &ClaudeCodeSession::busyChanged, this, &ChatPanel::onBusyChanged);
    connect(session_, &ClaudeCodeSession::failed, this, &ChatPanel::showError);
    connect(session_, &ClaudeCodeSession::sessionIdChanged, this, [this](const QString& sessionId) {
        conversation_.sessionId = sessionId;
        scheduleSave();
    });

    saveTimer_ = new QTimer(this);
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(400);
    connect(saveTimer_, &QTimer::timeout, this, &ChatPanel::saveConversation);

    // Until the project is known, show the never-saved project's conversation.
    switchTo(store_.load(QString()));
}

void ChatPanel::setProjectContext(bridge::IProjectLifecycleController* lifecycle, bridge::ISessionManager* sessions) {
    lifecycle_ = lifecycle;
    sessions_ = sessions;
    if (!projectWatch_) {
        // Polled rather than notified: the path only settles after the session switch (open,
        // relaunch restore) and also changes on "save as". The first check runs after startup work.
        projectWatch_ = new QTimer(this);
        projectWatch_->setInterval(1000);
        connect(projectWatch_, &QTimer::timeout, this, &ChatPanel::checkProject);
        projectWatch_->start();
    }
}

void ChatPanel::checkProject() {
    if (!lifecycle_ || !sessions_) {
        return;
    }
    const void* session = sessions_->getActiveSession();
    const QString path = QString::fromStdString(lifecycle_->getCurrentProjectPath());
    if (session == watchedSession_ && path == conversation_.projectPath) {
        return;
    }
    const bool firstCheck = (watchedSession_ == nullptr);
    const bool sameProject = (session == watchedSession_);
    watchedSession_ = session;

    if (sameProject) {
        // Saved under a new path: the conversation follows the project.
        const QString previousPath = conversation_.projectPath;
        conversation_.projectPath = path;
        saveConversation();
        if (previousPath.isEmpty()) {
            store_.remove(previousPath);
        }
        updateProjectLabel();
        return;
    }
    if (path.isEmpty() && !firstCheck) {
        switchTo(Conversation{});  // a new project starts a new conversation
        return;
    }
    switchTo(store_.load(path));  // opened, or restored after a relaunch
}

void ChatPanel::switchTo(const Conversation& conversation) {
    if (saveTimer_ && saveTimer_->isActive()) {
        saveTimer_->stop();
        saveConversation();
    }
    conversation_ = conversation;
    session_->setConversation(conversation.sessionId);
    if (conversation.transcriptHtml.isEmpty()) {
        transcript_->clear();
    } else {
        transcript_->setHtml(conversation.transcriptHtml);
        transcript_->verticalScrollBar()->setValue(transcript_->verticalScrollBar()->maximum());
    }
    sessionCostUsd_ = 0.0;
    updateProjectLabel();
}

void ChatPanel::scheduleSave() {
    saveTimer_->start();
}

void ChatPanel::saveConversation() {
    conversation_.transcriptHtml = transcript_->document()->isEmpty() ? QString() : transcript_->toHtml();
    if (conversation_.sessionId.isEmpty() && conversation_.transcriptHtml.isEmpty()) {
        store_.remove(conversation_.projectPath);
        return;
    }
    store_.save(conversation_);
}

void ChatPanel::updateProjectLabel() {
    const QString name = conversation_.projectPath.isEmpty()
        ? QStringLiteral("Untitled project")
        : QFileInfo(conversation_.projectPath).completeBaseName();
    projectLabel_->setText(QStringLiteral("Chat · %1").arg(name));
    projectLabel_->setToolTip(conversation_.projectPath.isEmpty()
        ? QStringLiteral("This conversation moves to the project when you save it.")
        : conversation_.projectPath);
}

void ChatPanel::insertReference(const QString& reference) {
    QString text = input_->toPlainText();
    if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n'))) {
        text += QLatin1Char('\n');
    }
    input_->setPlainText(text + reference + QLatin1Char('\n'));
    input_->moveCursor(QTextCursor::End);
    input_->setFocus();
}

void ChatPanel::focusInput() {
    input_->setFocus();
}

bool ChatPanel::eventFilter(QObject* watched, QEvent* event) {
    // Typing must reach the input, not the app's single-key shortcuts (Space plays, K toggles
    // the metronome...): claim plain keystrokes before shortcuts are matched.
    if (watched == input_ && event->type() == QEvent::ShortcutOverride) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (!(key->modifiers() & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier))) {
            event->accept();
            return true;
        }
    }
    if (watched == input_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
        if (enter && !(key->modifiers() & Qt::ShiftModifier)) {
            sendInput();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChatPanel::sendInput() {
    const QString text = input_->toPlainText().trimmed();
    if (text.isEmpty() || session_->isBusy()) {
        return;
    }
    input_->clear();
    appendHtml(QStringLiteral("<p style=\"color:#a78bfa; margin-top:10px;\"><b>You</b><br><span style=\"color:#e8e8e8;\">%1</span></p>")
                   .arg(escaped(text)));
    session_->send(text);
}

void ChatPanel::onStreamEvent(const StreamEvent& event) {
    switch (event.kind) {
    case StreamEvent::Kind::SessionStarted:
        model_ = event.model;
        statusLabel_->setText(QStringLiteral("Claude Code · %1").arg(model_));
        break;
    case StreamEvent::Kind::AssistantText:
        appendMarkdown(event.text);
        break;
    case StreamEvent::Kind::ToolUse:
        appendHtml(QStringLiteral("<p style=\"color:#8a8a8a; font-family:Menlo,monospace; font-size:11px; margin:2px 0;\">⚙ %1 · %2</p>")
                       .arg(escaped(event.toolName), escaped(event.text)));
        break;
    case StreamEvent::Kind::ToolResult:
        if (event.isError) {
            appendHtml(QStringLiteral("<p style=\"color:#f87171; font-family:Menlo,monospace; font-size:11px; margin:2px 0;\">✗ %1</p>")
                           .arg(escaped(event.text)));
        }
        break;
    case StreamEvent::Kind::TurnFinished:
        sessionCostUsd_ += event.costUsd;
        statusLabel_->setText(QStringLiteral("Claude Code · %1 · $%2 this session")
                                  .arg(model_)
                                  .arg(sessionCostUsd_, 0, 'f', 2));
        if (event.isError) {
            showError(event.text);
        }
        break;
    }
}

void ChatPanel::onBusyChanged(bool busy) {
    sendButton_->setEnabled(!busy);
    stopButton_->setEnabled(busy);
    if (busy) {
        statusLabel_->setText(QStringLiteral("Claude is working…"));
    }
}

void ChatPanel::showError(const QString& message) {
    appendHtml(QStringLiteral("<p style=\"color:#f87171;\">%1</p>").arg(escaped(message)));
}

void ChatPanel::appendHtml(const QString& html) {
    QTextCursor cursor(transcript_->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertBlock();
    cursor.insertHtml(html);
    transcript_->verticalScrollBar()->setValue(transcript_->verticalScrollBar()->maximum());
    scheduleSave();
}

void ChatPanel::appendMarkdown(const QString& markdown) {
    QTextCursor cursor(transcript_->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertBlock();
    cursor.insertMarkdown(markdown);
    transcript_->verticalScrollBar()->setValue(transcript_->verticalScrollBar()->maximum());
    scheduleSave();
}

} // namespace presentation::chat
