#pragma once

#include <string>

namespace app {

// How the app was started, from its command line.
struct LaunchOptions {
    // --smoke-test [--project <file>]: start without showing anything (offscreen), without opening
    // an audio device, the IPC socket or the log file, load the project if given, run for a few
    // seconds, then quit and exit 0. A crash or a failed start exits otherwise. Lets a rebuilt app
    // be checked before relaunching into it, while the user's app keeps running.
    bool smokeTest = false;
    std::string smokeProject;
    // --recovered: the relaunch watchdog reopened this, the last build that worked, because the
    // rebuilt one did not start.
    bool recovered = false;
};

LaunchOptions parseLaunchOptions(int argc, char* argv[]);
const LaunchOptions& launchOptions();
void setLaunchOptions(const LaunchOptions& options);

} // namespace app
