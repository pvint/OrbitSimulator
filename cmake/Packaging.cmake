# SPDX-License-Identifier: GPL-3.0-or-later
# Packaging with CPack. Build a release, then:
#     cmake --build build/release --target package
#
# Each platform gets its own format: a ZIP on Windows, a disk image on macOS,
# a tarball on Linux (where the AppImage, tools/make_appimage.sh, is the one
# to hand out). An installer for Windows (NSIS) can come later.

set(CPACK_PACKAGE_NAME "OrbitSimulator")
set(CPACK_PACKAGE_VENDOR "OrbitSim")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Orbit Simulator")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")

if(WIN32)
    # The Microsoft C++ runtime DLLs, next to the app, so it runs on machines
    # without the Visual C++ redistributable installed.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    include(InstallRequiredSystemLibraries)
    set(CPACK_GENERATOR ZIP)
elseif(APPLE)
    set(CPACK_GENERATOR DragNDrop)
else()
    set(CPACK_GENERATOR TGZ)
endif()

include(CPack)
