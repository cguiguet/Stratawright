#include "chat_panel.h"

#include "assistant_instructions.h"
#include "claude_account.h"
#include "claude_code_session.h"

#include "Middle Bridge/project/iproject_lifecycle_controller.h"
#include "Middle Bridge/project/isession_manager.h"

#include <QAction>
#include <QActionGroup>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QToolButton>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
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
    QToolButton#chatMenu { color: #e8e8e8; background: transparent; border: none; border-radius: 4px; font-size: 16px; padding: 0px 6px; }
    QToolButton#chatMenu:hover { background-color: #323232; }
    QToolButton#chatMenu::menu-indicator { image: none; }
)";

struct Choice {
    const char* label;
    const char* value;  // empty: Claude Code's default
};

constexpr Choice kModels[] = {
    {"Default", ""}, {"Fable", "fable"}, {"Opus", "opus"}, {"Sonnet", "sonnet"}, {"Haiku", "haiku"},
};
constexpr Choice kEfforts[] = {
    {"Default", ""}, {"Low", "low"}, {"Medium", "medium"}, {"High", "high"}, {"Extra high", "xhigh"}, {"Max", "max"},
};

template <size_t N>
QString labelOf(const Choice (&choices)[N], const QString& value) {
    for (const Choice& choice : choices) {
        if (value == QLatin1String(choice.value)) {
            return QString::fromLatin1(choice.label);
        }
    }
    return value;
}

// Adds the choices to menu as exclusive checkable items calling onChosen with the value.
template <size_t N, typename OnChosen>
void addChoices(QMenu* menu, const Choice (&choices)[N], const QString& current, OnChosen onChosen) {
    auto* group = new QActionGroup(menu);
    for (const Choice& choice : choices) {
        QAction* action = menu->addAction(QString::fromLatin1(choice.label));
        action->setCheckable(true);
        action->setChecked(current == QLatin1String(choice.value));
        group->addAction(action);
        const QString value = QString::fromLatin1(choice.value);
        QObject::connect(action, &QAction::triggered, menu, [onChosen, value]() { onChosen(value); });
    }
}

ClaudeCodeSession::Settings loadSettings() {
    const QSettings settings(QStringLiteral("Stratawright"), QStringLiteral("Stratawright"));
    ClaudeCodeSession::Settings loaded;
    loaded.model = settings.value(QStringLiteral("chat/model")).toString();
    loaded.effort = settings.value(QStringLiteral("chat/effort")).toString();
    loaded.readOnly = settings.value(QStringLiteral("chat/readOnly"), false).toBool();
    return loaded;
}

