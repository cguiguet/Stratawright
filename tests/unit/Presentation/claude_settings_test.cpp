#include <catch2/catch_test_macros.hpp>

#include "Presentation/views/chat/claude_code_session.h"

using namespace presentation::chat;

namespace {

QString valueAfter(const QStringList& arguments, const QString& option) {
    const qsizetype index = arguments.indexOf(option);
    return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : QString();
}

} // namespace

TEST_CASE("Claude Code runs with the chat settings", "[presentation][chat]") {
    const QStringList defaults = ClaudeCodeSession::arguments({}, QStringLiteral("prompt"), QString());
    CHECK(valueAfter(defaults, QStringLiteral("--permission-mode")) == QStringLiteral("acceptEdits"));
    CHECK(valueAfter(defaults, QStringLiteral("--allowedTools")).split(QLatin1Char(',')).contains(QStringLiteral("Bash")));
    CHECK_FALSE(defaults.contains(QStringLiteral("--model")));
    CHECK_FALSE(defaults.contains(QStringLiteral("--effort")));
    CHECK_FALSE(defaults.contains(QStringLiteral("--resume")));
    CHECK(valueAfter(defaults, QStringLiteral("--append-system-prompt")) == QStringLiteral("prompt"));

    ClaudeCodeSession::Settings settings;
    settings.model = QStringLiteral("opus");
    settings.effort = QStringLiteral("high");
    const QStringList chosen = ClaudeCodeSession::arguments(settings, QString(), QStringLiteral("session-1"));
    CHECK(valueAfter(chosen, QStringLiteral("--model")) == QStringLiteral("opus"));
    CHECK(valueAfter(chosen, QStringLiteral("--effort")) == QStringLiteral("high"));
    CHECK(valueAfter(chosen, QStringLiteral("--resume")) == QStringLiteral("session-1"));

    settings.readOnly = true;
    const QStringList readOnly = ClaudeCodeSession::arguments(settings, QString(), QString());
    CHECK(valueAfter(readOnly, QStringLiteral("--permission-mode")) == QStringLiteral("plan"));
    const QStringList tools = valueAfter(readOnly, QStringLiteral("--allowedTools")).split(QLatin1Char(','));
    CHECK(tools.contains(QStringLiteral("Read")));
    CHECK_FALSE(tools.contains(QStringLiteral("Bash")));
    CHECK_FALSE(tools.contains(QStringLiteral("Edit")));
    CHECK_FALSE(tools.contains(QStringLiteral("Write")));
}
