#include "upstream_pull_request_notifier.h"

#include <QAbstractButton>
#include <QDesktopServices>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QUrl>
#include <QWidget>

#include <iostream>

namespace presentation::updates {

namespace {

const QString kSettingsOrganization = QStringLiteral("Stratawright");
const QString kSettingsApplication = QStringLiteral("Stratawright");
const QString kLastSeenKey = QStringLiteral("upstream/lastSeenPullRequest");

} // namespace

UpstreamPullRequestNotifier::UpstreamPullRequestNotifier(QWidget* window)
    : QObject(window), window_(window) {
    connect(&network_, &QNetworkAccessManager::finished, this, &UpstreamPullRequestNotifier::onReplyFinished);
}

void UpstreamPullRequestNotifier::checkNow() {
    const QString repository = QString::fromLatin1(kUpstreamRepository);
    QNetworkRequest request(QUrl(
        QStringLiteral("https://api.github.com/repos/%1/pulls?state=all&sort=created&direction=desc&per_page=30")
            .arg(repository)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "Stratawright");
    request.setTransferTimeout(10000);
    network_.get(request);
}

void UpstreamPullRequestNotifier::onReplyFinished(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        std::cerr << "UpstreamPullRequestNotifier: could not check upstream pull requests: "
                  << reply->errorString().toStdString() << std::endl;
        return;
    }

    const std::vector<UpstreamPullRequest> pullRequests = parsePullRequests(reply->readAll());
    if (pullRequests.empty()) {
        return;  // Nothing upstream yet, or an API error payload: keep the last seen number as is.
    }

    QSettings settings(kSettingsOrganization, kSettingsApplication);
    std::optional<int> lastSeen;
    if (settings.contains(kLastSeenKey)) {
        lastSeen = settings.value(kLastSeenKey).toInt();
    }
    const std::vector<UpstreamPullRequest> fresh = selectNewPullRequests(pullRequests, lastSeen);
    settings.setValue(kLastSeenKey, highestPullRequestNumber(pullRequests, lastSeen.value_or(0)));

    if (!fresh.empty()) {
        showPopup(fresh);
    }
}

void UpstreamPullRequestNotifier::showPopup(const std::vector<UpstreamPullRequest>& pullRequests) {
    QStringList lines;
    for (const UpstreamPullRequest& pullRequest : pullRequests) {
        lines << QStringLiteral("#%1  %2  (@%3, %4)")
                     .arg(pullRequest.number)
                     .arg(pullRequest.title, pullRequest.author, pullRequest.state);
    }
    const QString repository = QString::fromLatin1(kUpstreamRepository);

    // Shown with show(), not exec(): no nested event loop while the rest of the UI keeps running.
    auto* box = new QMessageBox(window_);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(false);
    box->setIcon(QMessageBox::Information);
    box->setWindowTitle(QStringLiteral("Upstream pull requests"));
    box->setText(QStringLiteral("%1 new pull request(s) on %2")
                     .arg(static_cast<qulonglong>(pullRequests.size()))
                     .arg(repository));
    box->setInformativeText(lines.join(QLatin1Char('\n')));
    QPushButton* openButton = box->addButton(QStringLiteral("Open on GitHub"), QMessageBox::ActionRole);
    box->addButton(QMessageBox::Close);

    const QUrl pullsUrl(QStringLiteral("https://github.com/%1/pulls").arg(repository));
    connect(box, &QMessageBox::buttonClicked, box, [openButton, pullsUrl](QAbstractButton* clicked) {
        if (clicked == openButton) {
            QDesktopServices::openUrl(pullsUrl);
        }
    });
    box->show();
}

} // namespace presentation::updates
