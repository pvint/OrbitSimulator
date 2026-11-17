# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)

project(OrbitSimulator
    VERSION 0.1.0
    DESCRIPTION "Rocket launch and orbit simulator for building intuition"
    LANGUAGES CXX)

# C++20, not 23: MSVC and Apple Clang lag behind GCC, and we want all three
# platforms to build from the same code.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Write compile_commands.json for clangd and clang-tidy.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ORBIT_BUILD_APP "Build the Qt desktop app (needs Qt 6.8+)" ON)
option(ORBIT_BUILD_TESTS "Build the physics tests (needs Catch2 3)" ON)
option(ORBIT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

# Standard install locations (bin/, share/doc/, ...). Distribution packaging,
# such as a Gentoo ebuild, relies on these.
include(GNUInstallDirs)

# The physics core comes first and has no Qt dependency. Everything else is
# a client of it.
add_subdirectory(src/core)
add_subdirectory(src/cli)

if(ORBIT_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(ORBIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(FILES LICENSE README.md TYPE DOC)

include(Packaging)
