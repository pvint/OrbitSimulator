# SPDX-License-Identifier: GPL-3.0-or-later
# orbit_core: the physics library.
#
# Rule: nothing in here may depend on Qt. Plain C++20, the standard library,
# and nlohmann/json (header-only, for scenario files) only, so the physics can
# be tested, run from the command line, and reused by any front end.

# JSON for scenario files: the installed nlohmann_json if there is one,
# otherwise a pinned download. Used only inside scenario.cpp.
include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    FIND_PACKAGE_ARGS 3.11)
FetchContent_MakeAvailable(nlohmann_json)

configure_file(version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/orbit/core/version.hpp" @ONLY)

add_library(orbit_core STATIC
    src/aerodynamics.cpp
    src/astro_time.cpp
    src/atmosphere.cpp
    src/demo.cpp
    src/ephemeris.cpp
    src/flight_program.cpp
    src/gravity.cpp
    src/earth.cpp
    src/guidance.cpp
    src/integrator.cpp
    src/lost_in_space.cpp
    src/lunar_targeting.cpp
    src/mission_parameters.cpp
    src/natural_body.cpp
    src/natural_body_flight.cpp
    src/orbit.cpp
    src/parse.cpp
    src/rocket.cpp
    src/scenario.cpp
    src/simulation.cpp
    src/solar_system.cpp
    src/sweep.cpp
    src/two_body.cpp
    src/vec3.cpp
    src/voyage.cpp
)
add_library(orbit::core ALIAS orbit_core)

target_include_directories(orbit_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_compile_features(orbit_core PUBLIC cxx_std_20)
target_link_libraries(orbit_core PRIVATE nlohmann_json::nlohmann_json)
orbit_set_warnings(orbit_core)
