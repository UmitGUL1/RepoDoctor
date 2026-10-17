cmake_minimum_required(VERSION 3.20)

project(
    RepoDoctor
    VERSION 1.0.0
    DESCRIPTION "RepoDoctor — Professional Open-Source Repository Health Analyzer"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(REPODOCTOR_BUILD_CLI "Build the repodoctor-cli executable" ON)
option(REPODOCTOR_BUILD_GUI "Build the RepoDoctor Qt6 / QML desktop application" ON)
option(REPODOCTOR_BUILD_TESTS "Build the GoogleTest unit & integration test suite" ON)
option(REPODOCTOR_BUILD_BENCHMARKS "Build the repository performance benchmark suite" ON)
option(REPODOCTOR_USE_LIBGIT2 "Enable libgit2 integration when available" ON)
option(REPODOCTOR_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)
include(Dependencies)
find_package(Threads REQUIRED)

add_library(repodoctor_options INTERFACE)
repodoctor_set_project_warnings(repodoctor_options)
repodoctor_enable_sanitizers(repodoctor_options)

# ==============================================================================
# Core Scanning & Diagnostics Engine (Strictly Independent of Qt)
# ==============================================================================
set(REPODOCTOR_CORE_SOURCES
    src/core/types.cpp
    src/config/configuration_manager.cpp
    src/sources/repository_sources.cpp
    src/inspectors/git_repository_inspector.cpp
    src/inspectors/dependency_inspector.cpp
    src/security/secret_detection_engine.cpp
    src/engine/scan_context.cpp
    src/rules/irule.cpp
    src/rules/rule_registry.cpp
    src/rules/documentation_rules.cpp
    src/rules/git_hygiene_rules.cpp
    src/rules/security_rules.cpp
    src/rules/build_ci_rules.cpp
    src/rules/dependency_rules.cpp
    src/scoring/risk_scorer.cpp
    src/engine/scan_engine.cpp
    src/reports/report_generator.cpp
    src/fixes/fix_subsystem.cpp
)

add_library(repodoctor_core STATIC ${REPODOCTOR_CORE_SOURCES})
target_include_directories(
    repodoctor_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(
    repodoctor_core
    PUBLIC
        nlohmann_json::nlohmann_json
        Threads::Threads
    PRIVATE
        repodoctor_options
)

if(REPODOCTOR_USE_LIBGIT2 AND LIBGIT2_FOUND)
    target_compile_definitions(repodoctor_core PUBLIC REPODOCTOR_USE_LIBGIT2=1)
    target_link_libraries(repodoctor_core PUBLIC PkgConfig::LIBGIT2)
endif()

# ==============================================================================
# Command-Line Application (repodoctor-cli)
# ==============================================================================
if(REPODOCTOR_BUILD_CLI)
    add_executable(repodoctor-cli apps/cli/main.cpp)
    if(WIN32)
        target_link_libraries(repodoctor-cli PRIVATE shell32)
    endif()
    target_link_libraries(
        repodoctor-cli
        PRIVATE
            repodoctor_core
            repodoctor_options
    )
endif()

# ==============================================================================
# Qt 6 / QML Desktop Application (RepoDoctor)
# ==============================================================================
if(REPODOCTOR_BUILD_GUI AND Qt6_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/gui/main.cpp")
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)

    add_executable(
        RepoDoctor
        apps/gui/main.cpp
        apps/gui/backend_controller.hpp
        apps/gui/backend_controller.cpp
        ui/resources.qrc
    )
    target_include_directories(RepoDoctor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/apps/gui")
    target_link_libraries(
        RepoDoctor
        PRIVATE
            repodoctor_core
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Widgets
    )
endif()

# ==============================================================================
# GoogleTest Automated Test Suite
# ==============================================================================
if(REPODOCTOR_BUILD_TESTS AND GTest_FOUND)
    enable_testing()
    add_executable(repodoctor_tests tests/repodoctor_tests.cpp)
    add_executable(repodoctor_fake_git tests/fake_git.cpp)
    if(WIN32)
        target_link_libraries(repodoctor_fake_git PRIVATE shell32)
    endif()
    set_target_properties(repodoctor_fake_git PROPERTIES
        OUTPUT_NAME git
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-tools")
    add_dependencies(repodoctor_tests repodoctor_fake_git)
    target_compile_definitions(repodoctor_tests PRIVATE
        REPODOCTOR_FAKE_GIT_DIR="$<TARGET_FILE_DIR:repodoctor_fake_git>")
    target_link_libraries(
        repodoctor_tests
        PRIVATE
            repodoctor_core
            repodoctor_options
            GTest::gtest
            GTest::gtest_main
    )
    add_test(NAME RepoDoctorTests COMMAND repodoctor_tests)
    if(TARGET RepoDoctor)
        add_executable(repodoctor_gui_tests tests/gui_tests.cpp
            apps/gui/backend_controller.cpp apps/gui/backend_controller.hpp)
        target_include_directories(repodoctor_gui_tests PRIVATE apps/gui)
        target_link_libraries(repodoctor_gui_tests PRIVATE repodoctor_core GTest::gtest
            Qt6::Core Qt6::Gui Qt6::Widgets)
        add_dependencies(repodoctor_gui_tests repodoctor_fake_git)
        target_compile_definitions(repodoctor_gui_tests PRIVATE
            REPODOCTOR_FAKE_GIT_DIR="$<TARGET_FILE_DIR:repodoctor_fake_git>")
        add_test(NAME RepoDoctorGuiTests COMMAND repodoctor_gui_tests)
    endif()
endif()

# ==============================================================================
# Performance Benchmark Suite
# ==============================================================================
if(REPODOCTOR_BUILD_BENCHMARKS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/repodoctor_benchmarks.cpp")
    add_executable(repodoctor_benchmarks benchmarks/repodoctor_benchmarks.cpp)
    target_link_libraries(
        repodoctor_benchmarks
        PRIVATE
            repodoctor_core
            repodoctor_options
    )
endif()
