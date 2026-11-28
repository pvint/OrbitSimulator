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

#include "orbit/core/flyby.hpp"
#include "orbit/core/two_body.hpp"
#include "orbit/core/voyage.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace orbit {

enum class Difficulty { Easy, Normal, Hard };
std::string_view difficultyName(Difficulty difficulty);

/// Where a puzzle starts.
enum class Region {
    BetweenPlanets, ///< a random orbit not far from the Earth's
    Mercury,
    Venus,
    Earth, ///< always in orbit around it
    Mars,
    AsteroidBelt,
    Jupiter,
    Saturn,
    Uranus,
    Neptune,
};
inline constexpr std::array kAllRegions{
    Region::BetweenPlanets, Region::Mercury, Region::Venus,  Region::Earth,  Region::Mars,
    Region::AsteroidBelt,   Region::Jupiter, Region::Saturn, Region::Uranus, Region::Neptune};
/// A short key for files and the command line ("between", "mercury",
/// "venus", "earth", "mars", "belt", "jupiter", "saturn", "uranus",
/// "neptune"), and back.
std::string_view regionKey(Region region);
std::optional<Region> regionFromKey(std::string_view key);
/// For people: "Between the planets", "Near Venus", ...
std::string_view regionName(Region region);
/// The planet a region is about, if any.
std::optional<Body> regionPlanet(Region region);
/// The region about a planet (a moon: its planet's).
Region regionOf(Body body);

/// The goal at each difficulty. Going home: Easy, reach the Earth; Normal,
/// re-enter inside the corridor; Hard, end in orbit around the Earth. To
/// another planet or a moon: reach it; pass close (a flyby probe); end in
/// orbit (at a moon too small to orbit, canPark(), a close pass again).
/// Starting at the Moon, home is always a re-entry (Apollo's way back):
/// the ship is inside the Earth's sphere of influence from the start, and
/// any path that leaves the Moon without falling in is already an orbit
/// around the Earth. The difficulty sets the fuel.
Goal goalFor(Difficulty difficulty, Body destination = Body::Earth,
             std::optional<Body> startMoon = std::nullopt);

/// An orbit around a planet, by its low and high points (m from the
/// centre; equal for a circle).
struct ParkingOrbit {
    double periapsis = 0.0;
    double apoapsis = 0.0;
};
/// Where a puzzle "in orbit" starts: 200 km up around the Earth and
/// Mercury, 300 km around Venus and Mars (above their air), and a long
/// ellipse from 1.1 to 50 radii around a giant planet (like Juno's).
ParkingOrbit parkingOrbit(Body planet);
/// Can a ship park around it? Not if a low orbit wouldn't lie well inside
/// its sphere of influence (small moons close to a giant planet).
bool canPark(Body body);
/// How far out the orbit braked into on arrival reaches (m): half the
/// Moon's distance at the Earth; at a moon, 0.35 of its sphere of
/// influence; elsewhere a fifth of the sphere of influence, or 100 radii if
/// less.
double captureApoapsis(Body planet, double epoch, const VoyageOptions& options = {});
/// The closest approach to aim for at a planet other than the Earth (m,
/// from its centre): 10 radii to reach it, 2 for a close pass, 1.1 to brake
/// into orbit (at a moon, its parking orbit's height: above Titan's haze).
double aimPeriapsis(Goal goal, Body planet);

/// The Oberth effect: leaving a parking orbit from its low point r_p on a
/// path that ends up v∞ from the planet takes √(v∞² + 2μ/r_p) − v_p (m/s),
/// v_p = √(2μ/r_p − 2μ/(r_p + r_a)) the speed there now. Deep in a
/// planet's gravity, a little extra speed becomes a lot of v∞.
double departureCost(Body planet, const ParkingOrbit& orbit, double vInfinity);

/// Δv needed near the Earth, arriving with speed v∞ relative to it (m/s):
///   ReachEarth  nothing.
///   Reentry     slowing down until the entry speed √(v∞² + 2μ/r) at the
///               entry interface is survivable: the excess of v∞ over
///               √(v_max² − 2μ/r), taken off far out where it costs least.
///   EarthOrbit  braking at a 200 km perigee from the hyperbola's speed
///               √(v∞² + 2μ/r_p) into an ellipse reaching out to half the
///               Moon's distance, √(2μ/r_p − 2μ/(r_p + r_a)).
double arrivalCost(Goal goal, double vInfinity, const VoyageOptions& options = {});
/// The same for any destination; at another planet, only PlanetOrbit costs
/// anything: braking at 1.1 radii into an ellipse out to captureApoapsis.
double arrivalCost(Goal goal, double vInfinity, Body destination,
                   const VoyageOptions& options = {});

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

