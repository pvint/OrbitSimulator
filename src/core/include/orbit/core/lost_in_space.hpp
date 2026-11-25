// SPDX-License-Identifier: GPL-3.0-or-later
// "Lost in space": the game's puzzles and hints.
//
// A puzzle puts a ship on a random orbit around the Sun and gives it just
// enough propellant to get home, with a margin that shrinks with the
// difficulty. "Just enough" is worked out by solving the puzzle first:
//
// THE CHEAPEST WAY HOME
// For every pair of (departure day, arrival day) in a window, Lambert's
// problem (two_body.hpp) gives the transfer orbit from where the ship will
// be on the departure day to where the Earth will be on the arrival day.
// The departure burn is the difference between the transfer's velocity and
// the ship's; at the far end, the transfer arrives with some speed relative
// to the Earth, v∞, which may cost more to deal with (see arrivalCost).
// Plotting the total over the two days gives the famous "porkchop plot" of
// mission design; the puzzle's fuel is set from its lowest point. This
// treats the Sun as the only pull, as every first-cut mission design does;
// the real flight (voyage.hpp) adds the planets, which is what course
// corrections on the way are for.
//
// AIMING AT THE EARTH
// Arriving "at the Earth's centre" is not the point: the path must miss it
// by just the right amount so that the Earth's gravity bends it into the
// atmosphere at the right angle, or to the right perigee. The miss distance
// is measured on the "B-plane", the plane through the Earth's centre square
// to the incoming path; the impact parameter b (the miss distance if the
// Earth didn't pull) gives a perigee r_p where
//     b = r_p √(1 + 2μ/(r_p v∞²))
// (conservation of angular momentum, h = b v∞ = r_p v_p, and of energy,
// v_p² = v∞² + 2μ/r_p). Gravity "focuses" the approach: a target the size
// of the Earth's atmosphere is a few times larger in the B-plane.
#pragma once

#include "orbit/core/voyage.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace orbit {

enum class Difficulty { Easy, Normal, Hard };
std::string_view difficultyName(Difficulty difficulty);

/// The goal at each difficulty: Easy, reach the Earth; Normal, re-enter
/// inside the corridor; Hard, end in orbit around the Earth.
Goal goalFor(Difficulty difficulty);

/// Δv needed near the Earth, arriving with speed v∞ relative to it (m/s):
///   ReachEarth  nothing.
///   Reentry     slowing down until the entry speed √(v∞² + 2μ/r) at the
///               entry interface is survivable: the excess of v∞ over
///               √(v_max² − 2μ/r), taken off far out where it costs least.
///   EarthOrbit  braking at a 200 km perigee from the hyperbola's speed
///               √(v∞² + 2μ/r_p) into an ellipse reaching out to half the
///               Moon's distance, √(2μ/r_p − 2μ/(r_p + r_a)).
double arrivalCost(Goal goal, double vInfinity, const VoyageOptions& options = {});

/// The impact parameter b to aim for (m), arriving with speed v∞ (m/s):
///   Reentry     the middle of the corridor: an entry at γ = −6.5°, from
///               h = r_I v_I cos γ at the entry interface (r_I, v_I there),
///               and b = h / v∞;
///   EarthOrbit  a 200 km perigee;
///   ReachEarth  a 1000 km one (anything inside the sphere of influence
///               would do).
double aimImpactParameter(Goal goal, double vInfinity, const VoyageOptions& options = {});

/// A transfer from the ship's orbit to the Earth.
struct TransferPlan {
    double departure = 0.0;     ///< epoch of the burn
    double arrival = 0.0;       ///< epoch at the Earth
    Vec3 departureDeltaV;       ///< m/s, heliocentric
    double departureCost = 0.0; ///< m/s
    double vInfinity = 0.0;     ///< arrival speed relative to the Earth (m/s)
    double arrivalCost = 0.0;   ///< m/s (arrivalCost())
    double total() const { return departureCost + arrivalCost; }
};

/// The cheapest transfer leaving within `window` seconds of `epoch` and
/// taking between 30 days and `longestFlight`, for a ship at (r, v) at
/// `epoch` with only the Sun pulling. Nothing if none was found.
std::optional<TransferPlan> cheapestWayHome(double epoch, const Vec3& r, const Vec3& v, Goal goal,
                                            double window, double longestFlight);

struct Puzzle {
    Difficulty difficulty = Difficulty::Easy;
    std::uint64_t seed = 0;
    Voyage voyage;       ///< the ship where it starts, no burns yet
    TransferPlan plan;   ///< the cheapest way home found
    double budget = 0.0; ///< Δv in the tanks (m/s)
};

/// A puzzle starting at `epoch`. The same seed and difficulty always give
/// the same puzzle, on any computer.
Puzzle makePuzzle(Difficulty difficulty, std::uint64_t seed, double epoch);

// --- Hints ---------------------------------------------------------------

/// A burn at `epoch` that sends the ship from where it is then (flying the
/// voyage's burns before it) to the Earth at `arrival`, aimed with the
/// B-plane formula above at the goal's perigee: one Lambert solution,
/// with only the Sun pulling.
std::optional<Burn> burnTowardsEarth(const Voyage& voyage, double epoch, double arrival,
                                     const VoyageOptions& options = {});

/// Fine-tunes burn `index` of the voyage (in time order) with the full
/// simulation so the path meets the Earth as aimImpactParameter() says,
/// changing the burn as little as possible. Newton's
/// method on the B-vector B = ŝ × h / v∞ (ŝ the incoming direction, h the
/// angular momentum about the Earth), with its derivatives by finite
/// differences. Nothing if it doesn't converge.
std::optional<Burn> refineAim(const Voyage& voyage, std::size_t index,
                              const VoyageOptions& options = {});

/// The braking burn at the coming perigee that leaves the ship in an orbit
/// around the Earth reaching out to half the Moon's distance (or nothing,
/// if the path doesn't come within the Earth's sphere of influence).
std::optional<Burn> captureBurn(const Voyage& voyage, const VoyageOptions& options = {});

} // namespace orbit
