cmake_minimum_required(VERSION 3.21.1)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Oldest supported macOS")
endif()
project(Orbite VERSION 0.8.0 LANGUAGES CXX)
set(ORBITE_VERSION "${PROJECT_VERSION}-beta.1")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Concurrent Sql)

add_library(orbite_core STATIC
    src/common.cpp src/profile.cpp src/workspace.cpp src/attachments.cpp
    src/rpc.cpp src/acp.cpp src/computer_use.cpp)
target_include_directories(orbite_core PUBLIC src)
target_link_libraries(orbite_core PUBLIC Qt6::Core Qt6::Sql)
target_compile_definitions(orbite_core PRIVATE ORBITE_VERSION="${ORBITE_VERSION}")

add_library(orbite_widgets STATIC src/controller.cpp src/window.cpp src/review_panel.cpp src/theme.cpp src/workbench.cpp)
target_link_libraries(orbite_widgets PUBLIC orbite_core Qt6::Widgets Qt6::Concurrent)

qt_add_executable(Orbite WIN32 MACOSX_BUNDLE src/main.cpp)
qt_add_resources(Orbite "orbite_brand" PREFIX "/" FILES assets/orbite-mark.png)
target_link_libraries(Orbite PRIVATE orbite_widgets)
target_compile_definitions(Orbite PRIVATE ORBITE_VERSION="${ORBITE_VERSION}")
set_target_properties(Orbite PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME Orbite
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.rseam07.orbite
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE Orbite.icns)
if(APPLE)
    set(version_file "${CMAKE_CURRENT_BINARY_DIR}/release-version.txt")
    file(GENERATE OUTPUT "${version_file}" CONTENT "${ORBITE_VERSION}\n")
    target_sources(Orbite PRIVATE assets/Orbite.icns "${version_file}")
    set_source_files_properties(assets/Orbite.icns "${version_file}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()
install(TARGETS Orbite BUNDLE DESTINATION . RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    add_executable(orbite_fixture tests/acp_fixture.cpp)
    target_link_libraries(orbite_fixture PRIVATE Qt6::Core)
    add_executable(orbite_tests tests/core_tests.cpp)
    target_link_libraries(orbite_tests PRIVATE orbite_core Qt6::Test)
    add_dependencies(orbite_tests orbite_fixture)
    add_test(NAME orbite_core COMMAND orbite_tests)
    add_executable(orbite_desktop_tests tests/desktop_tests.cpp)
    qt_add_resources(orbite_desktop_tests "orbite_test_brand" PREFIX "/" FILES assets/orbite-mark.png)
    target_link_libraries(orbite_desktop_tests PRIVATE orbite_widgets Qt6::Test)
    add_dependencies(orbite_desktop_tests orbite_fixture)
    add_test(NAME orbite_desktop COMMAND orbite_desktop_tests)
    set_tests_properties(orbite_core orbite_desktop PROPERTIES TIMEOUT 45)
    set_tests_properties(orbite_desktop PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM=1")
endif()
