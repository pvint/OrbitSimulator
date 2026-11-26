// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/moon_transfer.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/flyby.hpp"
#include "orbit/core/orbit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace orbit {

namespace {

constexpr double kDay = 86400.0;
/// About this many Lambert solutions in a search (each a few microseconds).
constexpr double kLambertBudget = 150000.0;
/// Departures start this long after the moment planned from, so the burn
/// is still to come.
constexpr double kFirstDeparture = 60.0; ///< s
/// No leg shorter than this (s).
constexpr double kShortestLeg = 1800.0;

/// The ship's flight time along a path coming in: how long its distance
/// takes to cover at its speed (s).
double fallTime(const TwoBodyState& rel) { return norm(rel.r) / std::max(1.0, norm(rel.v)); }

/// Of the two ways round, the Lambert solution needing the smaller change
/// from v.
std::optional<LambertSolution> cheaperWayRound(const Vec3& r1, const Vec3& r2, double dt, double mu,
                                               const Vec3& v) {
    std::optional<LambertSolution> best;
    for (const bool prograde : {true, false}) {
        const auto solution = solveLambert(r1, r2, dt, mu, prograde);
        if (solution && (!best || norm(solution->v1 - v) < norm(best->v1 - v))) best = solution;
    }
    return best;
}

std::vector<Burn> burnsBefore(const Voyage& voyage, double epoch) {
    std::vector<Burn> burns = voyage.burns;
    std::erase_if(burns, [&](const Burn& b) { return b.epoch >= epoch; });
    return burns;
}

} // namespace

double moonDistance(Body moon, double epoch) { return norm(moonFromPlanet(moon, epoch).r); }

Vec3 moonOrbitNormal(Body moon, double epoch) {
    const MoonState m = moonFromPlanet(moon, epoch);
    return normalized(cross(m.r, m.v));
}

double moonApproachSpeed(double vInfinity, Body moon) {
    // Energy around the planet at the moon's distance a (vis-viva for the
    // hyperbola: v² = v∞² + 2μ/a), against the moon's own circular speed.
    const double mu = bodyMu(planetOf(moon));
    const double a = moonOrbit(moon).semiMajorAxis;
    return std::abs(std::sqrt(vInfinity * vInfinity + 2.0 * mu / a) - std::sqrt(mu / a));
}

std::optional<TransferPlan> cheapestMoonTransfer(double epoch, const TwoBodyState& relative,
                                                 Body moon, Goal goal, double window) {
    if (!isMoon(moon)) return std::nullopt;
    const double mu = bodyMu(planetOf(moon));
    const double aMoon = moonOrbit(moon).semiMajorAxis;
    const double moonPeriod = kTwoPi * std::sqrt(aMoon * aMoon * aMoon / mu);
    const OrbitalElements el = elementsFromState(relative.r, relative.v, mu, 0.0);

    // The trip times to try. From a closed orbit: around a Hohmann
    // transfer's (half the ellipse between the ship's distance and the
    // moon's: π √(a³/μ), Kepler's third law), from a third of the one from
    // the orbit's low point to twice the one from its high point (a long
    // ellipse may best leave from far out). Coming in from outside: up to
    // the time to fall in and a moon's period more, so the moon can be
    // anywhere round its orbit.
    double shortest = 0.0;
    double longest = 0.0;
    if (el.bound) {
        const auto hohmann = [&](double from) {
            const double a = 0.5 * (from + aMoon);
            return kPi * std::sqrt(a * a * a / mu);
        };
        shortest = 0.3 * hohmann(el.periapsis);
        longest = 2.0 * hohmann(el.apoapsis);
    } else {
        shortest = kShortestLeg;
        longest = fallTime(relative) + moonPeriod;
    }
    shortest = std::max(shortest, kShortestLeg);
    // Steps short enough that the moon moves less than a sixteenth of its
    // orbit between two trip times.
    const double flightStep = std::min((longest - shortest) / 40.0, moonPeriod / 16.0);
    const int flights = std::max(1, static_cast<int>((longest - shortest) / flightStep));
    // Departures: 72 points round a closed orbit (or 400 over the window),
    // fewer if that would mean too many solutions.
    double departureStep = el.bound ? el.period / 72.0 : window / 400.0;
    const double most = kLambertBudget / (2.0 * (flights + 1));
    departureStep = std::max(departureStep, window / most);
    const int departures = std::max(1, static_cast<int>(window / departureStep));

    std::optional<TransferPlan> best;
    const auto consider = [&](double departure, double flight) {
        if (departure < epoch + kFirstDeparture || flight < kShortestLeg) return;
        const TwoBodyState ship = propagateKepler(relative, departure - epoch, mu);
        const double arrival = departure + flight;
        const MoonState target = moonFromPlanet(moon, arrival);
        for (const bool prograde : {true, false}) {
            const auto lambert = solveLambert(ship.r, target.r, flight, mu, prograde);
            if (!lambert) continue;
            TransferPlan plan;
            plan.departure = departure;
            plan.arrival = arrival;
            plan.departureDeltaV = lambert->v1 - ship.v;
            plan.departureCost = norm(plan.departureDeltaV);
            plan.vInfinity = norm(lambert->v2 - target.v);
            plan.arrivalCost = arrivalCost(goal, plan.vInfinity, moon);
            if (!best || plan.total() < best->total()) best = plan;
        }
    };
    // The porkchop again, a coarse grid first...
    for (int i = 0; i <= departures; ++i) {
        const double departure = epoch + kFirstDeparture + i * departureStep;
        for (int j = 0; j <= flights; ++j) consider(departure, shortest + j * flightStep);
    }
    if (!best) return std::nullopt;
    // ...then a finer one around its lowest point.
    const TransferPlan coarse = *best;
    const double coarseFlight = coarse.arrival - coarse.departure;
    constexpr int kFine = 10;
    for (int i = -kFine; i <= kFine; ++i) {
        for (int j = -kFine; j <= kFine; ++j) {
            consider(coarse.departure + departureStep * i / kFine,
                     coarseFlight + flightStep * j / kFine);
        }
    }
    return best;
}

