// SPDX-License-Identifier: GPL-3.0-or-later
// Free roam: the game with no goal. Pick a planet or a moon, start in orbit
// around it with unlimited fuel, and go wherever you like: the physics is
// the same as in the puzzles (voyage.hpp), every planet and moon pulling.
//
// WHERE YOU START
// In a circular orbit, low but clear of the air (or Titan's haze): the
// parking orbit the puzzles use (lost_in_space.hpp, parkingOrbit()). The
// giant planets are the exception: there the puzzles start on a long
// ellipse like Juno's, which is cheap to leave but loops out through the
// moons' orbits; with unlimited fuel, cheap doesn't matter, so free roam
// starts on a circle at twice the planet's radius, inside all its major
// moons, going round in a few hours (Kepler's third law,
// T = 2π √(r³/μ)).
//
// The orbit lies in the plane of the body's own orbit (round the Sun for a
// planet, round its planet for a moon) and goes round the same way: the
// plane a ship would most likely be in, and the one where leaving forwards
// adds the body's own speed to yours. (The game has no axial tilts, so
// there's no equator to orbit over.) Its low point faces the Sun (for a
// moon, its planet).
//
// UNLIMITED FUEL
// The ship's tanks never run dry (Ship::unlimitedFuel): burns cost nothing,
// and the game counts the Δv they add up to instead, the number a mission
// designer would have to find propellant for.
#pragma once

#include "orbit/core/lost_in_space.hpp"
#include "orbit/core/solar_system.hpp"

namespace orbit {

/// How long a free-roam prediction starting at `body` looks ahead past the
/// last burn (s): a year, enough to see where a burn takes you among the
/// planets; but 30 days among a giant planet's moons, where a path loops
/// round every few days and each day takes many steps to fly. The game
/// looks further as the clock runs on (GameSession).
double freeRoamHorizon(Body body);

/// Can a free-roam game start around it? Any planet, and any moon a ship
/// can park around (canPark()).
bool canRoamFrom(Body body);

/// The orbit a free-roam game starts on around the body (m from its
/// centre): see WHERE YOU START.
ParkingOrbit freeRoamOrbit(Body body);

/// A free-roam game starting at `epoch` in orbit around `body` (a body
/// that canRoamFrom(); anything else starts at the Earth). The Puzzle holds
/// it so the game can play it like any other: no goal (Goal::Explore), the
/// body as its "destination", no plan and no budget.
Puzzle freeRoam(Body body, double epoch);

/// Is this game free roam?
bool isFreeRoam(const Puzzle& puzzle);

} // namespace orbit
