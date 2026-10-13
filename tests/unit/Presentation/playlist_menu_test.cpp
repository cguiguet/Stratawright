#include <catch2/catch_test_macros.hpp>

#include "Presentation/views/playlist/PlaylistMenuButton.h"

#include <QApplication>
#include <QMenuBar>
#include <QStringList>

using presentation::views::PlaylistMenuButton;

namespace {

QStringList titles(const QMenuBar& bar) {
    QStringList names;
    for (QAction* action : bar.actions()) {
        names << action->text();
    }
    return names;
}

QStringList texts(const QMenu& menu) {
    QStringList names;
    for (QAction* action : menu.actions()) {
        if (!action->isSeparator()) {
            names << action->text();
        }
    }
    return names;
}

} // namespace

TEST_CASE("The playlist menu moves to the menu bar", "[presentation][menus]") {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    int argc = 1;
    char name[] = "presentation_unit_tests";
    char* argv[] = {name, nullptr};
    QApplication app(argc, argv);

    QMenuBar bar;
    QMenu* file = bar.addMenu(QStringLiteral("File"));
    file->addAction(QStringLiteral("New Project"));

    auto* button = new PlaylistMenuButton(nullptr);
    button->moveToMenuBar(&bar, file);

    CHECK(titles(bar) == QStringList{"File", "Edit", "Snap", "Select", "Zoom", "Time Marker", "Playhead", "Diagnostics"});
    // The project commands other than Open and Save join File.
    CHECK(texts(*file) == QStringList{"New Project", "Export Audio Mixdown...", "Export Stems...", "Render Queue / Jobs...",
                                      "Export Session to JSON...", "Import Session from JSON..."});
    CHECK(button->isHidden());

    bool zoomedIn = false;
    QObject::connect(button, &PlaylistMenuButton::zoomInRequested, [&] { zoomedIn = true; });
    QMenu* zoom = bar.actions().at(4)->menu();
    REQUIRE(zoom != nullptr);
    zoom->actions().at(0)->trigger();
    CHECK(zoomedIn);

    // A new playlist (new session wiring) replaces them instead of adding more.
    delete button;
    CHECK(titles(bar) == QStringList{"File"});
    CHECK(texts(*file) == QStringList{"New Project"});
}