namespace {

/// Flying from a burn to `arrival` (with no burns after it), where the ship
/// ends up, and the voyage that flies it.
struct Homing {
    Voyage trial;
    VoyageOptions fly;
    std::optional<VoyageSample> at(const Burn& burn) {
        trial.burns = {burn};
        const VoyageResult result = flyVoyage(trial, fly);
        if (result.end != VoyageEnd::StopTime || result.samples.empty()) return std::nullopt;
        return result.samples.back();
    }
};

Homing homingFor(const Voyage& voyage, const Burn& burn, double arrival,
                 const VoyageOptions& options) {
    Voyage before = voyage;
    before.burns = burnsBefore(voyage, burn.epoch);
    const VoyageSample start = voyageStateAt(before, burn.epoch, options);
    Homing h;
    h.trial.startEpoch = burn.epoch;
    h.trial.position = start.r;
    h.trial.velocity = start.v;
    h.trial.ship = voyage.ship;
    h.trial.ship.propellant = std::max(0.0, start.mass - voyage.ship.dryMass);
    h.trial.goal = Goal::EarthOrbit;
    h.fly = options;
    h.fly.stopAt = arrival;
    h.fly.stopAtGoal = false;
    return h;
}

/// Where to pass beside the moon: square to the arrival direction ŝ (the
/// velocity relative to the moon), at the impact parameter for the pass
/// the goal wants, on the side that takes the path round the moon the way
/// it goes round its planet (B along ŝ × pole).
Vec3 besideMoon(Body moon, Goal goal, double epoch, const Vec3& relativeVelocity) {
    const Vec3 s = normalized(relativeVelocity);
    Vec3 side = cross(s, moonOrbitNormal(moon, epoch));
    if (norm(side) < 1e-6) side = cross(s, Vec3{0.0, 0.0, 1.0});
    const double b =
        impactParameter(aimPeriapsis(goal, moon), norm(relativeVelocity), bodyMu(moon));
    return b * normalized(side);
}

/// Newton's method with the full simulation: changes the burn until the
/// ship is at the moon's position plus `offset` at `arrival` (three
/// equations, the miss in x, y, z, for three unknowns, the burn's
/// components; the derivatives by finite differences). Every pull counts
/// here, the Sun's tide and the other moons' included, which the planning
/// with the planet alone left out. Nothing if it doesn't get within reach
/// of the moon.
std::optional<Burn> homeIn(const Voyage& voyage, const Burn& guess, double arrival, Body moon,
                           const Vec3& offset, const VoyageOptions& options) {
    Homing homing = homingFor(voyage, guess, arrival, options);
    const Vec3 target = bodyPosition(moon, arrival) + offset;
    using Components = std::array<double, 3>;
    const auto burnOf = [&](const Components& x) { return Burn{guess.epoch, x[0], x[1], x[2]}; };
    const auto miss = [&](const Components& x) -> std::optional<Vec3> {
        const auto at = homing.at(burnOf(x));
        if (!at) return std::nullopt;
        return at->r - target;
    };
    // Close enough for the B-plane aim (flyby.hpp) to take over.
    const double soi = sphereOfInfluence(moon, arrival);
    const double tolerance = std::min(0.5 * soi, std::max(0.3 * norm(offset), 50e3));
    Components x{guess.prograde, guess.radial, guess.normal};
    auto f = miss(x);
    constexpr double kStep = 0.01; // m/s
    for (int iteration = 0; iteration < 12 && f; ++iteration) {
        if (norm(*f) < tolerance) return burnOf(x);
        // The Jacobian, column k: how the miss moves per m/s along k.
        std::array<Vec3, 3> column{};
        for (std::size_t k = 0; k < 3; ++k) {
            Components xk = x;
            xk[k] += kStep;
            const auto fk = miss(xk);
            if (!fk) return std::nullopt;
            column.at(k) = (*fk - *f) / kStep;
        }
        // Solve J Δx = −f by Cramer's rule: Δx_k = det(J with column k
        // replaced by −f) / det J, with det [a b c] = a · (b × c).
        const auto det = [](const Vec3& a, const Vec3& b, const Vec3& c) {
            return dot(a, cross(b, c));
        };
        const double d = det(column[0], column[1], column[2]);
        if (std::abs(d) < 1e-30) return std::nullopt;
        const Vec3 rhs = -*f;
        const Components dx{det(rhs, column[1], column[2]) / d, det(column[0], rhs, column[2]) / d,
                            det(column[0], column[1], rhs) / d};
        // Far off, the first-order step can overshoot: halve it until the
        // miss shrinks.
        bool improved = false;
        double scale = 1.0;
        for (int halving = 0; halving < 8 && !improved; ++halving, scale *= 0.5) {
            Components xNew = x;
            for (std::size_t k = 0; k < 3; ++k) xNew.at(k) += scale * dx.at(k);
            const auto next = miss(xNew);
            if (next && norm(*next) < norm(*f)) {
                x = xNew;
                f = next;
                improved = true;
            }
        }
        if (!improved) break;
    }
    if (f && norm(*f) < 2.0 * soi) return burnOf(x);
    return std::nullopt;
}

/// The leg as planned: the burn, and the flight it gives (speed relative
/// to the moon on arrival, and what the goal costs there).
MoonLeg legFor(const Voyage& voyage, double from, const Burn& burn, double arrival, Body moon,
               const VoyageOptions& options) {
    MoonLeg leg;
    leg.from = from;
    leg.burn = burn;
    leg.plan.departure = burn.epoch;
    leg.plan.arrival = arrival;
    leg.plan.departureCost = burnSize(burn);
    Homing homing = homingFor(voyage, burn, arrival, options);
    if (const auto at = homing.at(burn)) {
        leg.plan.vInfinity = norm(at->v - bodyVelocity(moon, arrival));
        leg.plan.arrivalCost = arrivalCost(voyage.goal, leg.plan.vInfinity, moon);
    }
    return leg;
}

/// In orbit around the planet: the cheapest transfer (planet only), aimed
/// beside the moon, then homed in with the full simulation.
std::optional<MoonLeg> legFromOrbit(const Voyage& voyage, double now, const TwoBodyState& rel,
                                    const VoyageOptions& options) {
    const Body moon = voyage.destination;
    const Body planet = planetOf(moon);
    const double mu = bodyMu(planet);
    // How long to look for a departure: the next time round, unless
    // waiting up to half the moon's period (the moon crosses the plane of
    // the ship's orbit twice each time round) saves a good deal.
    const double aMoon = moonOrbit(moon).semiMajorAxis;
    const double moonPeriod = kTwoPi * std::sqrt(aMoon * aMoon * aMoon / mu);
    const OrbitalElements el = elementsFromState(rel.r, rel.v, mu, 0.0);
    auto plan = cheapestMoonTransfer(now, rel, moon, voyage.goal, 1.1 * el.period);
    const double longer = std::min(0.5 * moonPeriod, 60.0 * kDay);
    if (longer > 1.1 * el.period) {
        const auto later = cheapestMoonTransfer(now, rel, moon, voyage.goal, longer);
        if (later && (!plan || later->total() < 0.97 * plan->total() - 20.0)) plan = later;
    }
    if (!plan) return std::nullopt;

    // Aim beside the moon rather than at its centre; the arrival direction
    // depends on the aim point, so aim, solve, aim again.
    const double flight = plan->arrival - plan->departure;
    const TwoBodyState ship = propagateKepler(rel, plan->departure - now, mu);
    const MoonState target = moonFromPlanet(moon, plan->arrival);
    Vec3 offset;
    std::optional<LambertSolution> lambert;
    for (int i = 0; i < 3; ++i) {
        const auto solution = cheaperWayRound(ship.r, target.r + offset, flight, mu, ship.v);
        if (!solution) break;
        lambert = solution;
        offset = besideMoon(moon, voyage.goal, plan->arrival, solution->v2 - target.v);
    }
    if (!lambert) return std::nullopt;
    const Vec3 r0 = bodyPosition(planet, plan->departure) + ship.r;
    const Vec3 v0 = bodyVelocity(planet, plan->departure) + ship.v;
    const Burn guess =
        burnFromVector(plan->departure, lambert->v1 - ship.v, burnFrameAt(plan->departure, r0, v0));
    Voyage kept = voyage;
    kept.burns = burnsBefore(voyage, now);
    const Burn homed = homeIn(kept, guess, plan->arrival, moon, offset, options).value_or(guess);
    return legFor(kept, now, homed, plan->arrival, moon, options);
}

} // namespace

