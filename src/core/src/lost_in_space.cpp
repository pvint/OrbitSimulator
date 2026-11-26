// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/lost_in_space.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/flyby.hpp"
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
/// The longest trip searched for: a little more than a Hohmann transfer
/// from Neptune (31 years).
constexpr double kLongestTrip = 40.0 * kYear;
constexpr double kCapturePerigeeAltitude = 200e3; ///< m
constexpr double kAimEntryAngle = degToRad(-6.5); ///< the corridor's middle
/// A re-entry aims this much under the fastest survivable entry speed.
constexpr double kEntrySpeedMargin = 100.0; ///< m/s

double muEarth() { return bodyMu(Body::Earth); }

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
    Body planet;
    double minLead, maxLead;
    double maxPush;
};

NearPlanet nearPlanetFor(Region region) {
    switch (region) {
    case Region::Mercury: // sphere of influence 112,000 km
        return {Body::Mercury, degToRad(2.0), degToRad(6.0), 800.0};
    case Region::Earth: // (a game near the Earth starts in orbit instead)
        return {Body::Earth, degToRad(3.0), degToRad(8.0), 500.0};
    case Region::Venus:
        return {Body::Venus, degToRad(2.0), degToRad(6.0), 600.0};
    case Region::Mars:
        return {Body::Mars, degToRad(2.0), degToRad(6.0), 500.0};
    case Region::Jupiter:
        // Further ahead: Jupiter's sphere of influence reaches 0.32 au,
        // and its pull would dominate the start.
        return {Body::Jupiter, degToRad(12.0), degToRad(20.0), 300.0};
    case Region::Saturn: // sphere of influence 0.36 au
        return {Body::Saturn, degToRad(12.0), degToRad(20.0), 250.0};
    case Region::Uranus: // 0.35 au
        return {Body::Uranus, degToRad(8.0), degToRad(14.0), 150.0};
    case Region::Neptune: // 0.58 au
        return {Body::Neptune, degToRad(6.0), degToRad(12.0), 120.0};
    case Region::BetweenPlanets:
    case Region::AsteroidBelt:
        break;
    }
    return {Body::Mars, 0.0, 0.0, 0.0};
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
    case Region::Mercury:
    case Region::Venus:
    case Region::Earth:
    case Region::Mars:
    case Region::Jupiter:
    case Region::Saturn:
    case Region::Uranus:
    case Region::Neptune:
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
    const TwoBodyState planet{bodyPosition(near.planet, epoch), bodyVelocity(near.planet, epoch)};
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

/// A planet's position and velocity at the start of each day from `epoch`
/// on, so the transfer search doesn't recompute them for every pair of days.
class PlanetTable {
public:
    PlanetTable(Body planet, double epoch, double span) : m_planet(planet), m_epoch(epoch) {
        const auto days = static_cast<std::size_t>(std::ceil(span / kDay)) + 1;
        m_states.reserve(days);
        for (std::size_t i = 0; i < days; ++i) {
            const double t = epoch + static_cast<double>(i) * kDay;
            m_states.push_back({bodyPosition(planet, t), bodyVelocity(planet, t)});
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
        return {bodyPosition(m_planet, t), bodyVelocity(m_planet, t)};
    }

private:
    Body m_planet;
    double m_epoch;
    std::vector<TwoBodyState> m_states;
};

/// One candidate transfer: leave at `departure` from `ship` (the ship's own
/// state, or the planet it is parked around), arrive at the destination
/// after `flight`. Nothing if Lambert has no answer.
std::optional<TransferPlan> transfer(const TwoBodyState& ship, const DepartureFrom& from,
                                     double departure, double flight, const PlanetTable& target,
                                     Body to, Goal goal) {
    const TwoBodyState e = target.at(departure + flight);
    const auto lambert = solveLambert(ship.r, e.r, flight, kMuSun);
    if (!lambert) return std::nullopt;
    TransferPlan plan;
    plan.departure = departure;
    plan.arrival = departure + flight;
    plan.departureDeltaV = lambert->v1 - ship.v;
    plan.departureCost = norm(plan.departureDeltaV);
    if (from.planet && from.points.empty()) {
        plan.departureCost = departureCost(*from.planet, from.orbit, norm(plan.departureDeltaV));
    } else if (from.planet) {
        // An orbit already set: the best of its points for this v∞ (its
        // plane and its low point may not suit every direction).
        const double mu = bodyMu(*from.planet);
        double best = -1.0;
        for (const TwoBodyState& at : from.points) {
            if (const auto escape = escapeFrom(at, plan.departureDeltaV, mu)) {
                if (best < 0.0 || escape->cost < best) best = escape->cost;
            }
        }
        if (best < 0.0) return std::nullopt;
        plan.departureCost = best;
    }
    plan.vInfinity = norm(lambert->v2 - e.v);
    plan.arrivalCost = arrivalCost(goal, plan.vInfinity, to);
    return plan;
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
    case Region::Mercury:
        return "mercury";
    case Region::Earth:
        return "earth";
    case Region::Venus:
        return "venus";
    case Region::Mars:
        return "mars";
    case Region::AsteroidBelt:
        return "belt";
    case Region::Jupiter:
        return "jupiter";
    case Region::Saturn:
        return "saturn";
    case Region::Uranus:
        return "uranus";
    case Region::Neptune:
        return "neptune";
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
    case Region::Mercury:
        return "Near Mercury";
    case Region::Earth:
        return "The Earth";
    case Region::Venus:
        return "Near Venus";
    case Region::Mars:
        return "Near Mars";
    case Region::AsteroidBelt:
        return "In the asteroid belt";
    case Region::Jupiter:
        return "Near Jupiter";
    case Region::Saturn:
        return "Near Saturn";
    case Region::Uranus:
        return "Near Uranus";
    case Region::Neptune:
        return "Near Neptune";
    }
    return "";
}

std::optional<Body> regionPlanet(Region region) {
    switch (region) {
    case Region::BetweenPlanets:
    case Region::AsteroidBelt:
        return std::nullopt;
    case Region::Mercury:
        return Body::Mercury;
    case Region::Venus:
        return Body::Venus;
    case Region::Earth:
        return Body::Earth;
    case Region::Mars:
        return Body::Mars;
    case Region::Jupiter:
        return Body::Jupiter;
    case Region::Saturn:
        return Body::Saturn;
    case Region::Uranus:
        return Body::Uranus;
    case Region::Neptune:
        return Body::Neptune;
    }
    return std::nullopt;
}

Goal goalFor(Difficulty difficulty, Body destination) {
    const bool home = destination == Body::Earth;
    switch (difficulty) {
    case Difficulty::Easy:
        return home ? Goal::ReachEarth : Goal::ReachPlanet;
    case Difficulty::Normal:
        return home ? Goal::Reentry : Goal::ClosePass;
    case Difficulty::Hard:
        return home ? Goal::EarthOrbit : Goal::PlanetOrbit;
    }
    return Goal::ReachEarth;
}

ParkingOrbit parkingOrbit(Body planet) {
    const double radius = bodyRadius(planet);
    switch (planet) {
    case Body::Mercury:
    case Body::Earth:
        return {radius + 200e3, radius + 200e3};
    case Body::Venus: // above its thick air
    case Body::Mars:
        return {radius + 300e3, radius + 300e3};
    case Body::Jupiter:
    case Body::Saturn:
    case Body::Uranus:
    case Body::Neptune:
        // A long ellipse, like Juno's: a low circular orbit around a giant
        // is very deep in its gravity (escaping Jupiter from 1.1 radii takes
        // about 17 km/s), while from the low point of an ellipse reaching
        // out to 50 radii it takes a few.
        return {1.1 * radius, 50.0 * radius};
    case Body::Titan:
        // Above its haze: Titan's air reaches about 1,200 km up (Cassini's
        // closest passes were near 900 km, through its fringe).
        return {radius + 1500e3, radius + 1500e3};
    default:
        // Another moon: a low circle, a tenth of its radius up (at least
        // 50 km), clear of mountains.
        break;
    }
    const double low = radius + std::max(50e3, 0.1 * radius);
    return {low, low};
}

bool canPark(Body body) {
    // A low orbit must lie well inside the body's sphere of influence, or
    // its planet's pull would pull it apart (small moons close to a giant:
    // Mimas, Enceladus, Tethys, Miranda).
    return parkingOrbit(body).apoapsis < 0.4 * sphereOfInfluence(body, 0.0);
}

double captureApoapsis(Body planet, double epoch, const VoyageOptions& options) {
    if (planet == Body::Earth) return 0.5 * options.captureMaxApogee;
    return std::min(0.2 * sphereOfInfluence(planet, epoch), 100.0 * bodyRadius(planet));
}

double aimPeriapsis(Goal goal, Body planet) {
    const double radius = bodyRadius(planet);
    switch (goal) {
    case Goal::ReachPlanet:
        return 10.0 * radius;
    case Goal::ClosePass:
        return 2.0 * radius;
    case Goal::PlanetOrbit:
        return 1.1 * radius;
    case Goal::ReachEarth:
        return kRadiusEarth + 1000e3;
    case Goal::Reentry:
    case Goal::EarthOrbit:
        return kRadiusEarth + kCapturePerigeeAltitude;
    }
    return 10.0 * radius;
}

double departureCost(Body planet, const ParkingOrbit& orbit, double vInfinity) {
    // Vis-viva at the low point: on the escape path v² = v∞² + 2μ/r_p, on
    // the parking orbit v² = 2μ/r_p − 2μ/(r_p + r_a).
    const double mu = bodyMu(planet);
    const double rp = orbit.periapsis;
    const double escaping = std::sqrt(vInfinity * vInfinity + 2.0 * mu / rp);
    const double parked = std::sqrt(2.0 * mu / rp - 2.0 * mu / (rp + orbit.apoapsis));
    return escaping - parked;
}

double arrivalCost(Goal goal, double vInfinity, Body destination, const VoyageOptions& options) {
    if (destination == Body::Earth) return arrivalCost(goal, vInfinity, options);
    if (goal != Goal::PlanetOrbit) return 0.0;
    // Braking at the low point of the pass into a long ellipse.
    const double mu = bodyMu(destination);
    const double rp = aimPeriapsis(goal, destination);
    const double ra = captureApoapsis(destination, 0.0, options);
    const double arriving = std::sqrt(vInfinity * vInfinity + 2.0 * mu / rp);
    const double captured = std::sqrt(2.0 * mu / rp - 2.0 * mu / (rp + ra));
    return arriving - captured;
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
    case Goal::ReachPlanet:
    case Goal::ClosePass:
    case Goal::PlanetOrbit:
        break;
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
    case Goal::ReachPlanet:
    case Goal::ClosePass:
    case Goal::PlanetOrbit:
        break;
    }
    return 0.0;
}

