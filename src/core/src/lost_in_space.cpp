// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/lost_in_space.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/solar_system.hpp"
#include "orbit/core/two_body.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <vector>

namespace orbit {

namespace {

constexpr double kDay = 86400.0;
constexpr double kYear = 365.25 * kDay;
constexpr double kShortestFlight = 30.0 * kDay;
/// The longest trip searched for: it must fit in a prediction
/// (VoyageOptions::horizon, 4 years from the last burn).
constexpr double kLongestTrip = 3.5 * kYear;
constexpr double kCapturePerigeeAltitude = 200e3; ///< m
constexpr double kAimEntryAngle = degToRad(-6.5); ///< the corridor's middle
/// A re-entry aims this much under the fastest survivable entry speed.
constexpr double kEntrySpeedMargin = 100.0; ///< m/s

double muEarth() { return planetMu(Planet::Earth); }

/// Uniform random numbers in [low, high). std::uniform_real_distribution
/// differs between standard libraries; the engine's raw output does not, so
/// a seed gives the same puzzle on every platform.
class Random {
public:
    explicit Random(std::uint64_t seed) : m_engine(seed) {}
    double uniform(double low, double high) {
        const double unit = static_cast<double>(m_engine() >> 11U) * 0x1.0p-53;
        return low + (high - low) * unit;
    }

private:
    std::mt19937_64 m_engine;
};

/// How each difficulty sets up its puzzles.
struct Setup {
    double minA, maxA;       ///< semi-major axis range (au)
    double maxE;             ///< eccentricity up to
    double maxInclination;   ///< degrees
    double minCost, maxCost; ///< the cheapest way home must cost this (m/s)
    double margin;           ///< fuel = margin × cheapest + reserve
    double reserve;          ///< m/s, for course corrections
    double window;           ///< s to look for a departure
    double longestFlight;    ///< s
};

Setup setupFor(Difficulty difficulty) {
    switch (difficulty) {
    case Difficulty::Easy:
        return {0.85, 1.25, 0.08, 2.0, 300.0, 2500.0, 1.6, 300.0, 1.0 * kYear, 540.0 * kDay};
    case Difficulty::Normal:
        return {0.7, 1.7, 0.15, 5.0, 1000.0, 4500.0, 1.4, 250.0, 1.5 * kYear, 600.0 * kDay};
    case Difficulty::Hard:
        return {0.6, 2.6, 0.25, 8.0, 2000.0, 7500.0, 1.25, 200.0, 2.0 * kYear, 700.0 * kDay};
    }
    return setupFor(Difficulty::Easy);
}

/// Where a puzzle near a planet starts: on the planet's orbit, this far
/// ahead of it or behind it (radians), pushed by up to this much (m/s) in a
/// random direction.
struct NearPlanet {
    Planet planet;
    double minLead, maxLead;
    double maxPush;
};

NearPlanet nearPlanetFor(Region region) {
    switch (region) {
    case Region::Venus:
        return {Planet::Venus, degToRad(2.0), degToRad(6.0), 600.0};
    case Region::Mars:
        return {Planet::Mars, degToRad(2.0), degToRad(6.0), 500.0};
    case Region::Jupiter:
        // Further ahead: Jupiter's sphere of influence is 0.3 au across,
        // and its pull would dominate the start.
        return {Planet::Jupiter, degToRad(12.0), degToRad(20.0), 300.0};
    case Region::BetweenPlanets:
    case Region::AsteroidBelt:
        break;
    }
    return {Planet::Mars, 0.0, 0.0, 0.0};
}

/// A random start in the region: heliocentric position and velocity.
PositionVelocity randomStart(Region region, const Setup& setup, Random& random, double epoch) {
    const auto fromElements = [&](double minA, double maxA, double maxE, double maxInclination) {
        const double a = minA + (maxA - minA) * random.uniform(0.0, 1.0);
        const double e = random.uniform(0.0, maxE);
        const double i = degToRad(random.uniform(0.0, maxInclination));
        const double raan = random.uniform(0.0, kTwoPi);
        const double argp = random.uniform(0.0, kTwoPi);
        const double nu = random.uniform(0.0, kTwoPi);
        return stateFromElements(a * kAstronomicalUnit, e, i, raan, argp, nu, kMuSun);
    };
    switch (region) {
    case Region::BetweenPlanets:
        return fromElements(setup.minA, setup.maxA, setup.maxE, setup.maxInclination);
    case Region::AsteroidBelt:
        // The main belt: 2.2 to 3.2 au, most orbits only a little
        // eccentric and tilted.
        return fromElements(2.2, 3.2, 0.12, 10.0);
    case Region::Venus:
    case Region::Mars:
    case Region::Jupiter:
        break;
    }
    // On the planet's orbit, a few degrees ahead or behind: Kepler's
    // problem moves the planet's state by the time that takes, angle / n,
    // with n = √(μ/a³) the mean motion.
    const NearPlanet near = nearPlanetFor(region);
    const double a = planetOrbit(near.planet, epoch).semiMajorAxis;
    const double meanMotion = std::sqrt(kMuSun / (a * a * a));
    const double lead = random.uniform(near.minLead, near.maxLead);
    const double side = random.uniform(0.0, 1.0) < 0.5 ? -1.0 : 1.0;
    const TwoBodyState planet{planetPosition(near.planet, epoch),
                              planetVelocity(near.planet, epoch)};
    const TwoBodyState start = propagateKepler(planet, side * lead / meanMotion, kMuSun);
    // A push, so the ship and the planet drift apart rather than meet: a
    // smaller orbit has a shorter period, so slowing down makes the ship
    // gain on the planet, and speeding up makes it fall behind (by Kepler's
    // third law, T ∝ a^(3/2)). Ahead of the planet, then, it slows down;
    // behind, it speeds up. (Hill's equations add a wobble of about
    // 0.5 Δv/n back towards the planet first, n the mean motion: a few
    // million km at most, well inside the gap.) A smaller push in a random
    // direction square to that (uniform over the sphere: z uniform in
    // [−1, 1], the angle round uniform) tilts and stretches the orbit.
    const double push = near.maxPush * random.uniform(0.4, 1.0);
    const double z = random.uniform(-1.0, 1.0);
    const double phi = random.uniform(0.0, kTwoPi);
    const double sideways = 0.4 * push * random.uniform(0.0, 1.0);
    const double xy = std::sqrt(1.0 - z * z);
    const Vec3 along = normalized(start.v);
    const Vec3 random3{xy * std::cos(phi), xy * std::sin(phi), z};
    const Vec3 square = random3 - dot(random3, along) * along;
    const double squareNorm = norm(square);
    Vec3 dv = -side * push * along;
    if (squareNorm > 1e-9) dv += sideways * square / squareNorm;
    return {start.r, start.v + dv};
}

/// The Earth's position and velocity at the start of each day from `epoch`
/// on, so the transfer search doesn't recompute them for every pair of days.
class EarthTable {
public:
    EarthTable(double epoch, double span) : m_epoch(epoch) {
        const auto days = static_cast<std::size_t>(std::ceil(span / kDay)) + 1;
        m_states.reserve(days);
        for (std::size_t i = 0; i < days; ++i) {
            const double t = epoch + static_cast<double>(i) * kDay;
            m_states.push_back(
                {planetPosition(Planet::Earth, t), planetVelocity(Planet::Earth, t)});
        }
    }
    /// On a whole day from the start: from the table; otherwise computed.
    TwoBodyState at(double t) const {
        const double day = (t - m_epoch) / kDay;
        const double whole = std::round(day);
        if (std::abs(day - whole) < 1e-9 && whole >= 0.0 &&
            whole < static_cast<double>(m_states.size())) {
            return m_states[static_cast<std::size_t>(whole)];
        }
        return {planetPosition(Planet::Earth, t), planetVelocity(Planet::Earth, t)};
    }

private:
    double m_epoch;
    std::vector<TwoBodyState> m_states;
};

/// One candidate transfer: leave a ship in state `ship` at `departure`,
/// arrive at the Earth after `flight`. Nothing if Lambert has no answer.
std::optional<TransferPlan> transfer(const TwoBodyState& ship, double departure, double flight,
                                     const EarthTable& earth, Goal goal) {
    const TwoBodyState e = earth.at(departure + flight);
    const auto lambert = solveLambert(ship.r, e.r, flight, kMuSun);
    if (!lambert) return std::nullopt;
    TransferPlan plan;
    plan.departure = departure;
    plan.arrival = departure + flight;
    plan.departureDeltaV = lambert->v1 - ship.v;
    plan.departureCost = norm(plan.departureDeltaV);
    plan.vInfinity = norm(lambert->v2 - e.v);
    plan.arrivalCost = arrivalCost(goal, plan.vInfinity);
    return plan;
}

/// The B-vector of a path past the Earth, from a state relative to it
/// (m, m/s): where the incoming straight line would cross the plane through
/// the Earth's centre square to it. ŝ, the incoming direction, comes from
/// the hyperbola's shape: ŝ = ê/e + √(1 − 1/e²) (ĥ × ê).
struct BPlane {
    Vec3 s; ///< incoming direction
    Vec3 b; ///< the B-vector (m)
    double vInfinity = 0.0;
};
BPlane bPlane(const Vec3& r, const Vec3& v) {
    const double mu = muEarth();
    const double rNorm = norm(r);
    const double v2 = normSquared(v);
    const double energy = v2 / 2.0 - mu / rNorm;
    const Vec3 h = cross(r, v);
    const Vec3 eVec = ((v2 - mu / rNorm) * r - dot(r, v) * v) / mu;
    const double e = norm(eVec);
    BPlane plane;
    if (energy <= 0.0 || e <= 1.0 || norm(h) == 0.0) {
        // Not a hyperbola (deep in the Earth's grip, or far out where the
        // Sun's pull dominates): the straight-line miss is good enough.
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

/// The voyage with only its burns before `epoch` (or before and at it).
Voyage burnsBefore(const Voyage& voyage, double epoch, bool includeAt) {
    Voyage copy = voyage;
    std::erase_if(copy.burns,
                  [&](const Burn& b) { return includeAt ? b.epoch > epoch : b.epoch >= epoch; });
    return copy;
}

std::vector<Burn> sortedBurns(const Voyage& voyage) {
    std::vector<Burn> burns = voyage.burns;
    std::ranges::stable_sort(burns, {}, &Burn::epoch);
    return burns;
}

} // namespace

std::string_view difficultyName(Difficulty difficulty) {
    switch (difficulty) {
    case Difficulty::Easy:
        return "Easy";
    case Difficulty::Normal:
        return "Normal";
    case Difficulty::Hard:
        return "Hard";
    }
    return "";
}

std::string_view regionKey(Region region) {
    switch (region) {
    case Region::BetweenPlanets:
        return "between";
    case Region::Venus:
        return "venus";
    case Region::Mars:
        return "mars";
    case Region::AsteroidBelt:
        return "belt";
    case Region::Jupiter:
        return "jupiter";
    }
    return "";
}

std::optional<Region> regionFromKey(std::string_view key) {
    for (const Region region : kAllRegions) {
        if (regionKey(region) == key) return region;
    }
    return std::nullopt;
}

std::string_view regionName(Region region) {
    switch (region) {
    case Region::BetweenPlanets:
        return "Between the planets";
    case Region::Venus:
        return "Near Venus";
    case Region::Mars:
        return "Near Mars";
    case Region::AsteroidBelt:
        return "In the asteroid belt";
    case Region::Jupiter:
        return "Near Jupiter";
    }
    return "";
}

Goal goalFor(Difficulty difficulty) {
    switch (difficulty) {
    case Difficulty::Easy:
        return Goal::ReachEarth;
    case Difficulty::Normal:
        return Goal::Reentry;
    case Difficulty::Hard:
        return Goal::EarthOrbit;
    }
    return Goal::ReachEarth;
}

double arrivalCost(Goal goal, double vInfinity, const VoyageOptions& options) {
    const double mu = muEarth();
    switch (goal) {
    case Goal::ReachEarth:
        return 0.0;
    case Goal::Reentry:
        return std::max(0.0, vInfinity - fastestReentryArrival(options));
    case Goal::EarthOrbit: {
        const double rp = kRadiusEarth + kCapturePerigeeAltitude;
        const double ra = 0.5 * options.captureMaxApogee;
        const double arriving = std::sqrt(vInfinity * vInfinity + 2.0 * mu / rp);
        const double captured = std::sqrt(2.0 * mu / rp - 2.0 * mu / (rp + ra));
        return arriving - captured;
    }
    }
    return 0.0;
}

double fastestReentryArrival(const VoyageOptions& options) {
    // Energy around the Earth: v_entry² = v∞² + 2μ/r_I.
    const double rI = kRadiusEarth + options.entryInterfaceAltitude;
    const double fastest = options.fastestEntry - kEntrySpeedMargin;
    return std::sqrt(std::max(0.0, fastest * fastest - 2.0 * muEarth() / rI));
}

double aimImpactParameter(Goal goal, double vInfinity, const VoyageOptions& options) {
    const double mu = muEarth();
    // b = h / v∞, with h = r_p v_p at perigee or r v cos γ anywhere.
    const auto fromPerigee = [&](double altitude) {
        const double rp = kRadiusEarth + altitude;
        return rp * std::sqrt(vInfinity * vInfinity + 2.0 * mu / rp) / vInfinity;
    };
    switch (goal) {
    case Goal::ReachEarth:
        return fromPerigee(1000e3);
    case Goal::Reentry: {
        const double rI = kRadiusEarth + options.entryInterfaceAltitude;
        const double vI = std::sqrt(vInfinity * vInfinity + 2.0 * mu / rI);
        return rI * vI * std::cos(kAimEntryAngle) / vInfinity;
    }
    case Goal::EarthOrbit:
        return fromPerigee(kCapturePerigeeAltitude);
    }
    return 0.0;
}

std::optional<TransferPlan> cheapestWayHome(double epoch, const Vec3& r, const Vec3& v, Goal goal,
                                            double window, double longestFlight) {
    const EarthTable earth(epoch, window + longestFlight + 10.0 * kDay);
    const TwoBodyState start{r, v};
    std::optional<TransferPlan> best;
    const auto consider = [&](double departure, double flight) {
        if (departure < epoch || flight < kShortestFlight) return;
        const TwoBodyState ship = propagateKepler(start, departure - epoch, kMuSun);
        const auto plan = transfer(ship, departure, flight, earth, goal);
        if (plan && (!best || plan->total() < best->total())) best = plan;
    };

    // A coarse grid over (departure, flight time): the porkchop plot...
    constexpr double kDepartureStep = 4.0 * kDay;
    constexpr double kFlightStep = 5.0 * kDay;
    const int departures = static_cast<int>(window / kDepartureStep);
    const int flights = static_cast<int>((longestFlight - kShortestFlight) / kFlightStep);
    for (int i = 0; i <= departures; ++i) {
        for (int j = 0; j <= flights; ++j) {
            consider(epoch + i * kDepartureStep, kShortestFlight + j * kFlightStep);
        }
    }
    if (!best) return std::nullopt;
    // ...then a finer one, a quarter of a day apart, around its lowest point.
    const TransferPlan coarse = *best;
    const double coarseFlight = coarse.arrival - coarse.departure;
    constexpr double kFine = 0.25 * kDay;
    for (int i = -16; i <= 16; ++i) {
        for (int j = -20; j <= 20; ++j) {
            consider(coarse.departure + i * kFine, coarseFlight + j * kFine);
        }
    }
    return best;
}

SearchSpan searchSpan(const Vec3& r, const Vec3& v) {
    const OrbitalElements el = elementsFromState(r, v, kMuSun, kRadiusSun);
    SearchSpan span;
    // The synodic period: the ship gains (or loses) a lap on the Earth at
    // the rate 1/T − 1/T_earth.
    const double lapRate = el.bound ? std::abs(1.0 / el.period - 1.0 / kYear) : 0.0;
    span.window = lapRate > 0.0 ? std::clamp(1.1 / lapRate, kYear, 2.2 * kYear) : kYear;
    // Half the period of the transfer ellipse (Kepler's third law).
    const double far = el.bound ? el.apoapsis : norm(r);
    const double a = 0.5 * (far + kAstronomicalUnit);
    const double hohmann = kPi * std::sqrt(a * a * a / kMuSun);
    span.longestFlight = std::clamp(1.3 * hohmann, 540.0 * kDay, kLongestTrip);
    return span;
}

Puzzle makePuzzle(Difficulty difficulty, std::uint64_t seed, double epoch, Region region) {
    const Setup setup = setupFor(difficulty);
    const Goal goal = goalFor(difficulty);
    Random random(seed);
    Puzzle puzzle;
    puzzle.difficulty = difficulty;
    puzzle.region = region;
    puzzle.seed = seed;
    const bool anywhere = region == Region::BetweenPlanets;

    // Try random orbits until one's cheapest way home is in the difficulty's
    // range (between the planets; near a place, any way home will do); if
    // none is, keep the one closest to it.
    std::optional<Puzzle> fallback;
    double fallbackOff = 0.0;
    for (int attempt = 0; attempt < 40; ++attempt) {
        const PositionVelocity pv = randomStart(region, setup, random, epoch);
        // Not too close to the Earth already.
        if (norm(pv.r - planetPosition(Planet::Earth, epoch)) < 0.1 * kAstronomicalUnit) continue;

        const SearchSpan span =
            anywhere ? SearchSpan{setup.window, setup.longestFlight} : searchSpan(pv.r, pv.v);
        const auto plan = cheapestWayHome(epoch, pv.r, pv.v, goal, span.window, span.longestFlight);
        if (!plan) continue;

        Puzzle candidate = puzzle;
        candidate.voyage.startEpoch = epoch;
        candidate.voyage.position = pv.r;
        candidate.voyage.velocity = pv.v;
        candidate.voyage.goal = goal;
        candidate.plan = *plan;

        const double cost = plan->total();
        const double off =
            anywhere ? std::max({0.0, setup.minCost - cost, cost - setup.maxCost}) : 0.0;
        if (off == 0.0) {
            fallback = candidate;
            break;
        }
        if (!fallback || off < fallbackOff) {
            fallback = candidate;
            fallbackOff = off;
        }
    }
    if (!fallback) return puzzle;

    // The search flew the ship with only the Sun pulling. Until it leaves,
    // the planets pull too (most of all the one it starts near), so price
    // the departure burn again from where the full simulation puts the ship
    // then, and give the fuel for whichever is dearer.
    Puzzle chosen = *fallback;
    TransferPlan& plan = chosen.plan;
    if (const auto burn = burnTowardsEarth(chosen.voyage, plan.departure, plan.arrival)) {
        plan.departureCost = std::max(plan.departureCost, burnSize(*burn));
    }
    chosen.budget = setup.margin * plan.total() + setup.reserve;
    // Fill the tanks for that budget: m_prop = m_dry (e^(Δv/v_e) − 1).
    Ship& ship = chosen.voyage.ship;
    ship.propellant = ship.dryMass * (std::exp(chosen.budget / exhaustVelocity(ship.isp)) - 1.0);
    return chosen;
}

std::optional<Burn> burnTowardsEarth(const Voyage& voyage, double epoch, double arrival,
                                     const VoyageOptions& options) {
    if (arrival <= epoch) return std::nullopt;
    const VoyageSample here = voyageStateAt(burnsBefore(voyage, epoch, false), epoch, options);
    if (norm(here.r - planetPosition(Planet::Earth, epoch)) <
        sphereOfInfluence(Planet::Earth, epoch)) {
        return std::nullopt; // the Sun-only Lambert solution means nothing here
    }
    const Vec3 earthR = planetPosition(Planet::Earth, arrival);
    const Vec3 earthV = planetVelocity(Planet::Earth, arrival);

    // Aim beside the Earth, square to the arrival direction, on the side
    // that makes the pass go round the Earth the way it turns (anticlockwise
    // seen from the north). The arrival direction depends on the aim point,
    // so aim, solve, and aim again from the new direction.
    Vec3 target = earthR;
    LambertSolution lambert;
    for (int i = 0; i < 3; ++i) {
        const auto solution = solveLambert(here.r, target, arrival - epoch, kMuSun);
        if (!solution) return std::nullopt;
        lambert = *solution;
        const Vec3 vInfinity = lambert.v2 - earthV;
        const Vec3 side = normalized(cross(normalized(vInfinity), Vec3{0.0, 0.0, 1.0}));
        target = earthR + aimImpactParameter(voyage.goal, norm(vInfinity), options) * side;
    }
    return burnFromVector(epoch, lambert.v1 - here.v, burnFrameAt(epoch, here.r, here.v));
}

std::optional<Burn> refineAim(const Voyage& voyage, std::size_t index,
                              const VoyageOptions& options) {
    const std::vector<Burn> burns = sortedBurns(voyage);
    if (index >= burns.size()) return std::nullopt;
    const Burn original = burns[index];

    // Fly from just before the burn, with no burns after it, past the Earth
    // (and not stopping on reaching its sphere of influence).
    const VoyageSample start =
        voyageStateAt(burnsBefore(voyage, original.epoch, false), original.epoch, options);
    Voyage trial;
    trial.startEpoch = original.epoch;
    trial.position = start.r;
    trial.velocity = start.v;
    trial.ship = voyage.ship;
    trial.ship.propellant = std::max(0.0, start.mass - voyage.ship.dryMass);
    trial.goal = Goal::EarthOrbit;
    trial.burns = {original};

    VoyageOptions fly = options;
    const VoyageResult first = flyVoyage(trial, fly);
    if (!first.closest.valid) return std::nullopt;
    fly.stopAt = first.closest.epoch + 30.0 * kDay;

    // The B-plane's axes, fixed from the first flight so the target doesn't
    // move under Newton's feet: T square to the incoming direction in the
    // ecliptic, R completing the set.
    const BPlane plane0 = bPlane(first.closest.relativePosition, first.closest.relativeVelocity);
    const Vec3 axisT = normalized(cross(plane0.s, Vec3{0.0, 0.0, 1.0}));
    const Vec3 axisR = cross(plane0.s, axisT);
    // Keep to the side the path already passes on (the smallest change),
    // unless it is heading straight at the centre.
    const double bNow = norm(plane0.b);
    const Vec3 direction = bNow > 1000.0 ? plane0.b / bNow : axisT;
    const double bTarget = aimImpactParameter(voyage.goal, plane0.vInfinity, options);
    const std::array<double, 2> target{bTarget * dot(direction, axisT),
                                       bTarget * dot(direction, axisR)};

    // The miss: (B·T, B·R) minus the target, for a burn x.
    const auto miss = [&](const std::array<double, 3>& x) -> std::optional<std::array<double, 2>> {
        trial.burns = {Burn{original.epoch, x[0], x[1], x[2]}};
        const VoyageResult result = flyVoyage(trial, fly);
        if (!result.closest.valid) return std::nullopt;
        const BPlane p = bPlane(result.closest.relativePosition, result.closest.relativeVelocity);
        return std::array<double, 2>{dot(p.b, axisT) - target[0], dot(p.b, axisR) - target[1]};
    };

    std::array<double, 3> x{original.prograde, original.radial, original.normal};
    constexpr double kStep = 0.01;       // m/s, for the finite differences
    constexpr double kTolerance = 500.0; // m in the B-plane
    for (int iteration = 0; iteration < 12; ++iteration) {
        const auto f = miss(x);
        if (!f) return std::nullopt;
        if (std::hypot((*f)[0], (*f)[1]) < kTolerance) {
            return Burn{original.epoch, x[0], x[1], x[2]};
        }

        // The Jacobian J (2×3): how the miss moves per m/s in each direction.
        std::array<std::array<double, 3>, 2> jac{};
        for (std::size_t k = 0; k < 3; ++k) {
            std::array<double, 3> xk = x;
            xk[k] += kStep;
            const auto fk = miss(xk);
            if (!fk) return std::nullopt;
            jac[0][k] = ((*fk)[0] - (*f)[0]) / kStep;
            jac[1][k] = ((*fk)[1] - (*f)[1]) / kStep;
        }
        // The smallest change that cancels the miss, to first order:
        // Δx = Jᵀ (J Jᵀ)⁻¹ (−f).
        const double a = jac[0][0] * jac[0][0] + jac[0][1] * jac[0][1] + jac[0][2] * jac[0][2];
        const double b = jac[0][0] * jac[1][0] + jac[0][1] * jac[1][1] + jac[0][2] * jac[1][2];
        const double d = jac[1][0] * jac[1][0] + jac[1][1] * jac[1][1] + jac[1][2] * jac[1][2];
        const double det = a * d - b * b;
        if (std::abs(det) < 1e-30) return std::nullopt;
        const double y0 = (-d * (*f)[0] + b * (*f)[1]) / det;
        const double y1 = (b * (*f)[0] - a * (*f)[1]) / det;
        for (std::size_t k = 0; k < 3; ++k) x[k] += jac[0][k] * y0 + jac[1][k] * y1;
    }
    return std::nullopt;
}

std::optional<Burn> captureBurn(const Voyage& voyage, const VoyageOptions& options) {
    Voyage trial = voyage;
    trial.goal = Goal::EarthOrbit; // fly past the edge of the sphere of influence
    const VoyageResult result = flyVoyage(trial, options);
    const EarthApproach& c = result.closest;
    if (!c.valid || result.end == VoyageEnd::HitSun) return std::nullopt;
    if (c.distance > sphereOfInfluence(Planet::Earth, c.epoch)) return std::nullopt;
    if (c.distance < kRadiusEarth + options.entryInterfaceAltitude) return std::nullopt;

    // At perigee the velocity is square to the radius, so braking straight
    // back along it lowers the speed without turning the path: from the
    // hyperbola's v_p to the ellipse's √(2μ/r_p − 2μ/(r_p + r_a)) (vis-viva).
    const double mu = muEarth();
    const double rp = c.distance;
    const double ra = std::max(rp, 0.5 * options.captureMaxApogee);
    const double wanted = std::sqrt(2.0 * mu / rp - 2.0 * mu / (rp + ra));
    if (c.speed <= wanted) return std::nullopt; // already captured
    return Burn{c.epoch, -(c.speed - wanted), 0.0, 0.0};
}

double approachSpeed(const EarthApproach& closest) {
    if (!closest.valid || closest.distance <= 0.0) return 0.0;
    // Energy per kg around the Earth, v²/2 − μ/r = v∞²/2 (positive when
    // the path comes from far away and leaves again).
    const double twiceEnergy = closest.speed * closest.speed - 2.0 * muEarth() / closest.distance;
    return twiceEnergy > 0.0 ? std::sqrt(twiceEnergy) : 0.0;
}

std::optional<Burn> approachBrake(const Voyage& voyage, double notBefore,
                                  const VoyageOptions& options) {
    Voyage trial = voyage;
    trial.goal = Goal::EarthOrbit; // fly past the edge of the sphere of influence
    const VoyageResult result = flyVoyage(trial, options);
    const EarthApproach& c = result.closest;
    if (!c.valid || !result.earthArrival || result.end == VoyageEnd::HitSun) return std::nullopt;
    const double wanted = fastestReentryArrival(options);
    if (approachSpeed(c) <= wanted) return std::nullopt;

    // A third of the way in (in time): well inside the sphere of influence,
    // where the burn's directions are taken relative to the Earth, and
    // early enough to aim afterwards.
    const double arrival = *result.earthArrival;
    const double epoch = std::max(notBefore, arrival + (c.epoch - arrival) / 3.0);
    if (epoch > c.epoch - 3600.0) return std::nullopt;
    const VoyageSample here = voyageStateAt(burnsBefore(trial, epoch, false), epoch, options);
    const BurnFrame frame = burnFrameAt(epoch, here.r, here.v);
    if (frame.body != ReferenceBody::Earth) return std::nullopt;

    // From the speed here to the one that leaves v∞ = wanted:
    // v² = v∞² + 2μ/r. Prograde in the Earth's frame is along the velocity
    // relative to it, so a retrograde burn takes off exactly that much.
    const Vec3 relativeR = here.r - planetPosition(Planet::Earth, epoch);
    const Vec3 relativeV = here.v - planetVelocity(Planet::Earth, epoch);
    const double speed = norm(relativeV);
    const double target = std::sqrt(wanted * wanted + 2.0 * muEarth() / norm(relativeR));
    if (speed <= target) return std::nullopt;
    return Burn{epoch, -(speed - target), 0.0, 0.0};
}

} // namespace orbit
