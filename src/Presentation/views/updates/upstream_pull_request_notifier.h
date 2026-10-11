#pragma once

#include "upstream_pull_requests.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

class QNetworkReply;
class QWidget;

namespace presentation::updates {

// Asks GitHub for the pull requests of kUpstreamRepository and, when some were opened since the
// last launch, lists them in a non-blocking popup over `window`. Owned by `window`; network or
// API failures only log.
class UpstreamPullRequestNotifier : public QObject {
    Q_OBJECT

public:
    explicit UpstreamPullRequestNotifier(QWidget* window);

    void checkNow();

private:
    void onReplyFinished(QNetworkReply* reply);
    void showPopup(const std::vector<UpstreamPullRequest>& pullRequests);

    QPointer<QWidget> window_;
    QNetworkAccessManager network_;
};

} // namespace presentation::updates
