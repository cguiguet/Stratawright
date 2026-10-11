#include "upstream_pull_requests.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace presentation::updates {

std::vector<UpstreamPullRequest> parsePullRequests(const QByteArray& json) {
    std::vector<UpstreamPullRequest> pullRequests;
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isArray()) {
        return pullRequests;
    }

    for (const QJsonValue& value : document.array()) {
        const QJsonObject object = value.toObject();
        const int number = object.value(QStringLiteral("number")).toInt();
        if (number <= 0) {
            continue;
        }

        UpstreamPullRequest pullRequest;
        pullRequest.number = number;
        pullRequest.title = object.value(QStringLiteral("title")).toString();
        pullRequest.author = object.value(QStringLiteral("user")).toObject().value(QStringLiteral("login")).toString();
        pullRequest.url = object.value(QStringLiteral("html_url")).toString();
        if (!object.value(QStringLiteral("merged_at")).isNull() && !object.value(QStringLiteral("merged_at")).isUndefined()) {
            pullRequest.state = QStringLiteral("merged");
        } else {
            pullRequest.state = object.value(QStringLiteral("state")).toString();
        }
        pullRequests.push_back(pullRequest);
    }
    return pullRequests;
}

std::vector<UpstreamPullRequest> selectNewPullRequests(const std::vector<UpstreamPullRequest>& pullRequests,
                                                       std::optional<int> lastSeenNumber) {
    std::vector<UpstreamPullRequest> selected;
    for (const UpstreamPullRequest& pullRequest : pullRequests) {
        const bool isNew = lastSeenNumber ? pullRequest.number > *lastSeenNumber
                                          : pullRequest.state == QStringLiteral("open");
        if (isNew) {
            selected.push_back(pullRequest);
        }
    }
    std::sort(selected.begin(), selected.end(),
              [](const UpstreamPullRequest& a, const UpstreamPullRequest& b) { return a.number > b.number; });
    return selected;
}

int highestPullRequestNumber(const std::vector<UpstreamPullRequest>& pullRequests, int fallback) {
    int highest = fallback;
    for (const UpstreamPullRequest& pullRequest : pullRequests) {
        highest = std::max(highest, pullRequest.number);
    }
    return highest;
}

} // namespace presentation::updates
