#pragma once

#include <QByteArray>
#include <QString>

#include <optional>
#include <vector>

namespace presentation::updates {

// Repository whose pull requests are announced at startup.
inline constexpr const char* kUpstreamRepository = "Goldwaterfung/Stratawright";

struct UpstreamPullRequest {
    int number = 0;
    QString title;
    QString author;
    QString state;  // "open", "merged" or "closed"
    QString url;
};

// Parses a GitHub `GET /repos/{owner}/{repo}/pulls` response. Malformed input yields an empty list.
std::vector<UpstreamPullRequest> parsePullRequests(const QByteArray& json);

// Pull requests worth announcing, newest first: everything numbered above `lastSeenNumber`, or,
// on the very first check (no `lastSeenNumber`), the ones that are still open.
std::vector<UpstreamPullRequest> selectNewPullRequests(const std::vector<UpstreamPullRequest>& pullRequests,
                                                       std::optional<int> lastSeenNumber);

// Highest pull request number in the list, or `fallback` if the list is empty.
int highestPullRequestNumber(const std::vector<UpstreamPullRequest>& pullRequests, int fallback);

} // namespace presentation::updates