std::optional<MoonLeg> planToMoon(const Voyage& voyage, double now, const VoyageResult& prediction,
                                  const VoyageOptions& options) {
    const Body moon = voyage.destination;
    if (!isMoon(moon)) return std::nullopt;
    const Body planet = planetOf(moon);
    const double mu = bodyMu(planet);
    const auto fromPlanet = [&](double t, const Vec3& r) { return r - bodyPosition(planet, t); };

    // In orbit around the planet: a transfer from the orbit.
    Voyage before = voyage;
    before.burns = burnsBefore(voyage, now);
    const VoyageSample here = voyageStateAt(before, now, options);
    const TwoBodyState rel{fromPlanet(now, here.r), here.v - bodyVelocity(planet, now)};
    const bool inside = norm(rel.r) < sphereOfInfluence(planet, now);
    if (inside && elementsFromState(rel.r, rel.v, mu, 0.0).bound) {
        return legFromOrbit(voyage, now, rel, options);
    }

    // Passing through the planet's system: a correction, a month before
    // the path crosses the moon's orbit (or reaches its closest to the
    // planet, if it never gets that deep), or now if that's sooner. A month
    // out, a few m/s moves the arrival by thousands of km; once there,
    // meeting a moon on the wrong side of the planet takes km/s.
    const double aMoon = moonOrbit(moon).semiMajorAxis;
    const double moonPeriod = kTwoPi * std::sqrt(aMoon * aMoon * aMoon / mu);
    std::optional<double> crossing;
    for (const VoyageSample& s : prediction.samples) {
        if (s.epoch > now && norm(fromPlanet(s.epoch, s.r)) < aMoon) {
            crossing = s.epoch;
            break;
        }
    }
    const PlanetPass& pass = prediction.closestPasses.at(static_cast<std::size_t>(planet));
    if (!crossing && pass.valid && pass.epoch > now &&
        pass.distance < sphereOfInfluence(planet, pass.epoch)) {
        crossing = pass.epoch;
    }
    constexpr double kCorrectionLead = 30.0 * kDay;
    if (!crossing || *crossing < now + 3600.0) return std::nullopt;
    const double burnAt = std::max(now + kFirstDeparture, *crossing - kCorrectionLead);

    // When to meet the moon: of the moments from a quarter of the way to
    // the crossing to half the moon's period past it, the one where the
    // moon is nearest to where the ship would be anyway, for the time left
    // to close the gap. The gap over the time is the burn, roughly (a
    // straight line; Newton's method does the rest).
    const double first = burnAt + 0.25 * (*crossing - burnAt);
    const double last = std::min(*crossing + 0.5 * moonPeriod, prediction.endEpoch);
    if (last <= first) return std::nullopt;
    const double step = std::min(moonPeriod / 48.0, (last - first) / 50.0);
    const auto steps = static_cast<int>((last - first) / step);
    double arrival = first;
    Vec3 gap;
    double bestRate = -1.0;
    for (int i = 0; i <= steps; ++i) {
        const double t = first + i * step;
        const Vec3 g = bodyPosition(moon, t) - sampleAt(prediction, t).r;
        const double rate = norm(g) / (t - burnAt);
        if (bestRate < 0.0 || rate < bestRate) {
            bestRate = rate;
            arrival = t;
            gap = g;
        }
    }
    Voyage kept = voyage;
    kept.burns = burnsBefore(voyage, burnAt);
    const VoyageSample at = voyageStateAt(kept, burnAt, options);
    const Burn guess =
        burnFromVector(burnAt, gap / (arrival - burnAt), burnFrameAt(burnAt, at.r, at.v));
    // The side to pass on, from where the guess arrives.
    Homing homing = homingFor(kept, guess, arrival, options);
    const auto there = homing.at(guess);
    if (!there) return std::nullopt;
    const Vec3 offset =
        besideMoon(moon, voyage.goal, arrival, there->v - bodyVelocity(moon, arrival));
    const auto homed = homeIn(kept, guess, arrival, moon, offset, options);
    if (!homed) return std::nullopt;
    return legFor(kept, burnAt, *homed, arrival, moon, options);
}

} // namespace orbit
