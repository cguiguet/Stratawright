# tests/unit/Presentation/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

add_executable(presentation_unit_tests
    upstream_pull_requests_test.cpp
    claude_stream_test.cpp
    "${CMAKE_SOURCE_DIR}/src/Presentation/views/updates/upstream_pull_requests.cpp"
    "${CMAKE_SOURCE_DIR}/src/Presentation/views/chat/claude_stream.cpp"
)

target_include_directories(presentation_unit_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(presentation_unit_tests PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

# Register tests with CTest
include(Catch)
catch_discover_tests(presentation_unit_tests)
