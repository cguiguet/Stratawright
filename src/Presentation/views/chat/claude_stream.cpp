#include "claude_stream.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace presentation::chat {

namespace {

constexpr qsizetype kSummaryLength = 160;

QString firstLine(const QString& text) {
    QString line = text.trimmed().section(QLatin1Char('\n'), 0, 0);
    if (line.size() > kSummaryLength) {
        line = line.left(kSummaryLength) + QStringLiteral("…");
    }
    return line;
}

// tool_result content is either a string or a list of {"type": "text", "text": ...} blocks.
QString toolResultText(const QJsonValue& content) {
    if (content.isString()) {
        return content.toString();
    }
    QStringList parts;
    for (const QJsonValue& block : content.toArray()) {
        const QJsonObject object = block.toObject();
        if (object.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
            parts << object.value(QStringLiteral("text")).toString();
        }
    }
    return parts.join(QLatin1Char('\n'));
}

} // namespace

QString summarizeToolInput(const QString& toolName, const QJsonObject& input) {
    for (const char* key : {"command", "file_path", "pattern", "url", "query", "description", "prompt"}) {
        const QString value = input.value(QLatin1String(key)).toString();
        if (!value.isEmpty()) {
            return firstLine(value);
        }
    }
    return toolName;
}

std::vector<StreamEvent> parseStreamLine(const QByteArray& line) {
    std::vector<StreamEvent> events;
    const QJsonDocument document = QJsonDocument::fromJson(line);
    if (!document.isObject()) {
        return events;
    }
    const QJsonObject message = document.object();
    const QString type = message.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("system") && message.value(QStringLiteral("subtype")).toString() == QStringLiteral("init")) {
        StreamEvent event;
        event.kind = StreamEvent::Kind::SessionStarted;
        event.sessionId = message.value(QStringLiteral("session_id")).toString();
        event.model = message.value(QStringLiteral("model")).toString();
        events.push_back(event);
        return events;
    }

    if (type == QStringLiteral("assistant") || type == QStringLiteral("user")) {
        const QJsonArray content = message.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toArray();
        for (const QJsonValue& value : content) {
            const QJsonObject block = value.toObject();
            const QString blockType = block.value(QStringLiteral("type")).toString();
            StreamEvent event;
            if (blockType == QStringLiteral("text") && type == QStringLiteral("assistant")) {
                event.kind = StreamEvent::Kind::AssistantText;
                event.text = block.value(QStringLiteral("text")).toString();
                if (event.text.trimmed().isEmpty()) {
                    continue;
                }
            } else if (blockType == QStringLiteral("tool_use")) {
                event.kind = StreamEvent::Kind::ToolUse;
                event.toolName = block.value(QStringLiteral("name")).toString();
                event.text = summarizeToolInput(event.toolName, block.value(QStringLiteral("input")).toObject());
            } else if (blockType == QStringLiteral("tool_result")) {
                event.kind = StreamEvent::Kind::ToolResult;
                event.text = firstLine(toolResultText(block.value(QStringLiteral("content"))));
                event.isError = block.value(QStringLiteral("is_error")).toBool();
            } else {
                continue;
            }
            events.push_back(event);
        }
        return events;
    }

    if (type == QStringLiteral("result")) {
        StreamEvent event;
        event.kind = StreamEvent::Kind::TurnFinished;
        event.sessionId = message.value(QStringLiteral("session_id")).toString();
        event.text = message.value(QStringLiteral("result")).toString();
        event.isError = message.value(QStringLiteral("is_error")).toBool();
        event.costUsd = message.value(QStringLiteral("total_cost_usd")).toDouble();
        events.push_back(event);
    }
    return events;
}

QByteArray userMessageLine(const QString& text) {
    QJsonObject textBlock;
    textBlock.insert(QStringLiteral("type"), QStringLiteral("text"));
    textBlock.insert(QStringLiteral("text"), text);

    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("user"));
    message.insert(QStringLiteral("content"), QJsonArray{textBlock});

    QJsonObject line;
    line.insert(QStringLiteral("type"), QStringLiteral("user"));
    line.insert(QStringLiteral("message"), message);
    return QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n';
}

} // namespace presentation::chat
