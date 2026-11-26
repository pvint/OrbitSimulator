// SPDX-License-Identifier: GPL-3.0-or-later
// A voyage: a ship somewhere around the Sun, with a tank of propellant and a
// list of planned burns, flown with every planet pulling. This is the
// physics of the "Lost in space" game: find your way back to the Earth on
// the fuel you have.
//
// FRAME AND GRAVITY
// Heliocentric ecliptic J2000 (solar_system.hpp), the Sun, the eight planets
// and the Moon all pulling (heliocentricGravity). Near the Earth the same
// sum still holds; the Earth's pull simply takes over, so a near miss is
// bent, and an arrival curves in, exactly as it would.
//
// BURNS ARE INSTANT
// A burn changes the velocity at one moment, by Δv, and spends propellant
// by Tsiolkovsky's rocket equation
//     Δv = v_e ln(m_before / m_after),   v_e = Isp · g0,
// so each m/s costs more the heavier the ship still is. Real engines take
// minutes to give a few km/s; the voyage takes months, so the craft moves
// a negligible distance during a burn compared with the whole path. This
// "impulsive burn" approximation is how mission designers plan
// interplanetary transfers before refining them (the error grows for long,
// weak burns, which this ship does not have).
//
// BURN DIRECTIONS
// Each burn is given in the ship's own directions, relative to the body it
// is falling around at that moment (the Earth inside its sphere of
// influence, the Sun outside it):
//   prograde   along the velocity: faster (negative: retrograde, slower).
//              Raises (lowers) the far side of the orbit.
//   radial     perpendicular to the velocity, in the orbit's plane, away
//              from the body: turns the path outwards, swinging the orbit
//              round without much change in its size.
//   normal     along the orbit's angular momentum, "north" of the orbit's
//              plane: tilts the plane.
//
// COMING HOME
// The Earth's sphere of influence (about 925,000 km) counts as reaching it.
// Re-entry happens at the "entry interface", 120 km up, where the air
// starts to matter; whether a capsule survives depends on the angle and
// speed it arrives at there (the corridor, see VoyageOptions). Capture into
// orbit means being left on a closed orbit around the Earth whose perigee
// clears the atmosphere.
#pragma once

#include "orbit/core/angles.hpp"
#include "orbit/core/integrator.hpp"
#include "orbit/core/orbit.hpp"
#include "orbit/core/rocket.hpp"
#include "orbit/core/solar_system.hpp"
#include "orbit/core/vec3.hpp"

#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace orbit {

/// The ship. Only what the rocket equation needs: burns are instant.
struct Ship {
    double dryMass = 2000.0;    ///< kg, empty
    double propellant = 1000.0; ///< kg, in the tanks
    double isp = 320.0;         ///< s, specific impulse (a storable-propellant engine)
};

/// What a full tank is worth: Δv = v_e ln((dry + propellant) / dry) (m/s).
double deltaVCapacity(const Ship& ship);

/// The propellant needed to give `deltaV` to a ship of mass `mass`:
/// m (1 − e^(−Δv / v_e)) (kg).
double propellantFor(double deltaV, double mass, double isp);

/// What counts as being home.
enum class Goal {
    ReachEarth, ///< enter the Earth's sphere of influence
    Reentry,    ///< arrive at the atmosphere inside the entry corridor
    EarthOrbit, ///< end on a closed orbit around the Earth, clear of the air
};

/// One instant burn (m/s in each direction; see the top of the file).
struct Burn {
    double epoch = 0.0; ///< s since J2000
    double prograde = 0.0;
    double radial = 0.0;
    double normal = 0.0;
};

/// The size of the burn, √(prograde² + radial² + normal²) (m/s).
double burnSize(const Burn& burn);

/// The body a burn's directions are taken relative to.
enum class ReferenceBody { Sun, Earth };

/// The ship's own directions at a moment, relative to the body that
/// dominates there (unit vectors, heliocentric ecliptic frame).
struct BurnFrame {
    ReferenceBody body = ReferenceBody::Sun;
    Vec3 prograde;
    Vec3 radial;
    Vec3 normal;
};
BurnFrame burnFrameAt(double epoch, const Vec3& r, const Vec3& v);

/// The burn as a velocity change in the frame (m/s), and back.
Vec3 burnVector(const Burn& burn, const BurnFrame& frame);
Burn burnFromVector(double epoch, const Vec3& deltaV, const BurnFrame& frame);

struct Voyage {
    double startEpoch = 0.0; ///< s since J2000
    Vec3 position;           ///< m, heliocentric ecliptic J2000
    Vec3 velocity;           ///< m/s
    Ship ship;
    Goal goal = Goal::ReachEarth;
    std::vector<Burn> burns; ///< flown in order of time; burns before the start are skipped
    /// Predictions look at least this far past the last burn, or the last
    /// flyby (s), if more than VoyageOptions::horizon: a puzzle far out sets
    /// its longest trip.
    double lookAhead = 0.0;
};

