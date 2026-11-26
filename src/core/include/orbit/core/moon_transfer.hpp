// SPDX-License-Identifier: GPL-3.0-or-later
// Getting to a moon: the last leg of a trip, inside its planet's sphere of
// influence.
//
// PATCHED CONICS, TWICE
// A trip to Titan is planned in two pieces, as mission designers do. Far
// from Saturn only the Sun matters, and the transfer is aimed at Saturn
// (lost_in_space.hpp); the moon is too close to its planet to tell apart
// at that scale. Inside Saturn's sphere of influence only Saturn matters,
// and the ship falls along a hyperbola round it; Titan is then a target
// moving on a circle around Saturn. From an orbit around the planet, the
// leg to the moon is another Lambert problem, this time with Saturn's μ
// instead of the Sun's. Last, inside Titan's own sphere, Titan's pull
// bends the path (the B-plane aim, flyby.hpp). Each piece is a two-body
// problem; the real flight (voyage.hpp) has every pull at once, so each
// plan is then corrected with the full simulation (Newton's method).
//
// TIMING
// Aimed at Saturn from the Earth, the path crosses Titan's orbit at some
// moment, and Titan is somewhere else round it then. Put right once there,
// that would take km/s; a month before, a small push changes where and
// when the path arrives by as much as needed: the earlier a correction,
// the cheaper (the same reason missions correct their course early).
//
// ARRIVING AT A MOON'S ORBIT
// Coming in from far away with speed v∞ relative to the planet, energy
// around the planet gives the speed at the moon's distance a:
//     v = √(v∞² + 2μ/a)
// and the moon itself moves at v_m = √(μ/a) (a circle). At best the path
// grazes the moon's orbit at its low point, moving the same way as the
// moon, and meets it at v − v_m: a planet's gravity makes every arrival
// fast. From the Earth to Saturn the direct way (v∞ about 5.4 km/s), that
// is nearly 4 km/s at Titan. Once in orbit around Saturn, Cassini used
// Titan's gravity, pass after pass over 13 years, rather than its fuel to
// shape its orbit.
#pragma once

#include "orbit/core/lost_in_space.hpp"
#include "orbit/core/solar_system.hpp"
#include "orbit/core/two_body.hpp"
#include "orbit/core/vec3.hpp"
#include "orbit/core/voyage.hpp"

#include <optional>

namespace orbit {

/// The speed relative to a moon (m/s) on reaching its orbit from far away
/// at v∞ (m/s, relative to its planet), at best: |√(v∞² + 2μ/a) − √(μ/a)|.
double moonApproachSpeed(double vInfinity, Body moon);

/// How far a moon is from its planet's centre at the epoch (m).
double moonDistance(Body moon, double epoch);

/// The pole of a moon's orbit (unit vector along r × v, ecliptic axes).
Vec3 moonOrbitNormal(Body moon, double epoch);

/// The cheapest transfer to a moon from a ship at `relative` (relative to
/// the moon's planet, at `epoch`), leaving within `window` seconds, with
/// only the planet pulling: Lambert's problem with the planet's μ, both
/// ways round, from each departure (the ship moved by Kepler's equations)
/// to where the moon will be. The departure costs |v₁ − v|; the arrival,
/// arrivalCost() for the goal at the speed relative to the moon. Nothing
/// if none was found.
std::optional<TransferPlan> cheapestMoonTransfer(double epoch, const TwoBodyState& relative,
                                                 Body moon, Goal goal, double window);

/// The leg to the voyage's destination moon.
struct MoonLeg {
    double from = 0.0; ///< planned from here: burns at or after it are replaced
    /// The burn, homed in with the full simulation to pass beside the moon
    /// (to be fine-tuned by aimAtDestination, on the B-plane).
    Burn burn;
    /// Its departure and arrival, cost, speed relative to the moon on
    /// arrival (vInfinity) and what the goal costs there.
    TransferPlan plan;
};

/// For a voyage to a moon, the leg to it:
///   in orbit around its planet: the cheapest transfer from the orbit
///     (cheapestMoonTransfer), in the next orbit unless waiting up to half
///     the moon's period saves over 3%;
///   on a path through the planet's system (now, or later on the path
///     `prediction` flew): a correction a month before the path crosses
///     the moon's orbit (or now, if sooner), timed to meet the moon where
///     the gap to close per second left is smallest.
/// Nothing if the path doesn't get into the planet's system, the
/// destination isn't a moon, or the aim doesn't converge.
std::optional<MoonLeg> planToMoon(const Voyage& voyage, double now, const VoyageResult& prediction,
                                  const VoyageOptions& options = {});

} // namespace orbit
