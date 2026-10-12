#pragma once

#include <QString>

namespace presentation::chat {

// One chat conversation per project: the Claude Code session to resume and what the panel showed.
struct Conversation {
    QString projectPath;     // empty for a project that was never saved
    QString sessionId;       // Claude Code session, empty until the first answer
    QString transcriptHtml;  // the panel's transcript, restored as is
};

// Keeps conversations as JSON files (one per project path) in a directory, by default
// <app data>/Stratawright/chats.
class ConversationStore {
public:
    explicit ConversationStore(QString directory = QString());

    bool contains(const QString& projectPath) const;
    // The project's conversation, or an empty one (with projectPath set) if there is none yet.
    Conversation load(const QString& projectPath) const;
    void save(const Conversation& conversation) const;
    void remove(const QString& projectPath) const;

private:
    QString fileFor(const QString& projectPath) const;

    QString directory_;
};

} // namespace presentation::chat