std::optional<TransferPlan> cheapestWayHome(double epoch, const Vec3& r, const Vec3& v, Goal goal,
                                            double window, double longestFlight) {
    return cheapestTransfer(epoch, r, v, {}, Body::Earth, goal, window, longestFlight);
}

std::optional<TransferPlan> cheapestTransfer(double epoch, const Vec3& r, const Vec3& v,
                                             const DepartureFrom& from, Body to, Goal goal,
                                             double window, double longestFlight) {
    const PlanetTable target(to, epoch, window + longestFlight + 10.0 * kDay);
    const TwoBodyState start{r, v};
    std::optional<TransferPlan> best;
    const auto consider = [&](const TwoBodyState& ship, double departure, double flight) {
        if (flight < kShortestFlight) return;
        const auto plan = transfer(ship, from, departure, flight, target, to, goal);
        if (plan && (!best || plan->total() < best->total())) best = plan;
    };
    // Where the ship leaves from: its own orbit, or the planet it's parked
    // around (it leaves from there, at the cost of climbing out).
    const auto shipAt = [&](double departure) -> TwoBodyState {
        if (from.planet) {
            return {bodyPosition(*from.planet, departure), bodyVelocity(*from.planet, departure)};
        }
        return propagateKepler(start, departure - epoch, kMuSun);
    };

    // A coarse grid over (departure, flight time): the porkchop plot. The
    // flight times are 5 days apart, or wider for trips of decades (the
    // cost changes more slowly the longer the trip)...
    constexpr double kDepartureStep = 4.0 * kDay;
    const double flightStep = std::max(5.0 * kDay, (longestFlight - kShortestFlight) / 600.0);
    const int departures = static_cast<int>(window / kDepartureStep);
    const int flights = static_cast<int>((longestFlight - kShortestFlight) / flightStep);
    for (int i = 0; i <= departures; ++i) {
        const double departure = epoch + i * kDepartureStep;
        const TwoBodyState ship = shipAt(departure);
        for (int j = 0; j <= flights; ++j) {
            consider(ship, departure, kShortestFlight + j * flightStep);
        }
    }
    if (!best) return std::nullopt;
    // ...then a finer one around its lowest point, a quarter of a day apart
    // in departure, and a twentieth of the coarse step in flight time.
    const TransferPlan coarse = *best;
    const double coarseFlight = coarse.arrival - coarse.departure;
    constexpr double kFine = 0.25 * kDay;
    const double fineFlight = flightStep / 20.0;
    for (int i = -16; i <= 16; ++i) {
        const double departure = coarse.departure + i * kFine;
        if (departure < epoch) continue;
        const TwoBodyState ship = shipAt(departure);
        for (int j = -20; j <= 20; ++j) consider(ship, departure, coarseFlight + j * fineFlight);
    }
    return best;
}

