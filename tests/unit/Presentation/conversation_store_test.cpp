#include <catch2/catch_test_macros.hpp>

#include "Presentation/views/chat/conversation_store.h"

#include <QTemporaryDir>

using namespace presentation::chat;

TEST_CASE("Conversation store: one conversation per project", "[presentation][chat]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const ConversationStore store(directory.path());

    const QString song = QStringLiteral("/Users/me/Music/song.agdaw");
    CHECK_FALSE(store.contains(song));
    const Conversation fresh = store.load(song);
    CHECK(fresh.projectPath == song);
    CHECK(fresh.sessionId.isEmpty());

    store.save({song, QStringLiteral("session-1"), QStringLiteral("<p>hello</p>")});
    store.save({QString(), QStringLiteral("session-untitled"), QStringLiteral("<p>draft</p>")});
    REQUIRE(store.contains(song));
    REQUIRE(store.contains(QString()));

    const Conversation loaded = store.load(song);
    CHECK(loaded.sessionId == QStringLiteral("session-1"));
    CHECK(loaded.transcriptHtml == QStringLiteral("<p>hello</p>"));
    CHECK(store.load(QString()).sessionId == QStringLiteral("session-untitled"));
    CHECK(store.load(QStringLiteral("/Users/me/Music/other.agdaw")).sessionId.isEmpty());

    store.remove(QString());
    CHECK_FALSE(store.contains(QString()));
    CHECK(store.contains(song));
}
