#pragma once

namespace bridge {
class ITrackController;
class ITimelineController;
class IBrowserController;
class IArrangementController;
class IMidiEditorController;
class IAnalysisController;
class IRenderController;
class IProjectLifecycleController;
} // namespace bridge

namespace agentic {

struct BridgeControllers {
    bridge::ITrackController* trackController{nullptr};
    bridge::ITimelineController* timelineController{nullptr};
    bridge::IBrowserController* browserController{nullptr};
    bridge::IArrangementController* arrangementController{nullptr};
    bridge::IMidiEditorController* midiEditorController{nullptr};
    bridge::IAnalysisController* analysisController{nullptr};
    bridge::IRenderController* renderController{nullptr};
    bridge::IProjectLifecycleController* projectLifecycleController{nullptr};
};

} // namespace agentic
