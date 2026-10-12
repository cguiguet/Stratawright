#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Presentation/views/chat/claude_stream.h"

#include <QJsonArray>
#include <QJsonDocument>

using namespace presentation::chat;

// Lines shaped like real `claude -p --output-format stream-json --verbose` output.
TEST_CASE("Claude stream: session start, tool calls and turn end", "[presentation][chat]") {
    SECTION("init carries the session id and model") {
        const auto events = parseStreamLine(
            R"({"type":"system","subtype":"init","cwd":"/repo","session_id":"abc-123","tools":["Bash"],"model":"claude-sonnet"})");
        REQUIRE(events.size() == 1);
        CHECK(events[0].kind == StreamEvent::Kind::SessionStarted);
        CHECK(events[0].sessionId == QStringLiteral("abc-123"));
        CHECK(events[0].model == QStringLiteral("claude-sonnet"));
    }

    SECTION("an assistant message can mix text and tool calls") {
        const auto events = parseStreamLine(
            R"({"type":"assistant","message":{"role":"assistant","content":[)"
            R"({"type":"text","text":"Adding a drum track."},)"
            R"({"type":"tool_use","id":"toolu_1","name":"Bash","input":{"command":"daw-cli track create --name Drums --type midi --color red\nsecond line","description":"Create track"}}]}})");
        REQUIRE(events.size() == 2);
        CHECK(events[0].kind == StreamEvent::Kind::AssistantText);
        CHECK(events[0].text == QStringLiteral("Adding a drum track."));
        CHECK(events[1].kind == StreamEvent::Kind::ToolUse);
        CHECK(events[1].toolName == QStringLiteral("Bash"));
        CHECK(events[1].text == QStringLiteral("daw-cli track create --name Drums --type midi --color red"));
    }

    SECTION("tool results come back as user messages, as text or as blocks") {
        const auto plain = parseStreamLine(
            R"({"type":"user","message":{"role":"user","content":[{"type":"tool_result","tool_use_id":"toolu_1","content":"id\tname\n3\tDrums","is_error":false}]}})");
        REQUIRE(plain.size() == 1);
        CHECK(plain[0].kind == StreamEvent::Kind::ToolResult);
        CHECK(plain[0].text == QStringLiteral("id\tname"));
        CHECK_FALSE(plain[0].isError);

        const auto blocks = parseStreamLine(
            R"({"type":"user","message":{"role":"user","content":[{"type":"tool_result","tool_use_id":"toolu_2","content":[{"type":"text","text":"ERROR 70 INVALID_ARGS"}],"is_error":true}]}})");
        REQUIRE(blocks.size() == 1);
        CHECK(blocks[0].text == QStringLiteral("ERROR 70 INVALID_ARGS"));
        CHECK(blocks[0].isError);
    }

    SECTION("result ends the turn with its cost") {
        const auto events = parseStreamLine(
            R"({"type":"result","subtype":"success","is_error":false,"result":"Done.","session_id":"abc-123","total_cost_usd":0.0123})");
        REQUIRE(events.size() == 1);
        CHECK(events[0].kind == StreamEvent::Kind::TurnFinished);
        CHECK(events[0].text == QStringLiteral("Done."));
        CHECK(events[0].sessionId == QStringLiteral("abc-123"));
        CHECK_THAT(events[0].costUsd, Catch::Matchers::WithinAbs(0.0123, 1e-9));
    }

    SECTION("hooks, rate limits and garbage are ignored") {
        CHECK(parseStreamLine(R"({"type":"system","subtype":"hook_started","hook_id":"h"})").empty());
        CHECK(parseStreamLine(R"({"type":"rate_limit_event","rate_limit_info":{}})").empty());
        CHECK(parseStreamLine("not json").empty());
    }
}

TEST_CASE("Claude stream: user message line", "[presentation][chat]") {
    const QByteArray line = userMessageLine(QStringLiteral("Ajoute une batterie \"rock\""));
    REQUIRE(line.endsWith('\n'));
    const QJsonObject object = QJsonDocument::fromJson(line.trimmed()).object();
    CHECK(object.value(QStringLiteral("type")).toString() == QStringLiteral("user"));
    const QJsonObject block = object.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toArray().at(0).toObject();
    CHECK(block.value(QStringLiteral("text")).toString() == QStringLiteral("Ajoute une batterie \"rock\""));
}
