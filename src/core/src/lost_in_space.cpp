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
constexpr double kCapturePerigeeAltitude = 200e3; ///< m
constexpr double kAimEntryAngle = degToRad(-6.5); ///< the corridor's middle

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
    case Goal::Reentry: {
        const double rI = kRadiusEarth + options.entryInterfaceAltitude;
        const double fastest = options.fastestEntry;
        const double vInfinityMax = std::sqrt(std::max(0.0, fastest * fastest - 2.0 * mu / rI));
        return std::max(0.0, vInfinity - vInfinityMax);
    }
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

Puzzle makePuzzle(Difficulty difficulty, std::uint64_t seed, double epoch) {
    const Setup setup = setupFor(difficulty);
    const Goal goal = goalFor(difficulty);
    Random random(seed);
    Puzzle puzzle;
    puzzle.difficulty = difficulty;
    puzzle.seed = seed;

    // Try random orbits until one's cheapest way home is in the difficulty's
    // range; if none is, keep the one closest to it.
    std::optional<Puzzle> fallback;
    double fallbackOff = 0.0;
    for (int attempt = 0; attempt < 40; ++attempt) {
        const double a = setup.minA + (setup.maxA - setup.minA) * random.uniform(0.0, 1.0);
        const double e = random.uniform(0.0, setup.maxE);
        const double i = degToRad(random.uniform(0.0, setup.maxInclination));
        const double raan = random.uniform(0.0, kTwoPi);
        const double argp = random.uniform(0.0, kTwoPi);
        const double nu = random.uniform(0.0, kTwoPi);
        const PositionVelocity pv =
            stateFromElements(a * kAstronomicalUnit, e, i, raan, argp, nu, kMuSun);
        // Not too close to the Earth already.
        if (norm(pv.r - planetPosition(Planet::Earth, epoch)) < 0.1 * kAstronomicalUnit) continue;

        const auto plan =
            cheapestWayHome(epoch, pv.r, pv.v, goal, setup.window, setup.longestFlight);
        if (!plan) continue;

        Puzzle candidate = puzzle;
        candidate.voyage.startEpoch = epoch;
        candidate.voyage.position = pv.r;
        candidate.voyage.velocity = pv.v;
        candidate.voyage.goal = goal;
        candidate.plan = *plan;
        candidate.budget = setup.margin * plan->total() + setup.reserve;
        // Fill the tanks for that budget: m_prop = m_dry (e^(Δv/v_e) − 1).
        Ship& ship = candidate.voyage.ship;
        ship.propellant =
            ship.dryMass * (std::exp(candidate.budget / exhaustVelocity(ship.isp)) - 1.0);

        const double cost = plan->total();
        const double off = std::max({0.0, setup.minCost - cost, cost - setup.maxCost});
        if (off == 0.0) return candidate;
        if (!fallback || off < fallbackOff) {
            fallback = candidate;
            fallbackOff = off;
        }
    }
    return fallback ? *fallback : puzzle;
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

} // namespace orbit
