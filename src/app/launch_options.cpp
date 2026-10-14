#include "launch_options.h"

#include <cstring>

namespace app {

namespace {
LaunchOptions gOptions;
}

LaunchOptions parseLaunchOptions(int argc, char* argv[]) {
    LaunchOptions options;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) {
            options.smokeTest = true;
        } else if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc) {
            options.smokeProject = argv[++i];
        } else if (std::strcmp(argv[i], "--recovered") == 0) {
            options.recovered = true;
        }
    }
    return options;
}

const LaunchOptions& launchOptions() {
    return gOptions;
}

void setLaunchOptions(const LaunchOptions& options) {
    gOptions = options;
}

} // namespace app