struct VoyageOptions {
    /// How far to look ahead past the latest of the start, the last burn
    /// and the last flyby (a pass by a planet that leaves it again): long
    /// enough for the trip home from Jupiter (about 2.7 years).
    double horizon = 4.0 * 365.25 * 86400.0;
    /// If set, the flight stops at this epoch instead (to get the state
    /// there, or to keep a prediction short).
    std::optional<double> stopAt;
    double entryInterfaceAltitude = 120e3; ///< m
    /// The re-entry corridor, as the flight-path angle at the entry
    /// interface (radians, negative = descending). Steeper than about −8°,
    /// deceleration and heating are too much; shallower than about −5°, the
    /// capsule skips off the top of the air. (Apollo's corridor was about
    /// −5.2° to −7.7° at 11 km/s.)
    double steepestEntry = degToRad(-8.0);
    double shallowestEntry = degToRad(-5.0);
    /// The fastest survivable entry (m/s, relative to the Earth's centre).
    /// Stardust, the fastest return so far, came in at 12.9 km/s in 2006.
    double fastestEntry = 13000.0;
    double captureMinPerigeeAltitude = 150e3; ///< m: lower, and the air drags it down
    double captureMaxApogee = 384400e3;       ///< m: the Moon's distance
    Tolerances tolerances;
};

/// How the flight (or its prediction) ended.
enum class VoyageEnd {
    Horizon,      ///< still out there when the prediction stops
    StopTime,     ///< reached VoyageOptions::stopAt
    ReachedEarth, ///< entered the Earth's sphere of influence (ends only that goal)
    Reentered,    ///< hit the atmosphere inside the corridor
    TooSteep,     ///< hit the atmosphere too steeply: burned up
    TooFast,      ///< hit it faster than any heat shield can take
    Skipped,      ///< grazed it too shallowly and skipped off into space
    Captured,     ///< left on a closed orbit around the Earth
    HitSun,
};
std::string_view describe(VoyageEnd end);

/// Does this ending meet the goal?
bool goalMet(Goal goal, VoyageEnd end);

struct VoyageSample {
    double epoch = 0.0;
    Vec3 r;            ///< m, heliocentric
    Vec3 v;            ///< m/s
    double mass = 0.0; ///< kg
};

struct BurnReport {
    Burn burn; ///< as planned
    ReferenceBody body = ReferenceBody::Sun;
    double deltaV = 0.0;         ///< m/s actually given
    double propellantUsed = 0.0; ///< kg
    bool shortOfFuel = false;    ///< the tank ran dry part way
};

/// The closest the path comes to the Earth.
struct EarthApproach {
    bool valid = false;
    double epoch = 0.0;
    double distance = 0.0; ///< m, from the Earth's centre
    double speed = 0.0;    ///< m/s, relative to the Earth
    Vec3 relativePosition; ///< m, from the Earth's centre (ecliptic axes)
    Vec3 relativeVelocity; ///< m/s
    /// The orbit around the Earth there, as if only the Earth pulled:
    /// its perigee is where the path would bottom out.
    OrbitalElements orbit;
};

/// A pass by a planet. A gravity assist: inside the planet's sphere of
/// influence the path is (nearly) a hyperbola around it. Relative to the
/// planet the ship leaves as fast as it came, v∞, only turned by δ, with
/// sin(δ/2) = 1/e; but the planet is moving, so around the Sun the ship's
/// speed changes: |V_planet + v∞_out| instead of |V_planet + v∞_in|.
/// Passing behind the planet (on the side it is moving away from) turns
/// v∞ towards the planet's motion and speeds the ship up; passing in
/// front slows it down. The closer the pass and the slower v∞, the
/// bigger the turn.
struct PlanetPass {
    bool valid = false;
    /// A true closest approach (the distance stopped shrinking there), not
    /// just where the flight started, burned or ended.
    bool turning = false;
    Planet planet = Planet::Earth;
    double epoch = 0.0;
    double distance = 0.0;  ///< m, from the planet's centre
    double speed = 0.0;     ///< m/s, relative to the planet
    double vInfinity = 0.0; ///< m/s, far from the planet (0 if not a hyperbola)
    double turnAngle = 0.0; ///< radians, δ (0 if not a hyperbola)
    /// Speed around the Sun on the way in and on the way out (m/s), as if
    /// only the planet pulled during the pass (the same, if no hyperbola).
    double sunSpeedBefore = 0.0;
    double sunSpeedAfter = 0.0;
    Vec3 relativePosition; ///< m, from the planet's centre (ecliptic axes)
    Vec3 relativeVelocity; ///< m/s
};

/// The pass by `planet` of a ship at (r, v) heliocentric at `epoch`
/// (best at the closest approach).
PlanetPass passAt(Planet planet, double epoch, const Vec3& r, const Vec3& v);

struct VoyageResult {
    std::vector<VoyageSample> samples;
    std::vector<BurnReport> burns; ///< in the order flown
    VoyageEnd end = VoyageEnd::Horizon;
    bool goalMet = false;
    double endEpoch = 0.0;
    std::optional<double> earthArrival; ///< entered the sphere of influence
    EarthApproach closest;              ///< over the whole flight
    /// The closest the path comes to each planet (index: Planet), and each
    /// closest approach inside a planet's sphere of influence, in order.
    std::array<PlanetPass, 8> closestPasses;
    std::vector<PlanetPass> flybys;
    double entrySpeed = 0.0;     ///< m/s, at the entry interface, if reached
    double entryAngle = 0.0;     ///< radians, flight-path angle there
    double propellantLeft = 0.0; ///< kg
    double deltaVLeft = 0.0;     ///< m/s
};

VoyageResult flyVoyage(const Voyage& voyage, const VoyageOptions& options = {});

/// The state at `epoch` (flown from the start with the burns before it).
VoyageSample voyageStateAt(const Voyage& voyage, double epoch, const VoyageOptions& options = {});

} // namespace orbit
