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
    test_astro.cpp
    test_custom_program.cpp
    test_atmosphere.cpp
    test_earth.cpp
    test_heliocentric.cpp
    test_integrator.cpp
    test_flyby.cpp
    test_lost_in_space.cpp
    test_mission_parameters.cpp
    test_natural_body.cpp
    test_moon.cpp
    test_orbit.cpp
    test_scenario.cpp
    test_simulation.cpp
    test_solar_system.cpp
    test_staging.cpp
    test_two_body.cpp
    test_vec3.cpp
)
target_link_libraries(orbit_core_tests PRIVATE orbit::core Catch2::Catch2WithMain)
# Where the bundled example scenarios live (test_scenario.cpp loads them all).
target_compile_definitions(orbit_core_tests PRIVATE
    ORBIT_SCENARIO_DIR="${PROJECT_SOURCE_DIR}/scenarios")
orbit_set_warnings(orbit_core_tests)

# Register each TEST_CASE as its own CTest test.
include(Catch)
catch_discover_tests(orbit_core_tests)

# GUI-level tests of the app's panels (Qt Test).
if(ORBIT_BUILD_APP)
    add_subdirectory(app)
endif()
