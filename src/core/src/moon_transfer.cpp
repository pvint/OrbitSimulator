// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/moon_transfer.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/ephemeris.hpp"
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

namespace {

double periodOf(Body moon) {
    const double a = moonOrbit(moon).semiMajorAxis;
    return kTwoPi * std::sqrt(a * a * a / bodyMu(planetOf(moon)));
}

/// Where the ship leaves from, inside a planet's system: its own orbit
/// around the planet (moved by Kepler's equations, paying |v₁ − v|), or a
/// moon it is parked around (where the moon is, paying departureCost out
/// of the moon's gravity for |v₁ − v_moon|).
struct LegStart {
    TwoBodyState relative; ///< the ship (relative to the planet) at the search's epoch
    std::optional<Body> moon;
    ParkingOrbit orbit; ///< around the moon
};

/// The porkchop inside a planet's system: Lambert's problem with the
/// planet's μ, both ways round, from each departure to where the moon will
/// be. Each second of waiting counts as `waitCost` m/s more, when choosing
/// (so of two nearly equal ways, the sooner wins).
std::optional<TransferPlan> cheapestLeg(double epoch, const LegStart& start, Body moon, Goal goal,
                                        double window, double waitCost = 0.0) {
    if (!isMoon(moon)) return std::nullopt;
    const double mu = bodyMu(planetOf(moon));
    const double aMoon = moonOrbit(moon).semiMajorAxis;
    const double period = periodOf(moon);
    const auto shipAt = [&](double t) -> TwoBodyState {
        if (start.moon) {
            const MoonState m = moonFromPlanet(*start.moon, t);
            return {m.r, m.v};
        }
        return propagateKepler(start.relative, t - epoch, mu);
    };
    const auto costOf = [&](const Vec3& deltaV) {
        return start.moon ? departureCost(*start.moon, start.orbit, norm(deltaV)) : norm(deltaV);
    };
    const TwoBodyState relative = shipAt(epoch);
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
        longest = fallTime(relative) + period;
    }
    shortest = std::max(shortest, kShortestLeg);
    // Steps short enough that the moon moves less than a sixteenth of its
    // orbit between two trip times.
    const double flightStep = std::min((longest - shortest) / 40.0, period / 16.0);
    const int flights = std::max(1, static_cast<int>((longest - shortest) / flightStep));
    // Departures: 72 points round a closed orbit (or 400 over the window),
    // fewer if that would mean too many solutions.
    double departureStep = el.bound ? el.period / 72.0 : window / 400.0;
    const double most = kLambertBudget / (2.0 * (flights + 1));
    departureStep = std::max(departureStep, window / most);
    const int departures = std::max(1, static_cast<int>(window / departureStep));

