#include "conversation_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace presentation::chat {

ConversationStore::ConversationStore(QString directory) : directory_(std::move(directory)) {
    if (directory_.isEmpty()) {
        directory_ = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Stratawright/chats");
    }
}

QString ConversationStore::fileFor(const QString& projectPath) const {
    // Paths become file names through a hash; the never-saved project gets a fixed name.
    const QString name = projectPath.isEmpty()
        ? QStringLiteral("untitled")
        : QString::fromLatin1(QCryptographicHash::hash(projectPath.toUtf8(), QCryptographicHash::Sha1).toHex());
    return directory_ + QLatin1Char('/') + name + QStringLiteral(".json");
}

bool ConversationStore::contains(const QString& projectPath) const {
    return QFile::exists(fileFor(projectPath));
}

Conversation ConversationStore::load(const QString& projectPath) const {
    Conversation conversation;
    conversation.projectPath = projectPath;
    QFile file(fileFor(projectPath));
    if (!file.open(QIODevice::ReadOnly)) {
        return conversation;
    }
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    conversation.sessionId = object.value(QStringLiteral("sessionId")).toString();
    conversation.transcriptHtml = object.value(QStringLiteral("transcript")).toString();
    return conversation;
}

void ConversationStore::save(const Conversation& conversation) const {
    QDir().mkpath(directory_);
    QJsonObject object;
    object.insert(QStringLiteral("project"), conversation.projectPath);
    object.insert(QStringLiteral("sessionId"), conversation.sessionId);
    object.insert(QStringLiteral("transcript"), conversation.transcriptHtml);
    QSaveFile file(fileFor(conversation.projectPath));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void ConversationStore::remove(const QString& projectPath) const {
    QFile::remove(fileFor(projectPath));
}

} // namespace presentation::chat
