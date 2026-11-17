# SPDX-License-Identifier: GPL-3.0-or-later
# orbitsim: the Qt desktop app.
#
# The window, panels and forms are Qt Widgets. The 3D view (and, from M2, the
# plots) are Qt Quick scenes written in QML, embedded with QQuickWidget.

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Quick QuickWidgets Quick3D)

# Turns on AUTOMOC etc. and opts in to Qt's current CMake policies.
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(orbitsim
    main.cpp
    main_window.cpp
    main_window.hpp
)

# QML files are compiled into the executable as resources, under the module
# URI "OrbitSim" (loaded from qrc:/qt/qml/OrbitSim/...).
qt_add_qml_module(orbitsim
    URI OrbitSim
    VERSION 1.0
    QML_FILES
        qml/SceneView.qml
)

target_link_libraries(orbitsim PRIVATE
    orbit::core
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::Quick3D)

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