/// A transfer from the ship's orbit to the destination.
struct TransferPlan {
    double departure = 0.0; ///< epoch of the burn
    double arrival = 0.0;   ///< epoch at the destination
    /// m/s, heliocentric, with only the Sun pulling; leaving a parking
    /// orbit, the v∞ needed relative to that planet.
    Vec3 departureDeltaV;
    /// m/s: |departureDeltaV|, or in a puzzle the burn from where the full
    /// simulation puts the ship, if that is dearer.
    double departureCost = 0.0;
    double vInfinity = 0.0;   ///< arrival speed relative to the destination (m/s)
    double arrivalCost = 0.0; ///< m/s (arrivalCost())
    double total() const { return departureCost + arrivalCost; }
};

/// The cheapest transfer leaving within `window` seconds of `epoch` and
/// taking between 30 days and `longestFlight`, for a ship at (r, v) at
/// `epoch` with only the Sun pulling. Nothing if none was found.
std::optional<TransferPlan> cheapestWayHome(double epoch, const Vec3& r, const Vec3& v, Goal goal,
                                            double window, double longestFlight);

/// Where a transfer leaves from: the ship's own orbit around the Sun, or,
/// parked around a planet, the planet itself (paying departureCost for an
/// orbit to be set up to suit; or, given `points` round an orbit already
/// set, the cheapest escapeFrom one of them).
///
/// Parked around a moon, `planet` is the moon's planet, `moon` the moon,
/// `orbit` the orbit around the moon and `points` the moon's own places
/// round the planet: leaving takes two escapes, one inside the other
/// (escapeFromMoon).
struct DepartureFrom {
    std::optional<Body> planet;
    ParkingOrbit orbit;
    std::vector<TwoBodyState> points; ///< relative to the planet
    std::optional<Body> moon = std::nullopt;
};

/// Leaving an orbit from the point `at` (relative to a body of μ) on an
/// escape path that ends up at v∞ (vector): the burn, at its cheapest with
/// its low point there, and its cost (plus what missing the direction
/// would cost to put right later, about v∞ per radian).
struct Escape {
    Vec3 deltaV;
    double cost = 0.0;
};
std::optional<Escape> escapeFrom(const TwoBodyState& at, const Vec3& vInfinity, double mu);

/// Leaving a planet's system from an orbit around one of its moons, so as
/// to end up v∞ (vector) from the planet. Patched conics, twice: the path
/// out of the planet's gravity is a hyperbola with its low point at the
/// moon (`moonState`, relative to the planet), which escapeFrom gives; the
/// ship must leave the moon at the difference between that hyperbola's
/// speed there and the moon's own velocity, a v∞ relative to the moon; and
/// climbing out of the moon's gravity to that costs departureCost from
/// `orbit` around it. The moon's motion helps: leaving forwards along it,
/// the ship starts with the moon's speed round the planet for free.
/// Escape::deltaV is the v∞ relative to the moon; Escape::cost the whole
/// burn, plus escapeFrom's penalty for a poorly placed moon.
std::optional<Escape> escapeFromMoon(Body moon, const ParkingOrbit& orbit,
                                     const TwoBodyState& moonState, const Vec3& vInfinity);

/// `count` points evenly spaced in time round the orbit through `rel`.
std::vector<TwoBodyState> orbitPoints(const TwoBodyState& rel, double mu, int count);

