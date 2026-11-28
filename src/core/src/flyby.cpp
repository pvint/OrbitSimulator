// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/flyby.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/lost_in_space.hpp"
#include "orbit/core/two_body.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace orbit {

namespace {

constexpr double kDay = 86400.0;

/// The burn's three components (prograde, radial, normal).
using Components = std::array<double, 3>;

/// `u` with its part along `s` taken out, as a unit vector (or nothing if
/// it lies along s).
std::optional<Vec3> squareTo(const Vec3& u, const Vec3& s) {
    const Vec3 p = u - dot(u, s) * s;
    const double n = norm(p);
    if (n < 1e-6 * norm(u)) return std::nullopt;
    return p / n;
}

/// The direction in the B-plane (square to ŝ) of each side. The outgoing
/// direction is ŝ turned by δ towards −B̂ (gravity bends the path towards
/// the planet), so B̂ along −V_planet (as seen square to ŝ) turns v∞ most
/// towards the planet's motion: the most speed gained. North is ecliptic
/// north.
std::optional<Vec3> sideDirection(FlybySide side, const Vec3& s, const Vec3& planetVelocity) {
    switch (side) {
    case FlybySide::Faster:
        return squareTo(-planetVelocity, s);
    case FlybySide::Slower:
        return squareTo(planetVelocity, s);
    case FlybySide::North:
        return squareTo(Vec3{0.0, 0.0, 1.0}, s);
    case FlybySide::South:
        return squareTo(Vec3{0.0, 0.0, -1.0}, s);
    case FlybySide::Keep:
        break;
    }
    return std::nullopt;
}

/// Of the transfers from r at `epoch` to the planet, with only the Sun
/// pulling, the velocity needing the smallest change from v: trip times
/// from 20 days to 1.5 Hohmann transfers (half the period of the ellipse
/// whose ends are the ship's distance and the planet's, π √(a³/μ)), and
/// aimed to pass at impact parameter b on `side`.
struct Towards {
    Vec3 v1;       ///< velocity needed at r (m/s)
    double flight; ///< trip time (s)
};
std::optional<Towards> lambertTowards(double epoch, const Vec3& r, const Vec3& v,
                                      const FlybyAim& aim) {
    const double a = 0.5 * (norm(r) + planetOrbit(aim.planet, epoch).semiMajorAxis);
    const double longest = std::max(60.0 * kDay, 1.5 * kPi * std::sqrt(a * a * a / kMuSun));
    constexpr int kTrips = 400;
    std::optional<Vec3> best;
    double bestCost = 0.0;
    double bestFlight = 0.0;
    for (int i = 0; i <= kTrips; ++i) {
        const double flight = 20.0 * kDay + (longest - 20.0 * kDay) * i / kTrips;
        const auto solution =
            solveLambert(r, bodyPosition(aim.planet, epoch + flight), flight, kMuSun);
        if (!solution) continue;
        const double cost = norm(solution->v1 - v);
        if (!best || cost < bestCost) {
            best = solution->v1;
            bestCost = cost;
            bestFlight = flight;
        }
    }
    if (!best) return std::nullopt;

    // Aiming at the centre would fly through the planet: aim beside it
    // instead, square to the arrival direction, at the impact parameter
    // for the pass wanted (the arrival direction depends on the aim point,
    // so aim, solve, and aim again).
    const double arrival = epoch + bestFlight;
    const Vec3 planetR = bodyPosition(aim.planet, arrival);
    const Vec3 planetV = bodyVelocity(aim.planet, arrival);
    const double mu = bodyMu(aim.planet);
    Vec3 target = planetR;
    for (int i = 0; i < 3; ++i) {
        const auto solution = solveLambert(r, target, bestFlight, kMuSun);
        if (!solution) break;
        best = solution->v1;
        const Vec3 vInfinity = solution->v2 - planetV;
        const Vec3 s = normalized(vInfinity);
        const FlybySide side = aim.side == FlybySide::Keep ? FlybySide::Faster : aim.side;
        const Vec3 direction =
            sideDirection(side, s, planetV).value_or(normalized(cross(s, Vec3{0.0, 0.0, 1.0})));
        target = planetR + impactParameter(aim.periapsis, norm(vInfinity), mu) * direction;
    }
    return Towards{*best, bestFlight};
}

/// Leaving a parked orbit for the planet: of the trip times, the one whose
/// v∞ (from Lambert's problem, from the home planet's centre) is cheapest
/// to reach from the orbit's low point (the Oberth effect, departureCost).
struct Departure {
    Vec3 vInfinity;
    double cost = 0.0;
};
std::optional<Departure> departureTowards(double epoch, Body home, const TwoBodyState& rel,
                                          const FlybyAim& aim) {
    const double mu = bodyMu(home);
    const OrbitalElements el = elementsFromState(rel.r, rel.v, mu, bodyRadius(home));
    const ParkingOrbit orbit{el.periapsis, el.apoapsis};
    const Vec3 r = bodyPosition(home, epoch);
    const Vec3 v = bodyVelocity(home, epoch);
    const double a = 0.5 * (norm(r) + planetOrbit(aim.planet, epoch).semiMajorAxis);
    const double longest = std::max(60.0 * kDay, 1.5 * kPi * std::sqrt(a * a * a / kMuSun));
    constexpr int kTrips = 400;
    std::optional<Departure> best;
    for (int i = 0; i <= kTrips; ++i) {
        const double flight = 20.0 * kDay + (longest - 20.0 * kDay) * i / kTrips;
        const auto solution =
            solveLambert(r, bodyPosition(aim.planet, epoch + flight), flight, kMuSun);
        if (!solution) continue;
        const Vec3 vInfinity = solution->v1 - v;
        const double cost = departureCost(home, orbit, norm(vInfinity));
        if (!best || cost < best->cost) best = Departure{vInfinity, cost};
    }
    return best;
}

FlybyAimResult aimFlybyFrom(const Voyage& voyage, std::size_t index, const FlybyAim& aim,
                            const VoyageOptions& options, bool mayGuess);

} // namespace

