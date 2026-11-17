# SPDX-License-Identifier: GPL-3.0-or-later
# orbitsim-cli: runs the physics without any UI. Handy for checking results
# quickly, and later for batch sweeps that write CSV.

add_executable(orbitsim-cli main.cpp)
target_link_libraries(orbitsim-cli PRIVATE orbit::core)
orbit_set_warnings(orbitsim-cli)

install(TARGETS orbitsim-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
