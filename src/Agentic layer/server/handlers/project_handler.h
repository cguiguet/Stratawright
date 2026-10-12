#pragma once

#include "../../common/ipc_protocol.h"
#include "../../common/parsed_args.h"

namespace bridge {
class IProjectLifecycleController;
}

namespace agentic {

// `project info` and `project save [--path <file.agdaw>]`. Loading stays in the UI: replacing the
// session from the IPC thread would pull it from under the widgets.
class ProjectHandler {
public:
    static ExecutionResult handleCommand(const ParsedArgs& args, bridge::IProjectLifecycleController* lifecycle = nullptr);
};

} // namespace agentic
