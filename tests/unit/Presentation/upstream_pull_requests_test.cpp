#include <catch2/catch_test_macros.hpp>

#include "Presentation/views/updates/upstream_pull_requests.h"

using namespace presentation::updates;

namespace {

// Trimmed-down GitHub `GET /pulls?state=all` response.
const QByteArray kResponse = R"([
  {"number": 7, "title": "Add reverb", "state": "open", "merged_at": null,
   "html_url": "https://github.com/o/r/pull/7", "user": {"login": "alice"}},
  {"number": 5, "title": "Fix crash", "state": "closed", "merged_at": "2026-10-01T10:00:00Z",
   "html_url": "https://github.com/o/r/pull/5", "user": {"login": "bob"}},
  {"number": 3, "title": "Typo", "state": "closed", "merged_at": null,
   "html_url": "https://github.com/o/r/pull/3", "user": {"login": "carol"}}
])";

} // namespace

TEST_CASE("Upstream pull requests: parsing the GitHub response", "[presentation][updates]") {
    const auto pullRequests = parsePullRequests(kResponse);
    REQUIRE(pullRequests.size() == 3);
    CHECK(pullRequests[0].number == 7);
    CHECK(pullRequests[0].title == QStringLiteral("Add reverb"));
    CHECK(pullRequests[0].author == QStringLiteral("alice"));
    CHECK(pullRequests[0].state == QStringLiteral("open"));
    CHECK(pullRequests[0].url == QStringLiteral("https://github.com/o/r/pull/7"));
    CHECK(pullRequests[1].state == QStringLiteral("merged"));
    CHECK(pullRequests[2].state == QStringLiteral("closed"));

    CHECK(parsePullRequests("not json").empty());
    CHECK(parsePullRequests(R"({"message": "API rate limit exceeded"})").empty());
}

TEST_CASE("Upstream pull requests: choosing what to announce", "[presentation][updates]") {
    const auto pullRequests = parsePullRequests(kResponse);

    SECTION("first check announces only the open ones") {
        const auto selected = selectNewPullRequests(pullRequests, std::nullopt);
        REQUIRE(selected.size() == 1);
        CHECK(selected[0].number == 7);
    }

    SECTION("later checks announce everything newer than the last seen number, newest first") {
        const auto selected = selectNewPullRequests(pullRequests, 4);
        REQUIRE(selected.size() == 2);
        CHECK(selected[0].number == 7);
        CHECK(selected[1].number == 5);
    }

    SECTION("nothing new") {
        CHECK(selectNewPullRequests(pullRequests, 7).empty());
    }

    SECTION("highest number keeps the previous value when nothing was fetched") {
        CHECK(highestPullRequestNumber(pullRequests, 0) == 7);
        CHECK(highestPullRequestNumber({}, 4) == 4);
    }
}