SearchSpan searchSpan(const Vec3& r, const Vec3& v, Body to) {
    const OrbitalElements el = elementsFromState(r, v, kMuSun, kRadiusSun);
    const PlanetOrbit target = planetOrbit(to, 0.0);
    const double targetPeriod = kTwoPi * std::sqrt(target.semiMajorAxis * target.semiMajorAxis *
                                                   target.semiMajorAxis / kMuSun);
    SearchSpan span;
    // The synodic period: the ship gains (or loses) a lap on the target at
    // the rate 1/T − 1/T_target.
    const double lapRate = el.bound ? std::abs(1.0 / el.period - 1.0 / targetPeriod) : 0.0;
    span.window = lapRate > 0.0 ? std::clamp(1.1 / lapRate, kYear, 2.2 * kYear) : kYear;
    // Half the period of the transfer ellipse (Kepler's third law), between
    // the ship's orbit and the target's, from the far side of the ship's.
    const double far = el.bound ? el.apoapsis : norm(r);
    const double near = el.bound ? el.periapsis : norm(r);
    const double a =
        0.5 * (std::max(far, target.semiMajorAxis) + std::min(near, target.semiMajorAxis));
    const double hohmann = kPi * std::sqrt(a * a * a / kMuSun);
    span.longestFlight = std::clamp(1.3 * hohmann, 540.0 * kDay, kLongestTrip);
    return span;
}

