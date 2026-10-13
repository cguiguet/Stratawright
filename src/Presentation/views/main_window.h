// src/Presentation/views/main_window.h
#pragma once

#include <QMainWindow>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QSplitter>
#include <vector>
#include <memory>

#include "playlist/PlaylistWindow.h"
#include "Presentation/controllers/presentation_director.h"

namespace bridge {
class IMeteringProvider;
class IBrowserController;
class ITimelineController;
class IInputModeController;
class IWorkspaceController;
class IHardwareSettingsFacade;
class IMidiEditorController;
class IProjectLifecycleController;
}

namespace presentation::chat {
class ChatPanel;
}

namespace presentation::views {
class TopControlPanel;
class MixerWindow;
}

namespace presentation::views {

/**
 * @brief Premium MainWindow shell representing the visual interface of our C++20 DAW.
 * Houses multiple custom vertical channel strips (Dials, Faders, Meters) inside a dark-glass layout.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /**
     * @brief Configures and connects the Middle Bridge metering interface.
     */
    void setMeteringProvider(bridge::IMeteringProvider* provider);

    /**
     * @brief Configures and connects the Browser controller interface.
     */
    void setBrowserController(bridge::IBrowserController* controller);

    /**
     * @brief Configures and connects the top control panel controllers.
     */
    void setTopPanelControllers(bridge::ITimelineController* timelineCtrl,
                                bridge::IInputModeController* inputCtrl,
                                bridge::IWorkspaceController* workspaceCtrl);

    /**
     * @brief Instantiates and embeds the Playlist / Arrangement panel.
     */
    void setPlaylistControllers(const PlaylistWindow::Controllers& playlistCtrls);

    /**
     * @brief Configures and connects the Hardware Settings Facade.
     */
    void setHardwareSettingsFacade(bridge::IHardwareSettingsFacade* facade);

    /**
     * @brief Configures and connects the MIDI Editor controller.
     */
    void setMidiEditorController(bridge::IMidiEditorController* controller);

protected:
    void paintEvent(QPaintEvent* event) override;

private Q_SLOTS:
    /**
     * @brief Dynamically synchronizes MixerWindow channel strips with playlist track state.
     */
    void syncMixerStrips();

    /**
     * @brief Opens the custom premium Hardware Settings Dialog modal.
     */
    void showSettingsDialog();
    // File menu (New, Open, Save, Save As) in the menu bar, with their shortcuts.
    void buildFileMenu();

    /**
     * @brief Handles double clicks on MIDI clips from the Playlist.
     */
    void onMidiClipDoubleClicked(TrackID trackId, bridge::RegionID regionId);

    /**
     * @brief Handles window toggles clicked from the top control panel.
     */
    void onWindowToggleClicked(bridge::WorkspaceWindow window);
    void toggleWindowVisibility(bridge::WorkspaceWindow window);

private:
    class BrowserWidget*  m_browserWidget{nullptr};
    class QFrame*         m_browserDivider{nullptr};
    class PlaylistWindow* m_playlistWindow{nullptr};
    class PianoRollWindow* m_pianoRollWindow{nullptr};
    class QSplitter*      m_mainSplitter{nullptr};
    class MixerWindow*    m_mixerWindow{nullptr};
    class QDockWidget*    m_chatDock{nullptr};
    chat::ChatPanel*      m_chatPanel{nullptr};
    TopControlPanel*      m_topControlPanel{nullptr};

    // --- GUI Loop Director ---
    controllers::PresentationDirector m_director;

    bridge::IHardwareSettingsFacade* m_hardwareSettingsFacade{nullptr};
    QMenu* m_fileMenu{nullptr};  // File menu of the menu bar
    bridge::ITimelineController* m_timelineController{nullptr};
    bridge::IProjectLifecycleController* m_lifecycleController{nullptr};
    bridge::IWorkspaceController* m_workspaceController{nullptr};
    bridge::IArrangementController* m_arrangementController{nullptr};
};

} // namespace presentation::views
