# SPDX-License-Identifier: GPL-3.0-or-later
# Packaging with CPack. Build a release, then:
#     cmake --build build/release --target package
#
# Each platform gets its own format. Installers (NSIS on Windows, AppImage on
# Linux) can come later; these simple archives need no extra tools.

set(CPACK_PACKAGE_NAME "OrbitSimulator")
set(CPACK_PACKAGE_VENDOR "OrbitSim")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Orbit Simulator")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")

if(WIN32)
    set(CPACK_GENERATOR ZIP)
elseif(APPLE)
    set(CPACK_GENERATOR DragNDrop)
else()
    set(CPACK_GENERATOR TGZ)
endif()

include(CPack)