Puzzle makePuzzle(Difficulty difficulty, std::uint64_t seed, double epoch, Region region) {
    return makePuzzle({difficulty, region, false, Body::Earth}, seed, epoch);
}

PuzzleSpec settledSpec(PuzzleSpec spec) {
    const auto planet = regionPlanet(spec.from);
    if (!planet) spec.inOrbit = false;              // nothing to orbit
    if (planet == Body::Earth) spec.inOrbit = true; // never "near" the Earth
    if (planet == spec.to) spec.to = spec.to == Body::Earth ? Body::Mars : Body::Earth;
    return spec;
}

namespace {

/// Fill the tanks for the budget: m_prop = m_dry (e^(Δv/v_e) − 1).
void fillTanks(Puzzle& puzzle) {
    Ship& ship = puzzle.voyage.ship;
    ship.propellant = ship.dryMass * (std::exp(puzzle.budget / exhaustVelocity(ship.isp)) - 1.0);
}

/// A puzzle starting in orbit around a planet, set up as if the rocket had
/// launched at just the right time: the departure on the cheapest day
/// starts from the orbit's low point, in its plane.
Puzzle orbitPuzzle(Puzzle puzzle, const Setup& setup, Random& random, double epoch) {
    const Body home = regionPlanet(puzzle.region).value_or(Body::Earth);
    const Body to = puzzle.destination;
    const ParkingOrbit orbit = parkingOrbit(home);
    const Vec3 homeR = bodyPosition(home, epoch);
    const Vec3 homeV = bodyVelocity(home, epoch);
    const SearchSpan span = searchSpan(homeR, homeV, to);
    const auto plan = cheapestTransfer(epoch, homeR, homeV, {home, orbit, {}}, to,
                                       puzzle.voyage.goal, span.window, span.longestFlight);
    if (!plan) return puzzle;

    // The escape path leaves along v∞, turned from its low point by ν∞,
    // cos ν∞ = −1/e, e = 1 + r_p v∞²/μ. In the plane that holds v∞ and is
    // as close to the ecliptic as that allows (normal n̂ = ẑ minus its part
    // along v̂∞), the low point is p̂ = cos ν∞ v̂∞ − sin ν∞ (n̂ × v̂∞), and
    // the ship moves along n̂ × p̂ there.
    const double mu = bodyMu(home);
    const double vInf = norm(plan->departureDeltaV);
    const Vec3 vHat = plan->departureDeltaV / vInf;
    Vec3 n = Vec3{0.0, 0.0, 1.0} - vHat.z * vHat;
    n = norm(n) > 1e-6 ? normalized(n) : Vec3{1.0, 0.0, 0.0};
    const double e = 1.0 + orbit.periapsis * vInf * vInf / mu;
    const double nuInf = std::acos(-1.0 / e);
    const Vec3 pHat = std::cos(nuInf) * vHat - std::sin(nuInf) * cross(n, vHat);
    const Vec3 qHat = cross(n, pHat);
    const double vPark =
        std::sqrt(2.0 * mu / orbit.periapsis - 2.0 * mu / (orbit.periapsis + orbit.apoapsis));

    // The game starts a few weeks before, the ship going round meanwhile
    // (Kepler's problem, backwards from the low point on departure day).
    const double start = std::max(epoch, plan->departure - random.uniform(10.0, 30.0) * kDay);
    const TwoBodyState rel =
        propagateKepler({orbit.periapsis * pHat, vPark * qHat}, start - plan->departure, mu);
    puzzle.voyage.startEpoch = start;
    puzzle.voyage.position = bodyPosition(home, start) + rel.r;
    puzzle.voyage.velocity = bodyVelocity(home, start) + rel.v;
    puzzle.voyage.lookAhead = span.longestFlight + (plan->departure - start);
    puzzle.plan = *plan;
    puzzle.budget = setup.margin * plan->total() + setup.reserve;
    fillTanks(puzzle);
    return puzzle;
}

} // namespace

