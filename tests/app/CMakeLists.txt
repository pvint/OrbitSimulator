# SPDX-License-Identifier: GPL-3.0-or-later
# Tests for the app's panels and simulation controller, using Qt Test.
# They run without a window system (QT_QPA_PLATFORM=offscreen), so they also
# work on a build server.

find_package(Qt6 6.8 REQUIRED COMPONENTS Test)

add_executable(orbitsim_ui_tests test_ui.cpp)
target_link_libraries(orbitsim_ui_tests PRIVATE orbitsim_ui Qt6::Test)
set_target_properties(orbitsim_ui_tests PROPERTIES AUTOMOC ON)
orbit_set_warnings(orbitsim_ui_tests)

add_test(NAME orbitsim_ui_tests COMMAND orbitsim_ui_tests)
set_tests_properties(orbitsim_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The Lost in Space game's session and burn planner.
add_executable(orbitsim_game_tests test_game.cpp)
target_link_libraries(orbitsim_game_tests PRIVATE orbitsim_ui Qt6::Test)
set_target_properties(orbitsim_game_tests PROPERTIES AUTOMOC ON)
orbit_set_warnings(orbitsim_game_tests)

add_test(NAME orbitsim_game_tests COMMAND orbitsim_game_tests)
set_tests_properties(orbitsim_game_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