QString escaped(const QString& text) {
    return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

const QColor kAccent(0xa7, 0x8b, 0xfa);
const QColor kText(0xe8, 0xe8, 0xe8);
const QColor kMuted(0x8a, 0x8a, 0x8a);
const QColor kUserBubble(0x2e, 0x29, 0x45);
const QColor kClaudeBubble(0x26, 0x26, 0x26);
constexpr int kBubblePadding = 8;

QTextCharFormat textFormat(const QColor& color) {
    QTextCharFormat format;
    format.setForeground(color);
    return format;
}

QTextCharFormat noteFormat(const QColor& color) {
    QFont font(QStringLiteral("Menlo"));
    font.setPixelSize(11);
    QTextCharFormat format;
    format.setFont(font);
    format.setForeground(color);
    return format;
}

// Inserts a bubble at the end of the transcript, headed by its author, and returns it.
QTextFrame* insertBubble(QTextDocument* document, const QString& author, const QColor& background, int leftMargin) {
    QTextCursor cursor(document);
    cursor.movePosition(QTextCursor::End);
    QTextFrameFormat format;
    format.setBackground(background);
    format.setBorder(0);
    format.setPadding(kBubblePadding);
    format.setTopMargin(4);
    format.setLeftMargin(leftMargin);
    QTextFrame* frame = cursor.insertFrame(format);
    QTextCharFormat authorFormat = textFormat(kAccent);
    authorFormat.setFontWeight(QFont::Bold);
    cursor.insertText(author, authorFormat);
    return frame;
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
    input_->setPlaceholderText(QStringLiteral("Message Claude… (Enter to send, Shift+Enter for a new line, /mic to record)"));
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
    session_->setSettings(loadSettings());
    account_ = new ClaudeAccount(this);
    auto* header = new QHBoxLayout();
    header->addWidget(projectLabel_);
    header->addStretch();
    header->addWidget(buildMenuButton());

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(newButton);
    buttons->addWidget(relaunchButton);
    buttons->addStretch();
    buttons->addWidget(stopButton_);
    buttons->addWidget(sendButton_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addLayout(header);
    layout->addWidget(transcript_, 1);
    layout->addWidget(input_);
    layout->addLayout(buttons);
    layout->addWidget(statusLabel_);

    connect(sendButton_, &QPushButton::clicked, this, &ChatPanel::sendInput);
    connect(stopButton_, &QPushButton::clicked, this, [this]() {
        if (mic_->isRecording()) {
            mic_->stop();
        } else {
            session_->stop();
        }
    });
    mic_ = new MicRecorder(this);
    connect(mic_, &MicRecorder::started, this, [this](const QString& description) {
        appendNote(QStringLiteral("🎤 ") + description);
        statusLabel_->setText(QStringLiteral("Recording…"));
        updateStopButton();
    });
    connect(mic_, &MicRecorder::finished, this, [this](const QString& reference, const QString& description) {
        updateStopButton();
        statusLabel_->setText(description);
        if (reference.isEmpty()) {
            appendNote(QStringLiteral("🎤 ") + description);
            return;
        }
        appendNote(QStringLiteral("🎤 %1 Say what to make of it, e.g. \"a rock drum beat from this rhythm\".").arg(description));
        insertReference(reference);
    });
    connect(newButton, &QPushButton::clicked, this, [this]() {
        session_->newConversation();
        transcript_->clear();
        replyFrame_ = nullptr;
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
    replyFrame_ = nullptr;
    if (conversation.transcriptHtml.isEmpty()) {
        transcript_->clear();
    } else {
        transcript_->setHtml(conversation.transcriptHtml);
        // Saved bubbles come back without their padding.
        for (QTextFrame* bubble : transcript_->document()->rootFrame()->childFrames()) {
            QTextFrameFormat format = bubble->frameFormat();
            format.setPadding(kBubblePadding);
            bubble->setFrameFormat(format);
        }
        scrollToBottom();
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

QToolButton* ChatPanel::buildMenuButton() {
    auto* button = new QToolButton(this);
    button->setObjectName(QStringLiteral("chatMenu"));
    button->setText(QStringLiteral("⋯"));
    button->setToolTip(QStringLiteral("Chat settings"));
    button->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(button);
    button->setMenu(menu);

    accountAction_ = menu->addAction(account_->status().description());
    accountAction_->setEnabled(false);
    signAction_ = menu->addAction(QStringLiteral("Sign out…"));
    connect(signAction_, &QAction::triggered, this, [this]() {
        if (!account_->status().loggedIn) {
            account_->signInInTerminal();
            statusLabel_->setText(QStringLiteral("Finish signing in in Terminal, then open this menu again."));
            return;
        }
        const auto answer = QMessageBox::question(this, QStringLiteral("Sign out"),
            QStringLiteral("Sign out of Claude Code on this Mac? Claude Code in your terminal is signed out too."));
        if (answer == QMessageBox::Yes) {
            account_->signOut();
        }
    });
    menu->addSeparator();

    const ClaudeCodeSession::Settings& settings = session_->settings();
    modelMenu_ = menu->addMenu(QString());
    addChoices(modelMenu_, kModels, settings.model, [this](const QString& model) {
        ClaudeCodeSession::Settings changed = session_->settings();
        changed.model = model;
        applySettings(changed);
    });
    effortMenu_ = menu->addMenu(QString());
    addChoices(effortMenu_, kEfforts, settings.effort, [this](const QString& effort) {
        ClaudeCodeSession::Settings changed = session_->settings();
        changed.effort = effort;
        applySettings(changed);
    });
    readOnlyAction_ = menu->addAction(QStringLiteral("Read only: answers and plans, changes nothing"));
    readOnlyAction_->setCheckable(true);
    readOnlyAction_->setChecked(settings.readOnly);
    connect(readOnlyAction_, &QAction::toggled, this, [this](bool readOnly) {
        ClaudeCodeSession::Settings changed = session_->settings();
        changed.readOnly = readOnly;
        applySettings(changed);
    });
    menu->addSeparator();

    connect(menu->addAction(QStringLiteral("Export conversation…")), &QAction::triggered, this, &ChatPanel::exportConversation);
    connect(menu->addAction(QStringLiteral("Copy conversation")), &QAction::triggered, this, [this]() {
        QGuiApplication::clipboard()->setText(transcript_->document()->toMarkdown());
        statusLabel_->setText(QStringLiteral("Conversation copied as Markdown"));
    });
    terminalAction_ = menu->addAction(QStringLiteral("Continue in Terminal"));
    terminalAction_->setToolTip(QStringLiteral("Opens this conversation in Claude Code in Terminal."));
    connect(terminalAction_, &QAction::triggered, this, &ChatPanel::continueInTerminal);

    connect(menu, &QMenu::aboutToShow, this, [this]() {
        account_->refresh();
        terminalAction_->setEnabled(!conversation_.sessionId.isEmpty() && !session_->isBusy());
    });
    connect(account_, &ClaudeAccount::statusChanged, this, &ChatPanel::updateAccountActions);
    connect(account_, &ClaudeAccount::failed, this, &ChatPanel::showError);
    account_->refresh();
    applySettings(settings);  // titles of the submenus
    return button;
}

void ChatPanel::applySettings(const ClaudeCodeSession::Settings& settings) {
    const bool changed = !(settings == session_->settings());
    session_->setSettings(settings);
    modelMenu_->setTitle(QStringLiteral("Model · %1").arg(labelOf(kModels, settings.model)));
    effortMenu_->setTitle(QStringLiteral("Effort · %1").arg(labelOf(kEfforts, settings.effort)));
    if (!changed) {
        return;
    }
    QSettings stored(QStringLiteral("Stratawright"), QStringLiteral("Stratawright"));
    stored.setValue(QStringLiteral("chat/model"), settings.model);
    stored.setValue(QStringLiteral("chat/effort"), settings.effort);
    stored.setValue(QStringLiteral("chat/readOnly"), settings.readOnly);
    statusLabel_->setText(QStringLiteral("%1 · effort %2%3 — from the next message")
                              .arg(labelOf(kModels, settings.model), labelOf(kEfforts, settings.effort).toLower(),
                                   settings.readOnly ? QStringLiteral(" · read only") : QString()));
}

void ChatPanel::updateAccountActions() {
    const AccountStatus& status = account_->status();
    accountAction_->setText(status.description());
    signAction_->setText(status.loggedIn ? QStringLiteral("Sign out…") : QStringLiteral("Sign in…"));
    signAction_->setEnabled(status.known || !ClaudeCodeSession::findClaudeExecutable().isEmpty());
}

void ChatPanel::exportConversation() {
    const QString name = conversation_.projectPath.isEmpty()
        ? QStringLiteral("Untitled project")
        : QFileInfo(conversation_.projectPath).completeBaseName();
    const QString directory = conversation_.projectPath.isEmpty() ? QDir::homePath() : QFileInfo(conversation_.projectPath).absolutePath();
    QString filter;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export conversation"),
        QDir(directory).filePath(name + QStringLiteral(" - chat.md")),
        QStringLiteral("Markdown (*.md);;Web page (*.html)"), &filter);
    if (path.isEmpty()) {
        return;
    }
    const bool html = path.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive) || filter.contains(QStringLiteral("html"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        showError(QStringLiteral("Could not write %1.").arg(path));
        return;
    }
    file.write((html ? transcript_->toHtml() : transcript_->document()->toMarkdown()).toUtf8());
    if (!file.commit()) {
        showError(QStringLiteral("Could not write %1.").arg(path));
        return;
    }
    statusLabel_->setText(QStringLiteral("Conversation exported to %1").arg(QFileInfo(path).fileName()));
}

void ChatPanel::continueInTerminal() {
    const QString claude = ClaudeCodeSession::findClaudeExecutable();
    if (conversation_.sessionId.isEmpty() || claude.isEmpty()) {
        return;
    }
    // The panel's process lets go of the conversation; its next message picks up what was said in
    // Terminal.
    session_->stop();
    QStringList command{claude, QStringLiteral("--resume"), conversation_.sessionId};
    const ClaudeCodeSession::Settings& settings = session_->settings();
    if (!settings.model.isEmpty()) {
        command << QStringLiteral("--model") << settings.model;
    }
    if (!settings.effort.isEmpty()) {
        command << QStringLiteral("--effort") << settings.effort;
    }
    const QString source = sourceDirectory();
    if (!runInTerminal(source.isEmpty() ? QDir::homePath() : source, command)) {
        showError(QStringLiteral("Could not open Terminal."));
    }
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

void ChatPanel::setRecordingContext(const MicRecorder::Controllers& controllers) {
    mic_->setControllers(controllers);
}

void ChatPanel::sendInput() {
    const QString text = input_->toPlainText().trimmed();
    // Panel commands work even while Claude is busy (a second /mic stops the take).
    if (text == QStringLiteral("/mic") || text.startsWith(QStringLiteral("/mic "))) {
        input_->clear();
        handleMicCommand(text.mid(4).trimmed());
        return;
    }
    if (text.isEmpty() || session_->isBusy()) {
        return;
    }
    input_->clear();
    appendUserMessage(text);
    startReply();
    session_->send(text);
}

void ChatPanel::onStreamEvent(const StreamEvent& event) {
    switch (event.kind) {
    case StreamEvent::Kind::SessionStarted:
        model_ = event.model;
        statusLabel_->setText(QStringLiteral("Claude Code · %1").arg(model_));
        break;
    case StreamEvent::Kind::AssistantText: {
        QTextCursor cursor = replyCursor();
        cursor.insertBlock(QTextBlockFormat(), textFormat(kText));
        cursor.insertMarkdown(event.text);
        scrollToBottom();
        scheduleSave();
        break;
    }
    case StreamEvent::Kind::ToolUse: {
        QTextCursor cursor = replyCursor();
        cursor.insertBlock(QTextBlockFormat(), noteFormat(kMuted));
        cursor.insertText(QStringLiteral("⚙ %1 · %2").arg(event.toolName, event.text));
        scrollToBottom();
        scheduleSave();
        break;
    }
    case StreamEvent::Kind::ToolResult:
        if (event.isError) {
            QTextCursor cursor = replyCursor();
            cursor.insertBlock(QTextBlockFormat(), noteFormat(QColor(0xf8, 0x71, 0x71)));
            cursor.insertText(QStringLiteral("✗ %1").arg(event.text));
            scheduleSave();
        }
        break;
    case StreamEvent::Kind::TurnFinished:
        finishReply();
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

void ChatPanel::handleMicCommand(const QString& argument) {
    if (mic_->isRecording()) {
        mic_->stop();
        return;
    }
    const std::vector<MicRecorder::Input> inputs = mic_->inputs();
    if (inputs.empty()) {
        appendNote(QStringLiteral("🎤 No audio input: connect your sound card, or pick it in Settings."));
        return;
    }
    if (!argument.isEmpty()) {
        bool ok = false;
        const uint32_t number = argument.toUInt(&ok);
        for (const auto& input : inputs) {
            if (ok && input.channel + 1 == number) {
                startMic(input);
                return;
            }
        }
        appendNote(QStringLiteral("🎤 No input %1 here: type /mic to choose.").arg(argument));
        return;
    }
    // Which input: the menu remembers the last one.
    const uint32_t last = QSettings(QStringLiteral("Stratawright"), QStringLiteral("Stratawright"))
                              .value(QStringLiteral("chat/micInput"), 0).toUInt();
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addSection(QStringLiteral("Record from"));
    for (const auto& input : inputs) {
        QAction* action = menu->addAction(input.label);
        action->setCheckable(true);
        action->setChecked(input.channel == last);
        connect(action, &QAction::triggered, this, [this, input]() { startMic(input); });
    }
    menu->popup(input_->mapToGlobal(QPoint(0, input_->height())));
}

void ChatPanel::startMic(const MicRecorder::Input& input) {
    QSettings(QStringLiteral("Stratawright"), QStringLiteral("Stratawright"))
        .setValue(QStringLiteral("chat/micInput"), input.channel);
    const QString problem = mic_->start(input);
    if (!problem.isEmpty()) {
        appendNote(QStringLiteral("🎤 ") + problem);
    }
}

void ChatPanel::updateStopButton() {
    stopButton_->setEnabled(session_->isBusy() || mic_->isRecording());
}

void ChatPanel::appendNote(const QString& text) {
    QTextCursor cursor(transcript_->document());
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat block;
    block.setTopMargin(6);
    QTextCharFormat note = textFormat(kMuted);
    note.setFontItalic(true);
    cursor.insertBlock(block, note);
    cursor.insertText(text);
    scrollToBottom();
    scheduleSave();
}

void ChatPanel::onBusyChanged(bool busy) {
    sendButton_->setEnabled(!busy);
    updateStopButton();
    if (busy) {
        statusLabel_->setText(QStringLiteral("Claude is working…"));
    } else {
        finishReply();
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
    scrollToBottom();
    scheduleSave();
}

void ChatPanel::scrollToBottom() {
    // After the next layout pass: the scroll range does not include text just inserted yet.
    QTimer::singleShot(0, transcript_, [this]() {
        transcript_->verticalScrollBar()->setValue(transcript_->verticalScrollBar()->maximum());
    });
}

void ChatPanel::appendUserMessage(const QString& text) {
    QTextFrame* bubble = insertBubble(transcript_->document(), QStringLiteral("You"), kUserBubble, 24);
    QTextCursor cursor = bubble->lastCursorPosition();
    cursor.insertBlock(QTextBlockFormat(), textFormat(kText));
    cursor.insertText(text);
    scrollToBottom();
    scheduleSave();
}

void ChatPanel::startReply() {
    replyFrame_ = insertBubble(transcript_->document(), QStringLiteral("Claude"), kClaudeBubble, 0);
    QTextCursor cursor = replyFrame_->lastCursorPosition();
    QTextCharFormat placeholder = textFormat(kMuted);
    placeholder.setFontItalic(true);
    cursor.insertBlock(QTextBlockFormat(), placeholder);
    cursor.insertText(QStringLiteral("working…"));
    replyPlaceholder_ = true;
    scrollToBottom();
}

QTextCursor ChatPanel::replyCursor() {
    if (!replyFrame_) {
        startReply();  // output without a message from here (e.g. a resumed turn)
    }
    QTextCursor cursor = replyFrame_->lastCursorPosition();
    if (replyPlaceholder_) {
        // Drop the placeholder line: the output takes its place.
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        replyPlaceholder_ = false;
    }
    return cursor;
}

void ChatPanel::finishReply() {
    if (replyFrame_ && replyPlaceholder_) {
        QTextCursor cursor = replyFrame_->lastCursorPosition();
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        QTextCharFormat note = textFormat(kMuted);
        note.setFontItalic(true);
        cursor.insertText(QStringLiteral("(no answer)"), note);
        replyPlaceholder_ = false;
        scheduleSave();
    }
    replyFrame_ = nullptr;
}

} // namespace presentation::chat