Puzzle makePuzzle(const PuzzleSpec& spec, std::uint64_t seed, double epoch) {
    const PuzzleSpec settled = settledSpec(spec);
    const Setup setup = setupFor(settled.difficulty);
    const Body to = settled.to;
    const Goal goal = goalFor(settled.difficulty, to);
    const Region region = settled.from;
    Random random(seed);
    Puzzle puzzle;
    puzzle.difficulty = settled.difficulty;
    puzzle.region = region;
    puzzle.inOrbit = settled.inOrbit;
    puzzle.destination = to;
    puzzle.seed = seed;
    puzzle.voyage.goal = goal;
    puzzle.voyage.destination = to;
    if (settled.inOrbit) return orbitPuzzle(puzzle, setup, random, epoch);
    // The difficulty sets the cost range only for the old game: lost
    // between the planets, going home.
    const bool ranged = region == Region::BetweenPlanets && to == Body::Earth;

    // Try random orbits until one's cheapest way there is in the
    // difficulty's range (elsewhere, any way will do); if none is, keep the
    // one closest to it.
    std::optional<Puzzle> fallback;
    double fallbackOff = 0.0;
    for (int attempt = 0; attempt < 40; ++attempt) {
        const PositionVelocity pv = randomStart(region, setup, random, epoch);
        // Not too close to the destination already.
        if (norm(pv.r - bodyPosition(to, epoch)) < 0.1 * kAstronomicalUnit) continue;

        const SearchSpan span =
            ranged ? SearchSpan{setup.window, setup.longestFlight} : searchSpan(pv.r, pv.v, to);
        const auto plan =
            cheapestTransfer(epoch, pv.r, pv.v, {}, to, goal, span.window, span.longestFlight);
        if (!plan) continue;

        Puzzle candidate = puzzle;
        candidate.voyage.startEpoch = epoch;
        candidate.voyage.position = pv.r;
        candidate.voyage.velocity = pv.v;
        // Predictions look far enough ahead for the longest trip searched.
        candidate.voyage.lookAhead = span.longestFlight;
        candidate.plan = *plan;

        const double cost = plan->total();
        const double off =
            ranged ? std::max({0.0, setup.minCost - cost, cost - setup.maxCost}) : 0.0;
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
    if (const auto burn = burnTowards(chosen.voyage, plan.departure, plan.arrival)) {
        plan.departureCost = std::max(plan.departureCost, burnSize(*burn));
    }
    chosen.budget = setup.margin * plan.total() + setup.reserve;
    fillTanks(chosen);
    return chosen;
}

std::optional<Burn> burnTowardsEarth(const Voyage& voyage, double epoch, double arrival,
                                     const VoyageOptions& options) {
    Voyage home = voyage;
    home.destination = Body::Earth;
    return burnTowards(home, epoch, arrival, options);
}

std::optional<Burn> burnTowards(const Voyage& voyage, double epoch, double arrival,
                                const VoyageOptions& options) {
    if (arrival <= epoch) return std::nullopt;
    const VoyageSample here = voyageStateAt(burnsBefore(voyage, epoch, false), epoch, options);
    if (dominantPlanet(epoch, here.r)) {
        return std::nullopt; // the Sun-only Lambert solution means nothing here
    }
    const Body to = voyage.destination;
    const Vec3 targetR = bodyPosition(to, arrival);
    const Vec3 targetV = bodyVelocity(to, arrival);

    // Aim beside the planet, square to the arrival direction, on the side
    // that makes the pass go round it the way it turns (anticlockwise seen
    // from the north). The arrival direction depends on the aim point, so
    // aim, solve, and aim again from the new direction.
    Vec3 target = targetR;
    LambertSolution lambert;
    for (int i = 0; i < 3; ++i) {
        const auto solution = solveLambert(here.r, target, arrival - epoch, kMuSun);
        if (!solution) return std::nullopt;
        lambert = *solution;
        const Vec3 vInfinity = lambert.v2 - targetV;
        const Vec3 side = normalized(cross(normalized(vInfinity), Vec3{0.0, 0.0, 1.0}));
        const double b =
            to == Body::Earth
                ? aimImpactParameter(voyage.goal, norm(vInfinity), options)
                : impactParameter(aimPeriapsis(voyage.goal, to), norm(vInfinity), bodyMu(to));
        target = targetR + b * side;
    }
    return burnFromVector(epoch, lambert.v1 - here.v, burnFrameAt(epoch, here.r, here.v));
}

std::optional<Escape> escapeFrom(const TwoBodyState& at, const Vec3& vInfinity, double mu) {
    // The escape path with its low point here lies in the plane of r̂ and
    // v̂∞, moving along q̂ (square to r̂, towards v̂∞) at √(v∞² + 2μ/r); it
    // leaves along v̂∞ only if the angle from r̂ to v̂∞ is ν∞ (cos ν∞ = −1/e,
    // e = 1 + r v∞²/μ). Missing that costs about v∞ per radian to put
    // right later, so it counts too.
    const double vInf = norm(vInfinity);
    const double r = norm(at.r);
    if (vInf <= 0.0 || r <= 0.0) return std::nullopt;
    const Vec3 vHat = vInfinity / vInf;
    const Vec3 rHat = at.r / r;
    const double cosA = std::clamp(dot(rHat, vHat), -1.0, 1.0);
    const Vec3 q = vHat - cosA * rHat;
    if (norm(q) < 1e-9) return std::nullopt;
    const double e = 1.0 + r * vInf * vInf / mu;
    const double nuInf = std::acos(-1.0 / e);
    Escape escape;
    escape.deltaV = std::sqrt(vInf * vInf + 2.0 * mu / r) * normalized(q) - at.v;
    escape.cost = norm(escape.deltaV) + vInf * std::abs(std::acos(cosA) - nuInf);
    return escape;
}

std::vector<TwoBodyState> orbitPoints(const TwoBodyState& rel, double mu, int count) {
    const OrbitalElements el = elementsFromState(rel.r, rel.v, mu, 0.0);
    std::vector<TwoBodyState> points;
    if (!el.bound) return points;
    points.reserve(static_cast<std::size_t>(count));
    for (int k = 0; k < count; ++k) {
        points.push_back(propagateKepler(rel, el.period * k / count, mu));
    }
    return points;
}

std::optional<Burn> departureBurn(const Voyage& voyage, double earliest, const Vec3& vInfinity,
                                  const VoyageOptions& options) {
    const VoyageSample here =
        voyageStateAt(burnsBefore(voyage, earliest, false), earliest, options);
    const auto planet = parkedAround(earliest, here.r, here.v);
    const double vInf = norm(vInfinity);
    if (!planet || vInf <= 0.0) return std::nullopt;
    const double mu = bodyMu(*planet);
    const TwoBodyState start{here.r - bodyPosition(*planet, earliest),
                             here.v - bodyVelocity(*planet, earliest)};
    const OrbitalElements el = elementsFromState(start.r, start.v, mu, bodyRadius(*planet));

    // Try points round one orbit (see escapeFrom): the cheapest wins.
    constexpr int kPoints = 720;
    double bestCost = 0.0;
    std::optional<Burn> best;
    for (int k = 0; k < kPoints; ++k) {
        const double t = earliest + el.period * k / kPoints;
        const TwoBodyState rel = propagateKepler(start, t - earliest, mu);
        const auto escape = escapeFrom(rel, vInfinity, mu);
        if (!escape) continue;
        const Vec3 deltaV = escape->deltaV;
        const double cost = escape->cost;
        if (!best || cost < bestCost) {
            bestCost = cost;
            const Vec3 r0 = bodyPosition(*planet, t) + rel.r;
            const Vec3 v0 = bodyVelocity(*planet, t) + rel.v;
            best = burnFromVector(t, deltaV, burnFrameAt(t, r0, v0));
        }
    }
    return best;
}

std::optional<Burn> transferBurn(const Voyage& voyage, double departure, double arrival,
                                 const VoyageOptions& options) {
    if (arrival <= departure) return std::nullopt;
    const VoyageSample here =
        voyageStateAt(burnsBefore(voyage, departure, false), departure, options);
    const auto planet = parkedAround(departure, here.r, here.v);
    if (!planet) return burnTowards(voyage, departure, arrival, options);
    // Climbing out of a parked orbit: the v∞ that Lambert's problem asks
    // for, from the planet's centre, and the best point to burn for it
    // within half an orbit either side of the planned day.
    const auto lambert =
        solveLambert(bodyPosition(*planet, departure), bodyPosition(voyage.destination, arrival),
                     arrival - departure, kMuSun);
    if (!lambert) return std::nullopt;
    const Vec3 vInfinity = lambert->v1 - bodyVelocity(*planet, departure);
    const TwoBodyState rel{here.r - bodyPosition(*planet, departure),
                           here.v - bodyVelocity(*planet, departure)};
    const double period =
        elementsFromState(rel.r, rel.v, bodyMu(*planet), bodyRadius(*planet)).period;
    double earliest = std::max(voyage.startEpoch, departure - 0.5 * period);
    for (const Burn& b : voyage.burns) {
        if (b.epoch < departure) earliest = std::max(earliest, b.epoch + 1.0);
    }
    return departureBurn(voyage, earliest, vInfinity, options);
}

std::optional<Burn> aimAtDestination(const Voyage& voyage, std::size_t index,
                                     const VoyageOptions& options) {
    if (voyage.destination == Body::Earth) return refineAim(voyage, index, options);
    const FlybyAim aim{voyage.destination, aimPeriapsis(voyage.goal, voyage.destination),
                       FlybySide::Keep};
    return aimFlyby(voyage, index, aim, options).burn;
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
    trial.lookAhead = voyage.lookAhead;
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
    const BPlane plane0 =
        bPlane(first.closest.relativePosition, first.closest.relativeVelocity, muEarth());
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
        const BPlane p =
            bPlane(result.closest.relativePosition, result.closest.relativeVelocity, muEarth());
        return std::array<double, 2>{dot(p.b, axisT) - target[0], dot(p.b, axisR) - target[1]};
    };

    std::array<double, 3> x{original.prograde, original.radial, original.normal};
    constexpr double kStep = 0.01;       // m/s, for the finite differences
    constexpr double kTolerance = 500.0; // m in the B-plane
    const auto size = [](const std::array<double, 2>& m) { return std::hypot(m[0], m[1]); };
    auto f = miss(x);
    for (int iteration = 0; iteration < 25; ++iteration) {
        if (!f) return std::nullopt;
        if (size(*f) < kTolerance) return Burn{original.epoch, x[0], x[1], x[2]};

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
        // Far from the answer (a burn deep in a giant's gravity, a trip of
        // years) the first-order step can overshoot: halve it until the
        // miss shrinks.
        bool improved = false;
        double scale = 1.0;
        for (int halving = 0; halving < 6 && !improved; ++halving, scale *= 0.5) {
            std::array<double, 3> xNew = x;
            for (std::size_t k = 0; k < 3; ++k) {
                xNew[k] += scale * (jac[0][k] * y0 + jac[1][k] * y1);
            }
            const auto next = miss(xNew);
            if (next && size(*next) < size(*f)) {
                x = xNew;
                f = next;
                improved = true;
            }
        }
        if (!improved) return std::nullopt;
    }
    return std::nullopt;
}

