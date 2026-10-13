#include <catch2/catch_test_macros.hpp>

#include "Presentation/shortcuts/ShortcutRegistry.h"

#include <QGuiApplication>

using namespace presentation::shortcuts;

TEST_CASE("Project commands have their usual shortcuts", "[presentation][shortcuts]") {
    qputenv("QT_QPA_PLATFORM", "offscreen");  // key sequences are read through the GUI application
    int argc = 1;
    char name[] = "presentation_unit_tests";
    char* argv[] = {name, nullptr};
    QGuiApplication app(argc, argv);
    const ShortcutRegistry& registry = ShortcutRegistry::instance();
    CHECK(registry.getSequence(ShortcutAction::Global_NewProject) == QKeySequence(QStringLiteral("Ctrl+N")));
    CHECK(registry.getSequence(ShortcutAction::Global_OpenProject) == QKeySequence(QStringLiteral("Ctrl+O")));
    CHECK(registry.getSequence(ShortcutAction::Global_SaveProject) == QKeySequence(QStringLiteral("Ctrl+S")));
    CHECK(registry.getSequence(ShortcutAction::Global_SaveProjectAs) == QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    REQUIRE(registry.getInfo(ShortcutAction::Global_NewProject).has_value());
    CHECK(std::string(registry.getInfo(ShortcutAction::Global_NewProject)->displayName) == "New Project");
}
