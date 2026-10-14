# tests/unit/Presentation/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

add_executable(presentation_unit_tests
    upstream_pull_requests_test.cpp
    claude_stream_test.cpp
    conversation_store_test.cpp
    item_view_accessibility_test.cpp
    claude_settings_test.cpp
    mic_recorder_test.cpp
    shortcut_registry_test.cpp
    playlist_menu_test.cpp
    relaunch_watchdog_test.cpp
    "${CMAKE_SOURCE_DIR}/src/Presentation/views/relaunch.cpp"
    "${CMAKE_SOURCE_DIR}/src/Presentation/views/playlist/PlaylistMenuButton.cpp"
    "${CMAKE_SOURCE_DIR}/src/Presentation/views/theme.cpp"
    "${CMAKE_SOURCE_DIR}/src/Presentation/views/updates/upstream_pull_requests.cpp"
    "${CMAKE_SOURCE_DIR}/src/Presentation/views/chat/claude_stream.cpp"
    "${CMAKE_SOURCE_DIR}/src/Presentation/views/chat/conversation_store.cpp"
    "${CMAKE_SOURCE_DIR}/src/Presentation/views/item_view_accessibility.cpp"
    "${CMAKE_SOURCE_DIR}/src/Presentation/views/chat/claude_code_session.cpp"
    "${CMAKE_SOURCE_DIR}/src/Presentation/views/chat/claude_account.cpp"
    "${CMAKE_SOURCE_DIR}/src/Presentation/views/chat/chat_references.cpp"
    "${CMAKE_SOURCE_DIR}/src/Presentation/views/chat/mic_recorder.cpp"
    "${CMAKE_SOURCE_DIR}/src/Presentation/shortcuts/ShortcutRegistry.cpp"
)

target_include_directories(presentation_unit_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/Middle Bridge"
    "${CMAKE_SOURCE_DIR}/tests/unit"
    "${CMAKE_SOURCE_DIR}/src/Presentation/views"
)

target_link_libraries(presentation_unit_tests PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Widgets
    Qt6::Svg
)

# Register tests with CTest
include(Catch)
catch_discover_tests(presentation_unit_tests)
