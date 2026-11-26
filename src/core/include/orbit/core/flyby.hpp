// SPDX-License-Identifier: GPL-3.0-or-later
// Gravity assists: aiming a burn so the path passes a planet just where you
// want it to.
//
// WHY A PASS GIVES SPEED FOR FREE
// Seen from the planet, a passing ship follows a hyperbola: it leaves as
// fast as it came (v∞), only turned. Seen from the Sun the planet is
// moving, so the ship's velocity is the planet's plus v∞, and turning v∞
// changes the sum: turned towards the planet's motion (usually by passing
// behind it) the ship speeds up; turned away from it, it slows down. A pass
// can only turn v∞, so a ship that the planet catches up from behind (as at
// the end of a Hohmann transfer outwards) speeds up whichever way it turns. The energy
// comes from the planet, which slows down (or speeds up) by an amount too
// small ever to notice. This is how Voyager and Cassini reached the outer
// planets, and how Galileo and Juno used the Earth itself.
//
// THE B-PLANE
// Where the pass goes is decided long before, so it is measured on the
// "B-plane": the plane through the planet's centre square to the incoming
// path. The B-vector runs from the centre to where the incoming straight
// line (the asymptote) would cross it; its length, the impact parameter b,
// sets the closest approach r_p (from angular momentum, b v∞ = r_p v_p,
// and energy, v_p² = v∞² + 2μ/r_p):
//     b = r_p √(1 + 2μ/(r_p v∞²)),
// and its direction sets the side. Aiming is Newton's method on the two
// B-plane coordinates as functions of the burn's three components, with
// the full simulation (every planet pulling) giving the derivatives by
// finite differences; of all the burns that hit the target, it takes the
// one changed least.
#pragma once

#include "orbit/core/solar_system.hpp"
#include "orbit/core/vec3.hpp"
#include "orbit/core/voyage.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace orbit {

/// The B-plane of a path past a body of gravitational parameter μ (m³/s²),
/// from a state relative to it (m, m/s). For a hyperbola, the incoming
/// direction ŝ = ê/e + √(1 − 1/e²) (ĥ × ê) and B = ŝ × h / v∞; otherwise
/// (deep in the body's grip, or far away where the Sun dominates) the
/// straight-line miss along the current velocity.
struct BPlane {
    Vec3 s;                 ///< incoming direction
    Vec3 b;                 ///< the B-vector (m)
    double vInfinity = 0.0; ///< m/s
};
BPlane bPlane(const Vec3& r, const Vec3& v, double mu);

/// b = r_p √(1 + 2μ/(r_p v∞²)): the impact parameter that gives a closest
/// approach `periapsis` (m, from the centre) arriving at `vInfinity` (m/s).
double impactParameter(double periapsis, double vInfinity, double mu);

/// Which side of the planet to pass. The pass bends the path towards the
/// planet, so the side decides which way your motion relative to it turns.
enum class FlybySide {
    Keep, ///< the side the path passes now (the smallest change)
    /// The side that turns your motion relative to the planet most towards
    /// the planet's own motion: you gain the most speed around the Sun.
    /// Usually that means passing behind the planet.
    Faster,
    /// The opposite side: you lose the most speed (or gain the least: if
    /// the planet is catching you up from behind, every pass speeds you up).
    Slower,
    North, ///< over its north side (ecliptic north): bends your path south
    South, ///< under its south side: bends your path north
};
std::string_view flybySideName(FlybySide side);

struct FlybyAim {
    Planet planet = Planet::Jupiter;
    double periapsis = 0.0; ///< m, from the planet's centre
    FlybySide side = FlybySide::Keep;
};

/// The pass by `planet` after `after`: the first flyby inside its sphere
/// of influence, else the closest approach if it comes later than that.
std::optional<PlanetPass> passAfter(const VoyageResult& result, Planet planet, double after);

struct FlybyAimResult {
    std::optional<Burn> burn; ///< the burn that makes the pass, if found
    double passEpoch = 0.0;   ///< when the pass is, with that burn
    /// If the path wasn't heading there: the smallest burn that reaches the
    /// planet from where the burn is (Sun only, m/s; 0 if none was needed
    /// or found).
    double cheapest = 0.0;
    /// That is more than the tanks hold by then: try another date.
    bool outOfReach = false;
};

/// Changes burn `index` (in time order) so that the path passes the planet
/// as `aim` says. If the path isn't heading there (no pass within three
/// spheres of influence), it first aims the burn with Lambert's problem
/// (Sun only) at the trip time that needs the smallest burn, then refines
/// as above. Burns after it are kept. No burn if out of reach or if it
/// doesn't converge.
FlybyAimResult aimFlyby(const Voyage& voyage, std::size_t index, const FlybyAim& aim,
                        const VoyageOptions& options = {});

} // namespace orbit
