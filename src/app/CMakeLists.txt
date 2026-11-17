# SPDX-License-Identifier: GPL-3.0-or-later
# orbitsim: the Qt desktop app.
#
# The window, panels and forms are Qt Widgets. The 3D view and the plots are
# Qt Quick scenes written in QML, embedded with QQuickWidget. Simulations run
# on a worker thread via Qt Concurrent.

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Quick QuickWidgets Quick3D Graphs Concurrent)

# Turns on AUTOMOC etc. and opts in to Qt's current CMake policies.
qt_standard_project_setup(REQUIRES 6.8)

# orbitsim_ui: the panels and the simulation controller, i.e. everything that
# doesn't need QML. A separate library so tests/app can test it too.
qt_add_library(orbitsim_ui STATIC
    display_format.cpp
    display_format.hpp
    flight_stats_panel.cpp
    flight_stats_panel.hpp
    parameter_panel.cpp
    parameter_panel.hpp
    run_set.cpp
    run_set.hpp
    simulation_controller.cpp
    simulation_controller.hpp
    sweep_panel.cpp
    sweep_panel.hpp
)
target_include_directories(orbitsim_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(orbitsim_ui PUBLIC orbit::core Qt6::Widgets Qt6::Concurrent)
orbit_set_warnings(orbitsim_ui)

qt_add_executable(orbitsim
    demo_controller.cpp
    demo_controller.hpp
    globe_geometry.cpp
    globe_geometry.hpp
    globe_view.cpp
    globe_view.hpp
    main.cpp
    main_window.cpp
    main_window.hpp
    plot_panel.cpp
    plot_panel.hpp
    polyline_geometry.cpp
    polyline_geometry.hpp
    scene_coordinates.hpp
)

# QML files are compiled into the executable as resources, under the module
# URI "OrbitSim" (loaded from qrc:/qt/qml/OrbitSim/...).
qt_add_qml_module(orbitsim
    URI OrbitSim
    VERSION 1.0
    QML_FILES
        qml/AxisArrow.qml
        qml/GlobeScene.qml
        qml/Plots.qml
        qml/TimeGraph.qml
)

# The example scenarios from scenarios/, compiled in for File ▸ Examples
# (qrc:/scenarios/...).
file(GLOB example_scenarios CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/scenarios/*.json")
qt_add_resources(orbitsim "example_scenarios"
    PREFIX "/scenarios"
    BASE "${PROJECT_SOURCE_DIR}/scenarios"
    FILES ${example_scenarios}
)

# The demo tour (Help ▸ Demo Tour), qrc:/demos/tour.json.
qt_add_resources(orbitsim "demos"
    PREFIX "/demos"
    BASE "${PROJECT_SOURCE_DIR}/demos"
    FILES "${PROJECT_SOURCE_DIR}/demos/tour.json"
)

# Earth texture, compiled into the executable (qrc:/textures/...). BIG_RESOURCES
# embeds it as object code instead of a huge generated C++ array.
# Source and how it was made: tools/make_earth_texture.sh.
# The Moon's colour map is optional: the Moon is drawn grey without it. It
# isn't in the repository until it can be downloaded with a verified
# connection; see tools/make_moon_texture.sh.
set(orbitsim_textures assets/textures/earth_day.jpg)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/moon.jpg")
    list(APPEND orbitsim_textures assets/textures/moon.jpg)
endif()
# JPEGs are already compressed, so rcc shouldn't try again (with zstd it can
# also trip a linker restriction on some distributions' Qt builds).
qt_add_resources(orbitsim "textures"
    PREFIX "/"
    BASE assets
    BIG_RESOURCES
    OPTIONS -no-compress
    FILES ${orbitsim_textures}
)

target_link_libraries(orbitsim PRIVATE
    orbitsim_ui
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::Quick3D
    Qt6::Graphs)

orbit_set_warnings(orbitsim)

# A proper GUI app on each platform: no console window on Windows, an .app
# bundle on macOS.
set_target_properties(orbitsim PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "Orbit Simulator"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.orbitsim"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

install(TARGETS orbitsim
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Bundling Qt: copy the Qt libraries, plugins and QML modules the app needs
# next to it on install, so the packaged app runs without Qt installed.
#
# Off by default on Linux. There, Qt usually comes from the distribution, whose
# plugin folders also hold third-party plugins (KDE's, for example) that Qt's
# deploy step cannot relocate. Distributable Linux builds should use the
# official Qt from qt.io (planned for CI, as an AppImage).
if(WIN32 OR APPLE)
    set(_orbit_deploy_default ON)
else()
    set(_orbit_deploy_default OFF)
endif()
option(ORBIT_DEPLOY_QT "Bundle the Qt runtime when installing/packaging" ${_orbit_deploy_default})

if(ORBIT_DEPLOY_QT)
    qt_generate_deploy_qml_app_script(
        TARGET orbitsim
        OUTPUT_SCRIPT orbitsim_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${orbitsim_deploy_script})
endif()