/// The cheapest transfer to `to`, as cheapestWayHome. To a moon: to its
/// planet, the arrival priced at moonApproachSpeed() (moon_transfer.hpp),
/// and TransferPlan::vInfinity is that speed relative to the moon.
std::optional<TransferPlan> cheapestTransfer(double epoch, const Vec3& r, const Vec3& v,
                                             const DepartureFrom& from, Body to, Goal goal,
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
SearchSpan searchSpan(const Vec3& r, const Vec3& v, Body to = Body::Earth);

/// What to play: from where (drifting, or in orbit around the region's
/// planet or one of its moons), to where, how hard.
struct PuzzleSpec {
    Difficulty difficulty = Difficulty::Easy;
    Region from = Region::BetweenPlanets;
    bool inOrbit = false;
    Body to = Body::Earth;
    /// In orbit around this moon (of the region's planet) instead.
    std::optional<Body> startMoon = std::nullopt;
};
/// The spec made sensible: in orbit only around a planet (always around
/// the Earth, and around a planet going to one of its moons), and not to
/// the planet it starts at. A moon to start at sets the region to its
/// planet's and means in orbit; from a moon, not to itself, nor to its
/// planet unless that is the Earth (home from the Moon).
PuzzleSpec settledSpec(PuzzleSpec spec);

struct Puzzle {
    Difficulty difficulty = Difficulty::Easy;
    Region region = Region::BetweenPlanets;
    bool inOrbit = false;
    std::optional<Body> startMoon; ///< parked around this moon at the start
    Body destination = Body::Earth;
    std::uint64_t seed = 0;
    Voyage voyage;       ///< the ship where it starts, no burns yet
    TransferPlan plan;   ///< the cheapest way there found
    double budget = 0.0; ///< Δv in the tanks (m/s)
};

/// A puzzle starting at `epoch`, in `region`. The same seed, difficulty
/// and region always give the same puzzle, on any computer.
Puzzle makePuzzle(Difficulty difficulty, std::uint64_t seed, double epoch,
                  Region region = Region::BetweenPlanets);
/// Any puzzle. Starting in orbit, the game starts 10 to 30 days before the
/// cheapest departure, the orbit set up as if launched for it: the escape
/// starts at its low point, in its plane. In orbit around a planet going to
/// one of its moons, an orbit or two before a Hohmann transfer to it, in the
/// moon's plane. In orbit around a moon, an orbit or two before the
/// cheapest departure from it (planFromMoon, moon_transfer.hpp), the orbit
/// set up for it.
Puzzle makePuzzle(const PuzzleSpec& spec, std::uint64_t seed, double epoch);

// --- Hints ---------------------------------------------------------------

/// A burn at `epoch` that sends the ship from where it is then (flying the
/// voyage's burns before it) to the Earth at `arrival`, aimed with the
/// B-plane formula above at the goal's perigee: one Lambert solution,
/// with only the Sun pulling.
std::optional<Burn> burnTowardsEarth(const Voyage& voyage, double epoch, double arrival,
                                     const VoyageOptions& options = {});
/// The same, to the voyage's destination (aimed at aimPeriapsis away from
/// a planet other than the Earth; to a moon, at its planet, to pass at the
/// moon's distance going round the way it does). Nothing inside a planet's
/// sphere of influence: see transferBurn, and planToMoon for the last leg
/// to a moon.
std::optional<Burn> burnTowards(const Voyage& voyage, double epoch, double arrival,
                                const VoyageOptions& options = {});

/// Leaving a parked orbit (parkedAround) so as to end up v∞ (heliocentric
/// vector) from the planet: the burn at the point within one orbit after
/// `earliest` that costs least. There the escape path has its low point,
/// in the plane of the position and v∞, moving at √(v∞² + 2μ/r).
std::optional<Burn> departureBurn(const Voyage& voyage, double earliest, const Vec3& vInfinity,
                                  const VoyageOptions& options = {});

/// The first burn of a transfer leaving on `departure` and arriving at the
/// destination on `arrival`: burnTowards when drifting, departureBurn (for
/// the v∞ Lambert asks for, from the planet's centre, within half an orbit
/// of the day) when parked.
std::optional<Burn> transferBurn(const Voyage& voyage, double departure, double arrival,
                                 const VoyageOptions& options = {});

/// Fine-tunes burn `index` so the path meets the destination as the goal
/// wants: refineAim for the Earth, aimFlyby at aimPeriapsis elsewhere. To
/// a moon not yet in sight (no pass within three of its spheres of
/// influence), aimFlyby at its planet, passing at the moon's distance.
std::optional<Burn> aimAtDestination(const Voyage& voyage, std::size_t index,
                                     const VoyageOptions& options = {});

/// Fine-tunes burn `index` of the voyage (in time order) with the full
/// simulation so the path meets the Earth as aimImpactParameter() says,
/// changing the burn as little as possible. Newton's
/// method on the B-vector B = ŝ × h / v∞ (ŝ the incoming direction, h the
/// angular momentum about the Earth), with its derivatives by finite
/// differences. Nothing if it doesn't converge.
///
/// A path that never escaped the Earth (falling back from the Moon) has no
/// v∞ and no B-plane: then only the perigee matters, aimed with
/// aimPerigee() (one equation, the smallest change to the burn).
std::optional<Burn> refineAim(const Voyage& voyage, std::size_t index,
                              const VoyageOptions& options = {});

/// The perigee (m, from the Earth's centre) for the goal on a path of
/// energy ε = v²/2 − μ/r around the Earth (J/kg; negative if bound): for a
/// re-entry, the one whose path meets the entry interface at −6.5°, from
/// h = r_I v_I cos γ with v_I = √(2(ε + μ/r_I)), and the conic's
/// r_p = h² / (μ (1 + e)), e = √(1 + 2εh²/μ²); else the goal's perigee
/// height.
double aimPerigee(Goal goal, double energy, const VoyageOptions& options = {});

/// The braking burn at the coming low point that leaves the ship in an
/// orbit around the destination reaching out to captureApoapsis (or
/// nothing, if the path doesn't come within its sphere of influence).
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
