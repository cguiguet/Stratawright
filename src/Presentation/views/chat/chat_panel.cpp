#include "chat_panel.h"

#include "assistant_instructions.h"
#include "claude_code_session.h"

#include <QDir>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextCursor>
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

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(newButton);
    buttons->addWidget(relaunchButton);
    buttons->addStretch();
    buttons->addWidget(stopButton_);
    buttons->addWidget(sendButton_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(transcript_, 1);
    layout->addWidget(input_);
    layout->addLayout(buttons);
    layout->addWidget(statusLabel_);

    connect(sendButton_, &QPushButton::clicked, this, &ChatPanel::sendInput);
    connect(stopButton_, &QPushButton::clicked, session_, &ClaudeCodeSession::stop);
    connect(newButton, &QPushButton::clicked, this, [this]() {
        session_->newConversation();
        transcript_->clear();
        sessionCostUsd_ = 0.0;
        statusLabel_->setText(QStringLiteral("New conversation"));
    });
    connect(relaunchButton, &QPushButton::clicked, this, &ChatPanel::relaunchRequested);
    connect(session_, &ClaudeCodeSession::streamEvent, this, &ChatPanel::onStreamEvent);
    connect(session_, &ClaudeCodeSession::busyChanged, this, &ChatPanel::onBusyChanged);
    connect(session_, &ClaudeCodeSession::failed, this, &ChatPanel::showError);
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
}

void ChatPanel::appendMarkdown(const QString& markdown) {
    QTextCursor cursor(transcript_->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertBlock();
    cursor.insertMarkdown(markdown);
    transcript_->verticalScrollBar()->setValue(transcript_->verticalScrollBar()->maximum());
}

} // namespace presentation::chat
