#include <iostream>
#include <project_config.h>
#include "application.h"
#include "launch_options.h"

#ifdef __APPLE__
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#endif

namespace {

// Launched from Finder there is no terminal and everything the layers print is lost. Keep it in
// ~/Library/Logs/Stratawright/stratawright.log (previous run in stratawright.previous.log) so
// problems can be investigated afterwards, by the user or by the in-app assistant.
void redirectOutputToLogFile() {
#ifdef __APPLE__
    if (isatty(STDOUT_FILENO)) {
        return;
    }
    const char* home = std::getenv("HOME");
    if (!home) {
        return;
    }
    const std::filesystem::path directory = std::filesystem::path(home) / "Library" / "Logs" / "Stratawright";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path logFile = directory / "stratawright.log";
    std::filesystem::rename(logFile, directory / "stratawright.previous.log", error);
    if (std::freopen(logFile.c_str(), "w", stdout)) {
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
        dup2(fileno(stdout), STDERR_FILENO);
    }
#endif
}

} // namespace

int main(int argc, char* argv[]) {
    app::setLaunchOptions(app::parseLaunchOptions(argc, argv));
    if (app::launchOptions().smokeTest) {
        // Nothing on screen, and the output stays with whoever runs the test. Files the app drops
        // in its working directory go to a temporary one.
        setenv("QT_QPA_PLATFORM", "offscreen", 1);
        app::LaunchOptions options = app::launchOptions();
        std::error_code error;
        if (!options.smokeProject.empty()) {
            options.smokeProject = std::filesystem::absolute(options.smokeProject, error).string();
            app::setLaunchOptions(options);
        }
        std::filesystem::current_path(std::filesystem::temp_directory_path(error), error);
    } else {
        redirectOutputToLogFile();
    }

    std::cout << config::PROJECT_DISPLAY_NAME << " - Production Grade" << std::endl;
    std::cout << "===================================" << std::endl;
    std::cout << std::endl;

    // Create and initialize application
    app::Application application(argc, argv);

    if (!application.initialize()) {
        std::cerr << "Failed to initialize application" << std::endl;
        return 1;
    }

    // Run main loop
    int exitCode = application.run();

    // Cleanup is handled by Application destructor
    return exitCode;
}
