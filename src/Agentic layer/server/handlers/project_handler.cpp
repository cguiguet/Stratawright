#include "project_handler.h"
#include "Middle Bridge/project/iproject_lifecycle_controller.h"
#include <string>

namespace agentic {

ExecutionResult ProjectHandler::handleCommand(const ParsedArgs& args, bridge::IProjectLifecycleController* lifecycle) {
    if (!lifecycle) {
        return ExecutionResult::Error(ErrorCode::DAW_NOT_RUNNING, "Project controller unavailable.");
    }
    std::string_view sub = args.getSubcommand();

    if (sub == "info") {
        const std::string path = lifecycle->getCurrentProjectPath();
        return ExecutionResult::Success("PROJECT_INFO", {
            {"path", path.empty() ? std::string("(never saved)") : path},
            {"unsaved_changes", lifecycle->isProjectDirty() ? "true" : "false"}
        });
    }

    if (sub == "save") {
        std::string path(args.getOption("--path"));
        if (path.empty()) {
            path = lifecycle->getCurrentProjectPath();
        }
        if (path.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS,
                "This project has never been saved: pass --path <file.agdaw>.");
        }
        if (!lifecycle->saveProject(path.c_str())) {
            return ExecutionResult::Error(ErrorCode::ASSET_I_O_ERROR, "Could not save the project to " + path + ".");
        }
        return ExecutionResult::Success("PROJECT_SAVED", {{"path", path}});
    }

    return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Unknown project subcommand '" + std::string(sub) + "' (use info or save).");
}

} // namespace agentic
