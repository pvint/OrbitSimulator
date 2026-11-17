# SPDX-License-Identifier: GPL-3.0-or-later
# orbit_core: the physics library.
#
# Rule: nothing in here may depend on Qt. Plain C++20 and the standard library
# only, so the physics can be tested, run from the command line, and reused
# by any front end.

configure_file(version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/orbit/core/version.hpp" @ONLY)

add_library(orbit_core STATIC
    src/earth.cpp
    src/guidance.cpp
    src/integrator.cpp
    src/mission_parameters.cpp
    src/orbit.cpp
    src/rocket.cpp
    src/simulation.cpp
    src/vec3.cpp
)
add_library(orbit::core ALIAS orbit_core)

target_include_directories(orbit_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_compile_features(orbit_core PUBLIC cxx_std_20)
orbit_set_warnings(orbit_core)
