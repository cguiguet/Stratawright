#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <vector>

namespace presentation::chat {

// One thing worth showing from Claude Code's `--output-format stream-json` output.
struct StreamEvent {
    enum class Kind {
        SessionStarted,  // sessionId, model
        AssistantText,   // text
        ToolUse,         // toolName, text = short summary of the input
        ToolResult,      // text = first line of the output, isError
        TurnFinished,    // sessionId, text = final result, isError, costUsd
    };

    Kind kind = Kind::AssistantText;
    QString text;
    QString toolName;
    QString sessionId;
    QString model;
    bool isError = false;
    double costUsd = 0.0;
};

// Events carried by one line of stream-json output (a message can hold several content blocks).
// Lines that are not JSON or carry nothing to show (hooks, rate limits) yield no event.
std::vector<StreamEvent> parseStreamLine(const QByteArray& line);

// One `--input-format stream-json` line carrying a user message, newline included.
QByteArray userMessageLine(const QString& text);

// Short, single-line description of a tool call, e.g. the command of a Bash call.
QString summarizeToolInput(const QString& toolName, const QJsonObject& input);

} // namespace presentation::chat