std::optional<Burn> captureBurn(const Voyage& voyage, const VoyageOptions& options) {
    if (voyage.destination != Body::Earth) {
        // Another planet: brake at the low point of the first pass inside
        // its sphere of influence, into a long ellipse.
        const Body planet = voyage.destination;
        Voyage trial = voyage;
        trial.goal = Goal::PlanetOrbit; // fly on past arriving, and past a close pass
        const auto pass = passAfter(flyVoyage(trial, options), planet, voyage.startEpoch);
        if (!pass || pass->vInfinity <= 0.0) return std::nullopt;
        if (pass->distance > sphereOfInfluence(planet, pass->epoch)) return std::nullopt;
        if (pass->distance < options.orbitMinPeriapsisRadii * bodyRadius(planet)) {
            return std::nullopt; // into the planet
        }
        const double mu = bodyMu(planet);
        const double rp = pass->distance;
        const double ra = std::max(rp, captureApoapsis(planet, pass->epoch, options));
        const double wanted = std::sqrt(2.0 * mu / rp - 2.0 * mu / (rp + ra));
        if (pass->speed <= wanted) return std::nullopt;
        return Burn{pass->epoch, -(pass->speed - wanted), 0.0, 0.0};
    }
    Voyage trial = voyage;
    trial.goal = Goal::EarthOrbit; // fly past the edge of the sphere of influence
    const VoyageResult result = flyVoyage(trial, options);
    const EarthApproach& c = result.closest;
    if (!c.valid || result.end == VoyageEnd::HitSun) return std::nullopt;
    if (c.distance > sphereOfInfluence(Body::Earth, c.epoch)) return std::nullopt;
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
    if (!c.valid || !result.arrival || result.end == VoyageEnd::HitSun) return std::nullopt;
    const double wanted = fastestReentryArrival(options);
    if (approachSpeed(c) <= wanted) return std::nullopt;

    // A third of the way in (in time): well inside the sphere of influence,
    // where the burn's directions are taken relative to the Earth, and
    // early enough to aim afterwards.
    const double arrival = *result.arrival;
    const double epoch = std::max(notBefore, arrival + (c.epoch - arrival) / 3.0);
    if (epoch > c.epoch - 3600.0) return std::nullopt;
    const VoyageSample here = voyageStateAt(burnsBefore(trial, epoch, false), epoch, options);
    const BurnFrame frame = burnFrameAt(epoch, here.r, here.v);
    if (frame.planet != Body::Earth) return std::nullopt;

    // From the speed here to the one that leaves v∞ = wanted:
    // v² = v∞² + 2μ/r. Prograde in the Earth's frame is along the velocity
    // relative to it, so a retrograde burn takes off exactly that much.
    const Vec3 relativeR = here.r - bodyPosition(Body::Earth, epoch);
    const Vec3 relativeV = here.v - bodyVelocity(Body::Earth, epoch);
    const double speed = norm(relativeV);
    const double target = std::sqrt(wanted * wanted + 2.0 * muEarth() / norm(relativeR));
    if (speed <= target) return std::nullopt;
    return Burn{epoch, -(speed - target), 0.0, 0.0};
}

} // namespace orbit
