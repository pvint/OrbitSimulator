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
//
// WHERE YOU'RE LOST
// A puzzle can start anywhere between the planets (a random orbit not far
// from the Earth's, wider for harder puzzles) or near a place: Venus,
// Mars, the asteroid belt, or one of the giant planets. Near a planet, the ship shares its
// orbit, a few degrees ahead of it or behind it, with a small random push
// so the two drift apart: far enough that the planet's own pull hardly
// matters. The further out, the longer the trip and the faster the ship
// meets the Earth: by Kepler's third law, a Hohmann transfer from Jupiter
// (half an ellipse from 5.2 au down to 1 au) takes 2.7 years, and arrives
// nearly 9 km/s faster than the Earth is moving. From Neptune the same
// half ellipse takes 31 years: a reason to look for a gravity assist.
//
// COMING IN TOO FAST
// A capsule falling from far away meets the air at √(v∞² + 2μ/r): from
// Jupiter's distance, over 14 km/s, more than any heat shield has taken.
// To land, it must first shed some of v∞, braking against its motion
// relative to the Earth once inside its sphere of influence
// (approachBrake).
#pragma once

#include "orbit/core/voyage.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace orbit {

enum class Difficulty { Easy, Normal, Hard };
std::string_view difficultyName(Difficulty difficulty);

/// Where a puzzle starts.
enum class Region {
    BetweenPlanets, ///< a random orbit not far from the Earth's
    Venus,
    Mars,
    AsteroidBelt,
    Jupiter,
    Saturn,
    Uranus,
    Neptune,
};
inline constexpr std::array kAllRegions{Region::BetweenPlanets, Region::Venus,   Region::Mars,
                                        Region::AsteroidBelt,   Region::Jupiter, Region::Saturn,
                                        Region::Uranus,         Region::Neptune};
/// A short key for files and the command line ("between", "venus", "mars",
/// "belt", "jupiter", "saturn", "uranus", "neptune"), and back.
std::string_view regionKey(Region region);
std::optional<Region> regionFromKey(std::string_view key);
/// For people: "Between the planets", "Near Venus", ...
std::string_view regionName(Region region);

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

/// The fastest v∞ (m/s) that a re-entry can arrive with, keeping a little
/// under the fastest survivable entry speed: v∞ = √(v_entry² − 2μ/r_I).
double fastestReentryArrival(const VoyageOptions& options = {});

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
    double departure = 0.0; ///< epoch of the burn
    double arrival = 0.0;   ///< epoch at the Earth
    Vec3 departureDeltaV;   ///< m/s, heliocentric, with only the Sun pulling
    /// m/s: |departureDeltaV|, or in a puzzle the burn from where the full
    /// simulation puts the ship, if that is dearer.
    double departureCost = 0.0;
    double vInfinity = 0.0;   ///< arrival speed relative to the Earth (m/s)
    double arrivalCost = 0.0; ///< m/s (arrivalCost())
    double total() const { return departureCost + arrivalCost; }
};

/// The cheapest transfer leaving within `window` seconds of `epoch` and
/// taking between 30 days and `longestFlight`, for a ship at (r, v) at
/// `epoch` with only the Sun pulling. Nothing if none was found.
std::optional<TransferPlan> cheapestWayHome(double epoch, const Vec3& r, const Vec3& v, Goal goal,
                                            double window, double longestFlight);

/// How long to search for a way home from (r, v) around the Sun.
struct SearchSpan {
    /// Departures within this long (s): about one synodic period, the time
    /// for the ship and the Earth to come back to the same places relative
    /// to each other, 1 / |1/T − 1/T_earth| (between 1 and 2.2 years).
    double window = 0.0;
    /// Trips up to this long (s): 1.3 times a Hohmann transfer's from the
    /// orbit's far point down (or up) to the Earth's orbit, π √(a³/μ) with
    /// a the average of the two distances; at least 540 days, at most 40
    /// years.
    double longestFlight = 0.0;
};
SearchSpan searchSpan(const Vec3& r, const Vec3& v);

struct Puzzle {
    Difficulty difficulty = Difficulty::Easy;
    Region region = Region::BetweenPlanets;
    std::uint64_t seed = 0;
    Voyage voyage;       ///< the ship where it starts, no burns yet
    TransferPlan plan;   ///< the cheapest way home found
    double budget = 0.0; ///< Δv in the tanks (m/s)
};

/// A puzzle starting at `epoch`, in `region`. The same seed, difficulty
/// and region always give the same puzzle, on any computer.
Puzzle makePuzzle(Difficulty difficulty, std::uint64_t seed, double epoch,
                  Region region = Region::BetweenPlanets);

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

/// For a re-entry: if the path meets the Earth faster than
/// fastestReentryArrival(), a braking burn once inside its sphere of
/// influence (about a third of the way from its edge to the closest
/// approach, and not before `notBefore`), straight back against the motion
/// relative to the Earth, that brings v∞ down to that. Energy around the
/// Earth gives the speed wanted there: √(v∞² + 2μ/r). Nothing if the path
/// isn't too fast, doesn't come within the sphere of influence, or there's
/// no time left. Aim it with refineAim afterwards: a slower arrival is
/// bent more, so it needs a wider miss.
std::optional<Burn> approachBrake(const Voyage& voyage, double notBefore,
                                  const VoyageOptions& options = {});

/// The speed relative to the Earth far from it, v∞ = √(v² − 2μ/r), of a
/// path past it (energy around the Earth), from its closest approach
/// (m/s); 0 if it never gets near or is bound.
double approachSpeed(const EarthApproach& closest);

} // namespace orbit
