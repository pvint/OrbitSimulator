# SPDX-License-Identifier: GPL-3.0-or-later
# Physics tests, using Catch2 v3.
#
# FetchContent first looks for an installed Catch2 (FIND_PACKAGE_ARGS). Only if
# none is found does it download the pinned version from GitHub.

include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS 3)
FetchContent_MakeAvailable(Catch2)

add_executable(orbit_core_tests
    test_vec3.cpp
)
target_link_libraries(orbit_core_tests PRIVATE orbit::core Catch2::Catch2WithMain)
orbit_set_warnings(orbit_core_tests)

# Register each TEST_CASE as its own CTest test.
include(Catch)
catch_discover_tests(orbit_core_tests)