BPlane bPlane(const Vec3& r, const Vec3& v, double mu) {
    const double rNorm = norm(r);
    const double v2 = normSquared(v);
    const double energy = v2 / 2.0 - mu / rNorm;
    const Vec3 h = cross(r, v);
    const Vec3 eVec = ((v2 - mu / rNorm) * r - dot(r, v) * v) / mu;
    const double e = norm(eVec);
    BPlane plane;
    if (energy <= 0.0 || e <= 1.0 || norm(h) == 0.0) {
        plane.s = normalized(v);
        plane.b = r - dot(r, plane.s) * plane.s;
        plane.vInfinity = std::sqrt(v2);
        return plane;
    }
    const Vec3 eHat = eVec / e;
    plane.s = eHat / e + std::sqrt(1.0 - 1.0 / (e * e)) * cross(normalized(h), eHat);
    plane.vInfinity = std::sqrt(2.0 * energy);
    plane.b = cross(plane.s, h) / plane.vInfinity;
    return plane;
}

double impactParameter(double periapsis, double vInfinity, double mu) {
    return periapsis * std::sqrt(1.0 + 2.0 * mu / (periapsis * vInfinity * vInfinity));
}

std::string_view flybySideName(FlybySide side) {
    switch (side) {
    case FlybySide::Keep:
        return "Keep";
    case FlybySide::Faster:
        return "Faster";
    case FlybySide::Slower:
        return "Slower";
    case FlybySide::North:
        return "North";
    case FlybySide::South:
        return "South";
    }
    return "";
}

std::optional<PlanetPass> passAfter(const VoyageResult& result, Body planet, double after) {
    for (const PlanetPass& pass : result.flybys) {
        if (pass.planet == planet && pass.epoch > after) return pass;
    }
    const PlanetPass& closest = result.closestPasses.at(static_cast<std::size_t>(planet));
    if (closest.valid && closest.turning && closest.epoch > after + kDay) return closest;
    return std::nullopt;
}

FlybyAimResult aimFlyby(const Voyage& voyage, std::size_t index, const FlybyAim& aim,
                        const VoyageOptions& options) {
    return aimFlybyFrom(voyage, index, aim, options, true);
}