    std::optional<TransferPlan> best;
    const auto score = [&](const TransferPlan& plan) {
        return plan.total() + waitCost * (plan.departure - epoch);
    };
    const auto consider = [&](double departure, double flight) {
        if (departure < epoch + kFirstDeparture || flight < kShortestLeg) return;
        const TwoBodyState ship = shipAt(departure);
        const double arrival = departure + flight;
        const MoonState target = moonFromPlanet(moon, arrival);
        for (const bool prograde : {true, false}) {
            const auto lambert = solveLambert(ship.r, target.r, flight, mu, prograde);
            if (!lambert) continue;
            TransferPlan plan;
            plan.departure = departure;
            plan.arrival = arrival;
            plan.departureDeltaV = lambert->v1 - ship.v;
            plan.departureCost = costOf(plan.departureDeltaV);
            plan.vInfinity = norm(lambert->v2 - target.v);
            plan.arrivalCost = arrivalCost(goal, plan.vInfinity, moon);
            if (!best || score(plan) < score(*best)) best = plan;
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

} // namespace

std::optional<TransferPlan> cheapestMoonTransfer(double epoch, const TwoBodyState& relative,
                                                 Body moon, Goal goal, double window) {
    return cheapestLeg(epoch, {relative, std::nullopt, {}}, moon, goal, window);
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

/// The velocity a path will leave a body with, far from it (relative to
/// it): v∞ along the hyperbola's outgoing asymptote,
/// ŝ_out = −ê/e + √(1 − 1/e²) (ĥ × ê). Nothing if it isn't escaping.
std::optional<Vec3> leavingVelocity(const Vec3& r, const Vec3& v, double mu) {
    const double rNorm = norm(r);
    const double v2 = normSquared(v);
    const double energy = v2 / 2.0 - mu / rNorm;
    const Vec3 h = cross(r, v);
    if (energy <= 0.0 || norm(h) == 0.0) return std::nullopt;
    const Vec3 eVec = ((v2 - mu / rNorm) * r - dot(r, v) * v) / mu;
    const double e = norm(eVec);
    if (e <= 1.0) return std::nullopt;
    const Vec3 eHat = eVec / e;
    const Vec3 out = -eHat / e + std::sqrt(1.0 - 1.0 / (e * e)) * cross(normalized(h), eHat);
    return std::sqrt(2.0 * energy) * out;
}

/// Newton's method with the full simulation once more, the other way out:
/// changes a burn leaving a moon until the path climbs out of the planet's
/// gravity with velocity `vInfinity` relative to it, read off its
/// hyperbola around the planet once well clear of the moons (the farther
/// of 1.5 times the moon's distance and 0.3 of the sphere of influence).
/// Planned with one body at a time, the escape comes out a little off (the
/// moon still pulls as the ship climbs away from it, the planet already
/// does), and far from the planet, a few m/s off is a planet missed by
/// millions of km. Nothing if it doesn't converge.
std::optional<Burn> homeOut(const Voyage& voyage, const Burn& guess, Body moon,
                            const Vec3& vInfinity, const VoyageOptions& options) {
    const Body planet = planetOf(moon);
    const double mu = bodyMu(planet);
    const double clear =
        std::max(1.5 * moonOrbit(moon).semiMajorAxis, 0.3 * sphereOfInfluence(planet, guess.epoch));
    // When the path gets that far: flown once to find out (it climbs at
    // more than v∞ all the way, so twice the straight-line time is ample).
    const double ample = 2.0 * clear / std::max(norm(vInfinity), 300.0);
    Homing probe = homingFor(voyage, guess, guess.epoch + ample, options);
    probe.trial.burns = {guess};
    const VoyageResult flown = flyVoyage(probe.trial, probe.fly);
    std::optional<double> stop;
    for (const VoyageSample& s : flown.samples) {
        if (s.epoch > guess.epoch && norm(s.r - bodyPosition(planet, s.epoch)) > clear) {
            stop = s.epoch;
            break;
        }
    }
    if (!stop) return std::nullopt;

    Homing homing = homingFor(voyage, guess, *stop, options);
    const Vec3 planetR = bodyPosition(planet, *stop);
    const Vec3 planetV = bodyVelocity(planet, *stop);
    using Components = std::array<double, 3>;
    const auto burnOf = [&](const Components& x) { return Burn{guess.epoch, x[0], x[1], x[2]}; };
    const auto miss = [&](const Components& x) -> std::optional<Vec3> {
        const auto at = homing.at(burnOf(x));
        if (!at) return std::nullopt;
        const auto out = leavingVelocity(at->r - planetR, at->v - planetV, mu);
        if (!out) return std::nullopt;
        return *out - vInfinity;
    };
    constexpr double kTolerance = 0.5; // m/s
    constexpr double kStep = 0.01;     // m/s
    Components x{guess.prograde, guess.radial, guess.normal};
    auto f = miss(x);
    for (int iteration = 0; iteration < 12 && f; ++iteration) {
        if (norm(*f) < kTolerance) return burnOf(x);
        std::array<Vec3, 3> column{};
        for (std::size_t k = 0; k < 3; ++k) {
            Components xk = x;
            xk.at(k) += kStep;
            const auto fk = miss(xk);
            if (!fk) return std::nullopt;
            column.at(k) = (*fk - *f) / kStep;
        }
        // J Δx = −f, by Cramer's rule as in homeIn.
        const auto det = [](const Vec3& a, const Vec3& b, const Vec3& c) {
            return dot(a, cross(b, c));
        };
        const double d = det(column[0], column[1], column[2]);
        if (std::abs(d) < 1e-30) return std::nullopt;
        const Vec3 rhs = -*f;
        const Components dx{det(rhs, column[1], column[2]) / d, det(column[0], rhs, column[2]) / d,
                            det(column[0], column[1], rhs) / d};
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
    if (f && norm(*f) < 20.0 * kTolerance) return burnOf(x);
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

namespace {

/// The places a moon takes round its planet over one period, evenly
/// spaced in time (relative to the planet).
std::vector<TwoBodyState> moonPlaces(Body moon, double epoch, int count) {
    const double period = periodOf(moon);
    std::vector<TwoBodyState> places;
    places.reserve(static_cast<std::size_t>(count));
    for (int k = 0; k < count; ++k) {
        const MoonState m = moonFromPlanet(moon, epoch + period * k / count);
        places.push_back({m.r, m.v});
    }
    return places;
}

/// Home to the planet (the Earth) from its moon: leave the moon backwards,
/// so that relative to the planet the ship is left slower than the moon,
/// at the high point of half an ellipse whose low point is the perigee the
/// goal wants. Vis-viva at the high point r_m of an ellipse down to r_p:
/// v² = 2μ r_p / (r_m (r_m + r_p)); the trip is half its period.
std::optional<MoonDeparture> fallHome(Body moon, const ParkingOrbit& orbit, double epoch,
                                      Goal goal) {
    const Body planet = planetOf(moon);
    if (planet != Body::Earth) return std::nullopt; // (only the Earth has air to land in here)
    const double mu = bodyMu(planet);
    const double leave = epoch + kFirstDeparture;
    const MoonState m = moonFromPlanet(moon, leave);
    const double rm = norm(m.r);
    const Vec3 rHat = m.r / rm;
    const Vec3 along = normalized(m.v - dot(m.v, rHat) * rHat);
    // The perigee wanted depends on the path's energy, −μ/(r_m + r_p),
    // which depends on the perigee: a few rounds settle it.
    double rp = bodyRadius(planet);
    for (int i = 0; i < 4; ++i) rp = aimPerigee(goal, -mu / (rm + rp));
    const double vHigh = std::sqrt(2.0 * mu * rp / (rm * (rm + rp)));
    const double a = 0.5 * (rm + rp);
    MoonDeparture d;
    d.epoch = leave;
    d.vInfinity = vHigh * along - m.v;
    d.plan.departure = leave;
    d.plan.arrival = leave + kPi * std::sqrt(a * a * a / mu);
    d.plan.departureDeltaV = d.vInfinity;
    d.plan.departureCost = departureCost(moon, orbit, norm(d.vInfinity));
    return d;
}

/// To another moon of the same planet, within about one synodic period
/// (at most two months): the time for the two to come back to the same
/// places relative to each other, 1 / |1/T₁ − 1/T₂|.
std::optional<MoonDeparture> toSibling(Body moon, const ParkingOrbit& orbit, double epoch, Body to,
                                       Goal goal) {
    const double t1 = periodOf(moon);
    const double synodic = 1.0 / std::abs(1.0 / t1 - 1.0 / periodOf(to));
    const double window = std::min(1.1 * synodic, 60.0 * kDay);
    // Sooner rather than later: 10 m/s a day of waiting.
    constexpr double kWaitCost = 10.0 / kDay;
    const auto plan = cheapestLeg(epoch, {{}, moon, orbit}, to, goal, window, kWaitCost);
    if (!plan) return std::nullopt;
    return MoonDeparture{plan->departure, plan->departureDeltaV, *plan};
}

/// Out of the planet's system: the cheapest transfer from the planet, each
/// departure priced at the moon's best place; then the moment, within half
/// the moon's period of the planned day, when the moon is best placed
/// (Lambert's problem again for each moment, to the same arrival).
std::optional<MoonDeparture> outOfSystem(Body moon, const ParkingOrbit& orbit, double epoch,
                                         Body to, Goal goal) {
    const Body planet = planetOf(moon);
    const Vec3 planetR = bodyPosition(planet, epoch);
    const Vec3 planetV = bodyVelocity(planet, epoch);
    const SearchSpan span = searchSpan(planetR, planetV, to);
    const DepartureFrom from{planet, orbit, moonPlaces(moon, epoch, 36), moon};
    auto plan =
        cheapestTransfer(epoch, planetR, planetV, from, to, goal, span.window, span.longestFlight);
    if (!plan) return std::nullopt;
    // Sooner rather than later: leaving within two months (or a turn of the
    // moon, if longer), unless waiting saves over 3%. (Just before one
    // launch window, the next, a year or more away, may be a little
    // cheaper.)
    const double period = periodOf(moon);
    const double soon = std::max(60.0 * kDay, period);
    if (plan->departure > epoch + soon) {
        const auto sooner =
            cheapestTransfer(epoch, planetR, planetV, from, to, goal, soon, span.longestFlight);
        if (sooner && sooner->total() < 1.03 * plan->total()) plan = sooner;
    }

    const Vec3 targetR = bodyPosition(planetOf(to), plan->arrival);
    const double first = std::max(epoch + kFirstDeparture, plan->departure - 0.5 * period);
    const double last = plan->departure + 0.5 * period;
    std::optional<MoonDeparture> best;
    const auto consider = [&](double t) {
        const auto lambert =
            solveLambert(bodyPosition(planet, t), targetR, plan->arrival - t, kMuSun);
        if (!lambert) return;
        const Vec3 vInfinity = lambert->v1 - bodyVelocity(planet, t);
        const MoonState m = moonFromPlanet(moon, t);
        const auto escape = escapeFromMoon(moon, orbit, {m.r, m.v}, vInfinity);
        if (!escape || (best && escape->cost >= best->plan.departureCost)) return;
        MoonDeparture d{t, escape->deltaV, *plan};
        d.plan.departure = t;
        d.plan.departureDeltaV = vInfinity;
        d.plan.departureCost = escape->cost;
        best = d;
    };
    constexpr int kSteps = 200;
    const double step = (last - first) / kSteps;
    for (int k = 0; k <= kSteps; ++k) consider(first + k * step);
    if (!best) return std::nullopt;
    const double coarse = best->epoch;
    constexpr int kFine = 20;
    for (int k = -kFine; k <= kFine; ++k) {
        const double t = coarse + step * k / kFine;
        if (t >= first && t <= last) consider(t);
    }
    return best;
}

} // namespace

std::optional<MoonDeparture> departureFromMoon(Body moon, const ParkingOrbit& orbit, double epoch,
                                               Body to, Goal goal) {
    if (!isMoon(moon) || to == moon) return std::nullopt;
    const Body planet = planetOf(moon);
    if (to == planet) return fallHome(moon, orbit, epoch, goal);
    if (isMoon(to) && planetOf(to) == planet) return toSibling(moon, orbit, epoch, to, goal);
    return outOfSystem(moon, orbit, epoch, to, goal);
}

std::optional<MoonLeg> planFromMoon(const Voyage& voyage, double now,
                                    const VoyageOptions& options) {
    Voyage kept = voyage;
    kept.burns = burnsBefore(voyage, now);
    const VoyageSample here = voyageStateAt(kept, now, options);
    const auto parked = parkedAround(now, here.r, here.v);
    if (!parked || !isMoon(*parked)) return std::nullopt;
    const Body moon = *parked;
    const Body to = voyage.destination;
    const TwoBodyState rel{here.r - bodyPosition(moon, now), here.v - bodyVelocity(moon, now)};
    const OrbitalElements el = elementsFromState(rel.r, rel.v, bodyMu(moon), bodyRadius(moon));
    const auto departure =
        departureFromMoon(moon, {el.periapsis, el.apoapsis}, now, to, voyage.goal);
    if (!departure) return std::nullopt;

    // The best point in the orbit to leave from, from half a turn before.
    const double earliest = std::max(now + kFirstDeparture, departure->epoch - 0.5 * el.period);
    const auto burn = departureBurn(kept, earliest, departure->vInfinity, options);
    if (!burn) return std::nullopt;
    if (isMoon(to) && planetOf(to) == planetOf(moon)) {
        // To another moon: homed in on it with the full simulation, beside
        // it, as from an orbit around the planet.
        const double arrival = departure->plan.arrival;
        Homing homing = homingFor(kept, *burn, arrival, options);
        Burn homed = *burn;
        if (const auto there = homing.at(*burn)) {
            const Vec3 offset =
                besideMoon(to, voyage.goal, arrival, there->v - bodyVelocity(to, arrival));
            homed = homeIn(kept, *burn, arrival, to, offset, options).value_or(*burn);
        }
        return legFor(kept, now, homed, arrival, to, options);
    }
    MoonLeg leg;
    leg.from = now;
    leg.burn = *burn;
    leg.plan = departure->plan;
    if (to != planetOf(moon)) {
        // Out of the planet's system: leaving it with just the v∞ planned,
        // then a correction once clear of it (when it has climbed 0.3 of
        // the sphere of influence at v∞, or after ten days).
        const Vec3& vInfinity = departure->plan.departureDeltaV;
        leg.burn = homeOut(kept, *burn, moon, vInfinity, options).value_or(*burn);
        const double clear =
            0.3 * sphereOfInfluence(planetOf(moon), now) / std::max(norm(vInfinity), 300.0);
        Voyage corrected = kept;
        corrected.burns.push_back(leg.burn);
        const Burn correction{leg.burn.epoch + std::max(10.0 * kDay, clear), 0.0, 0.0, 0.0};
        corrected.burns.push_back(correction);
        std::ranges::stable_sort(corrected.burns, {}, &Burn::epoch);
        const auto index = static_cast<std::size_t>(
            std::ranges::find_if(corrected.burns,
                                 [&](const Burn& b) { return b.epoch == correction.epoch; }) -
            corrected.burns.begin());
        leg.correction = aimAtDestination(corrected, index, options);
    }
    leg.plan.departure = burn->epoch;
    return leg;
}

} // namespace orbit
