// src/Presentation/views/playlist/PlaylistMenuButton.cpp
#include "PlaylistMenuButton.h"
#include <QMessageBox>
#include <QDebug>
#include "theme.h"
#include <QActionGroup>
#include <QMenuBar>
#include "../../shortcuts/ShortcutRegistry.h"

namespace presentation::views {

PlaylistMenuButton::PlaylistMenuButton(
    bridge::ITimelineController*  timeline,
    bridge::IWorkspaceController* workspace,
    QWidget* parent)
    : QPushButton(parent)
    , m_timeline(timeline)
    , m_workspace(workspace)
{
    setObjectName("playlistMenuBtn");
    setIcon(theme::PaintHelper::createSvgIcon(":/icons/playlist-menu.svg", QSize(20, 20)));
    setFixedWidth(32);
    setFixedHeight(32);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet(
        "QPushButton#playlistMenuBtn::menu-indicator { image: none; }"
    );

    m_menu = new QMenu(this);
    buildMenu();
    setMenu(m_menu);
}

void PlaylistMenuButton::buildMenu()
{
    m_menu->clear();
    QMenu* projectMenu = m_menu->addMenu(QStringLiteral("Project"));
    buildMenus(projectMenu, true, [this](const QString& title) { return m_menu->addMenu(title); });
}

void PlaylistMenuButton::moveToMenuBar(QMenuBar* bar, QMenu* fileMenu)
{
    setMenu(nullptr);
    m_menu->clear();
    hide();
    // Owned by this button, so they leave the menu bar with the playlist that created them.
    buildMenus(fileMenu, false, [this, bar](const QString& title) {
        auto* menu = new QMenu(title, this);
        bar->addMenu(menu);
        return menu;
    });
}

void PlaylistMenuButton::buildMenus(QMenu* projectMenu, bool withOpenSave,
                                    const std::function<QMenu*(const QString&)>& addMenu)
{
    // Actions belong to this button whatever menu shows them (the File menu outlives it).
    const auto add = [this](QMenu* menu, const QString& text, const QString& id) {
        auto* action = new QAction(text, this);
        action->setData(id);
        menu->addAction(action);
        connect(action, &QAction::triggered, this, [this, action]() { onMenuTriggered(action); });
        return action;
    };
    const auto separator = [this](QMenu* menu) {
        auto* action = new QAction(this);
        action->setSeparator(true);
        menu->addAction(action);
    };

    // --- 1. Project ---
    if (withOpenSave) {
        add(projectMenu, QStringLiteral("Open Session..."), QStringLiteral("open_project"));
        add(projectMenu, QStringLiteral("Save Session"), QStringLiteral("save_project"));
        add(projectMenu, QStringLiteral("Save Session As..."), QStringLiteral("save_project_as"));
    }
    separator(projectMenu);
    add(projectMenu, QStringLiteral("Export Audio Mixdown..."), QStringLiteral("export_audio"))
        ->setShortcut(shortcuts::ShortcutRegistry::instance().getSequence(shortcuts::ShortcutAction::Playlist_OpenExportDialog));
    add(projectMenu, QStringLiteral("Export Stems..."), QStringLiteral("export_stems"))
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    add(projectMenu, QStringLiteral("Render Queue / Jobs..."), QStringLiteral("render_queue"));
    separator(projectMenu);
    add(projectMenu, QStringLiteral("Export Session to JSON..."), QStringLiteral("export_project_json"));
    add(projectMenu, QStringLiteral("Import Session from JSON..."), QStringLiteral("import_project_json"));

    // --- 2. Edit ---
    QMenu* editMenu = addMenu(QStringLiteral("Edit"));
    add(editMenu, QStringLiteral("Undo"), QStringLiteral("undo"));
    add(editMenu, QStringLiteral("Redo"), QStringLiteral("redo"));

    // --- 3. Snap ---
    QMenu* snapMenu = addMenu(QStringLiteral("Snap"));
    struct SnapItem {
        QString label;
        bridge::SnapMode mode;
    };
    const SnapItem snapItems[] = {
        { QStringLiteral("Free (No Snap)"), bridge::SnapMode::Free },
        { QStringLiteral("Bar"), bridge::SnapMode::Bar },
        { QStringLiteral("1/2 Note"), bridge::SnapMode::Note_1_2 },
        { QStringLiteral("1/4 Note"), bridge::SnapMode::Note_1_4 },
        { QStringLiteral("1/8 Note"), bridge::SnapMode::Note_1_8 },
        { QStringLiteral("1/16 Note"), bridge::SnapMode::Note_1_16 },
        { QStringLiteral("1/32 Note"), bridge::SnapMode::Note_1_32 },
        { QStringLiteral("1/64 Note"), bridge::SnapMode::Note_1_64 },
        { QStringLiteral("1/2T Note"), bridge::SnapMode::Note_1_2_Triplet },
        { QStringLiteral("1/4T Note"), bridge::SnapMode::Note_1_4_Triplet },
        { QStringLiteral("1/8T Note"), bridge::SnapMode::Note_1_8_Triplet },
        { QStringLiteral("1/16T Note"), bridge::SnapMode::Note_1_16_Triplet },
        { QStringLiteral("1/32T Note"), bridge::SnapMode::Note_1_32_Triplet },
        { QStringLiteral("1/64T Note"), bridge::SnapMode::Note_1_64_Triplet }
    };
    for (const auto& item : snapItems) {
        add(snapMenu, item.label, QStringLiteral("snap_%1").arg(static_cast<int>(item.mode)));
    }

    // --- 4. Select ---
    QMenu* selectMenu = addMenu(QStringLiteral("Select"));
    add(selectMenu, QStringLiteral("Select All"), QStringLiteral("select_all"));
    add(selectMenu, QStringLiteral("Deselect All"), QStringLiteral("deselect_all"));

    // --- 5. Zoom ---
    QMenu* zoomMenu = addMenu(QStringLiteral("Zoom"));
    add(zoomMenu, QStringLiteral("Zoom In"), QStringLiteral("zoom_in"));
    add(zoomMenu, QStringLiteral("Zoom Out"), QStringLiteral("zoom_out"));
    add(zoomMenu, QStringLiteral("Reset Zoom"), QStringLiteral("zoom_reset"));

    // --- 6. Time Marker ---
    QMenu* markerMenu = addMenu(QStringLiteral("Time Marker"));
    add(markerMenu, QStringLiteral("Add Song Marker"), QStringLiteral("add_marker"));

    // --- 7. Playhead ---
    QMenu* playheadMenu = addMenu(QStringLiteral("Playhead"));
    add(playheadMenu, QStringLiteral("Seek to Song Start"), QStringLiteral("seek_start"));
    add(playheadMenu, QStringLiteral("Seek to Loop Start"), QStringLiteral("seek_loop"));

    // --- 8. Diagnostics ---
    QMenu* diagnosticsMenu = addMenu(QStringLiteral("Diagnostics"));
    add(diagnosticsMenu, QStringLiteral("Analyze Session Loudness"), QStringLiteral("analyze_loudness"));
}

void PlaylistMenuButton::onMenuTriggered(QAction* action)
{
    if (!action) return;
    const QString id = action->data().toString();

    if (id == QStringLiteral("open_project")) {
        emit openProjectRequested();
    } else if (id == QStringLiteral("save_project")) {
        emit saveProjectRequested();
    } else if (id == QStringLiteral("save_project_as")) {
        emit saveProjectAsRequested();
    } else if (id == QStringLiteral("export_audio")) {
        emit exportAudioRequested();
    } else if (id == QStringLiteral("export_stems")) {
        emit exportStemsRequested();
    } else if (id == QStringLiteral("render_queue")) {
        emit renderQueueRequested();
    } else if (id == QStringLiteral("export_project_json")) {
        emit exportProjectJsonRequested();
    } else if (id == QStringLiteral("import_project_json")) {
        emit importProjectJsonRequested();
    } else if (id == QStringLiteral("undo")) {
        emit undoRequested();
    } else if (id == QStringLiteral("redo")) {
        emit redoRequested();

    } else if (id.startsWith(QStringLiteral("snap_"))) {
        int snapId = id.mid(5).toInt();
        emit snapSelected(static_cast<bridge::SnapMode>(snapId));
    } else if (id == QStringLiteral("select_all")) {
        emit selectAllRequested();
    } else if (id == QStringLiteral("deselect_all")) {
        emit deselectAllRequested();
    } else if (id == QStringLiteral("zoom_in")) {
        emit zoomInRequested();
    } else if (id == QStringLiteral("zoom_out")) {
        emit zoomOutRequested();
    } else if (id == QStringLiteral("zoom_reset")) {
        emit zoomResetRequested();
    } else if (id == QStringLiteral("add_marker")) {
        emit addMarkerRequested();
    } else if (id == QStringLiteral("seek_start")) {
        if (m_timeline) m_timeline->seekToFrame(0);
    } else if (id == QStringLiteral("seek_loop")) {
        if (m_timeline) {
            uint64_t loopStart = m_timeline->getLoopStart();
            m_timeline->seekToFrame(loopStart);
        }
    } else if (id == QStringLiteral("analyze_loudness")) {
        emit analyzeLoudnessRequested();
    }
}

} // namespace presentation::views