namespace {

FlybyAimResult aimFlybyFrom(const Voyage& voyage, std::size_t index, const FlybyAim& aim,
                            const VoyageOptions& options, bool mayGuess) {
    FlybyAimResult out;
    std::vector<Burn> burns = voyage.burns;
    std::ranges::stable_sort(burns, {}, &Burn::epoch);
    if (index >= burns.size()) return out;
    const Burn original = burns[index];

    // Fly from just before the burn, with the burns before it already
    // done, and the ones after it kept.
    Voyage before = voyage;
    before.burns.assign(burns.begin(), burns.begin() + static_cast<std::ptrdiff_t>(index));
    const VoyageSample start = voyageStateAt(before, original.epoch, options);
    Voyage trial;
    trial.startEpoch = original.epoch;
    trial.position = start.r;
    trial.velocity = start.v;
    trial.ship = voyage.ship;
    trial.ship.propellant = std::max(0.0, start.mass - voyage.ship.dryMass);
    trial.goal = Goal::EarthOrbit; // don't stop on reaching the Earth's neighbourhood
    trial.lookAhead = voyage.lookAhead;
    trial.horizon = voyage.horizon;
    trial.burns.assign(burns.begin() + static_cast<std::ptrdiff_t>(index), burns.end());
    const BurnFrame frame = burnFrameAt(original.epoch, start.r, start.v);
    const auto withBurn = [&](const Components& x) {
        trial.burns.front() = Burn{original.epoch, x[0], x[1], x[2]};
    };

    Components x{original.prograde, original.radial, original.normal};
    VoyageOptions fly = options;
    fly.stopAtGoal = false; // fly on past arriving, to the pass
    withBurn(x);
    auto pass = passAfter(flyVoyage(trial, fly), aim.planet, original.epoch);
    const bool heading = pass && pass->distance < 3.0 * sphereOfInfluence(aim.planet, pass->epoch);
    const double available = deltaVAvailable(voyage.ship, start.mass);
    const auto parked = parkedAround(original.epoch, start.r, start.v);
    if (!heading && parked && mayGuess) {
        // In orbit around a planet: the cheapest way out towards it, and
        // the best point in the orbit to burn for that, within half an
        // orbit of the burn as placed; then aim from there.
        const TwoBodyState rel{start.r - bodyPosition(*parked, original.epoch),
                               start.v - bodyVelocity(*parked, original.epoch)};
        const auto departure = departureTowards(original.epoch, *parked, rel, aim);
        if (!departure) return out;
        out.cheapest = departure->cost;
        if (departure->cost > available) {
            out.outOfReach = true;
            return out;
        }
        const double period =
            elementsFromState(rel.r, rel.v, bodyMu(*parked), bodyRadius(*parked)).period;
        double earliest = std::max(voyage.startEpoch, original.epoch - 0.5 * period);
        if (index > 0) earliest = std::max(earliest, burns[index - 1].epoch + 1.0);
        const auto burn = departureBurn(before, earliest, departure->vInfinity, options);
        if (!burn) return out;
        Voyage moved = voyage;
        moved.burns = std::move(burns);
        moved.burns[index] = *burn;
        std::ranges::stable_sort(moved.burns, {}, &Burn::epoch);
        const auto at = std::ranges::find_if(moved.burns,
                                             [&](const Burn& b) { return b.epoch == burn->epoch; });
        FlybyAimResult aimed = aimFlybyFrom(
            moved, static_cast<std::size_t>(at - moved.burns.begin()), aim, options, false);
        aimed.cheapest = departure->cost;
        return aimed;
    }
    if (!heading && !parked) {
        // Not heading there yet: aim with Lambert's problem first, if the
        // tanks hold enough (v_e ln(m / m_dry)).
        const auto v1 = lambertTowards(original.epoch, start.r, start.v, aim);
        if (!v1) return out;
        out.cheapest = norm(v1->v1 - start.v);
        if (out.cheapest > available) {
            out.outOfReach = true;
            return out;
        }
        const Burn guess = burnFromVector(original.epoch, v1->v1 - start.v, frame);
        x = {guess.prograde, guess.radial, guess.normal};
        withBurn(x);
        // Fly long enough to get there, whatever the usual look-ahead.
        fly.stopAt = original.epoch + 1.3 * v1->flight + 60.0 * kDay;
        pass = passAfter(flyVoyage(trial, fly), aim.planet, original.epoch);
    }
    if (!pass) return out;
    // From now on, fly only until a month past the pass.
    fly.stopAt = pass->epoch + 30.0 * kDay;

    // The B-plane's axes, fixed from this flight so the target doesn't move
    // under Newton's feet: T square to the incoming direction in the
    // ecliptic, R completing the set.
    const double mu = bodyMu(aim.planet);
    const BPlane plane0 = bPlane(pass->relativePosition, pass->relativeVelocity, mu);
    const Vec3 axisT = normalized(cross(plane0.s, Vec3{0.0, 0.0, 1.0}));
    const Vec3 axisR = cross(plane0.s, axisT);
    const double bNow = norm(plane0.b);
    Vec3 direction = axisT;
    if (const auto side =
            sideDirection(aim.side, plane0.s, bodyVelocity(aim.planet, pass->epoch))) {
        direction = *side;
    } else if (bNow > 1000.0) {
        direction = plane0.b / bNow; // keep the side it passes on now
    }
    const double dirT = dot(direction, axisT);
    const double dirR = dot(direction, axisR);

    // The miss for a burn x: (B·T, B·R) minus the target, whose size
    // follows the pass's own v∞ (it changes a little as the aim does).
    struct Miss {
        std::array<double, 2> f;
        double target;
    };
    const auto miss = [&](const Components& c) -> std::optional<Miss> {
        withBurn(c);
        const auto p = passAfter(flyVoyage(trial, fly), aim.planet, original.epoch);
        if (!p) return std::nullopt;
        out.passEpoch = p->epoch;
        const BPlane plane = bPlane(p->relativePosition, p->relativeVelocity, mu);
        const double b = impactParameter(aim.periapsis, plane.vInfinity, mu);
        return Miss{{dot(plane.b, axisT) - b * dirT, dot(plane.b, axisR) - b * dirR}, b};
    };
    const auto size = [](const Miss& m) { return std::hypot(m.f[0], m.f[1]); };

    constexpr double kStep = 0.01; // m/s, for the finite differences
    auto current = miss(x);
    if (!current) return out;
    for (int iteration = 0; iteration < 25; ++iteration) {
        if (size(*current) < 1e-3 * current->target + 100.0) {
            out.burn = Burn{original.epoch, x[0], x[1], x[2]};
            return out;
        }
        // The Jacobian J (2×3): how the miss moves per m/s in each direction.
        std::array<std::array<double, 3>, 2> jac{};
        for (std::size_t k = 0; k < 3; ++k) {
            Components xk = x;
            xk[k] += kStep;
            const auto fk = miss(xk);
            if (!fk) return out;
            jac[0][k] = (fk->f[0] - current->f[0]) / kStep;
            jac[1][k] = (fk->f[1] - current->f[1]) / kStep;
        }
        // The smallest change that cancels the miss, to first order:
        // Δx = Jᵀ (J Jᵀ)⁻¹ (−f).
        const double a = jac[0][0] * jac[0][0] + jac[0][1] * jac[0][1] + jac[0][2] * jac[0][2];
        const double b = jac[0][0] * jac[1][0] + jac[0][1] * jac[1][1] + jac[0][2] * jac[1][2];
        const double d = jac[1][0] * jac[1][0] + jac[1][1] * jac[1][1] + jac[1][2] * jac[1][2];
        const double det = a * d - b * b;
        if (std::abs(det) < 1e-30) return out;
        const double y0 = (-d * current->f[0] + b * current->f[1]) / det;
        const double y1 = (b * current->f[0] - a * current->f[1]) / det;
        Components dx{};
        for (std::size_t k = 0; k < 3; ++k) dx[k] = jac[0][k] * y0 + jac[1][k] * y1;

        // Far from the answer the first-order step can overshoot (a close
        // pass bends the path a lot): halve it until the miss shrinks.
        bool improved = false;
        double scale = 1.0;
        for (int halving = 0; halving < 6 && !improved; ++halving, scale *= 0.5) {
            Components xNew = x;
            for (std::size_t k = 0; k < 3; ++k) xNew[k] += scale * dx[k];
            const auto next = miss(xNew);
            if (next && size(*next) < size(*current)) {
                x = xNew;
                current = next;
                improved = true;
            }
        }
        if (!improved) return out;
    }
    return out;
}

} // namespace

} // namespace orbit
