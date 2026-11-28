// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/voyage.hpp"

#include "orbit/core/constants.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/gravity.hpp"
#include "orbit/core/solar_system.hpp"
#include "orbit/core/state.hpp"
#include "orbit/core/two_body.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace orbit {

namespace {

constexpr double kDay = 86400.0;
/// The longest integration step near a planet: the step size adapts
/// anyway, but the cap makes sure a step never jumps clean over a close
/// pass by the Earth, or a planet.
constexpr double kMaxStep = kDay;
/// Far from every planet (out past Jupiter, on a trip of decades), steps may
/// grow to this...
constexpr double kMaxFarStep = 30.0 * kDay;
/// ...as long as no planet could be reached in five of them: the fastest a
/// ship and a planet close on each other here is well under this.
constexpr double kFastestClosing = 60e3; ///< m/s
/// Output samples are spaced by this fraction of √(r³/μ) (a circular period
/// over 2π), around the Sun, or around the Earth when that is shorter.
constexpr double kOutputFraction = 0.005;
constexpr double kMinOutputInterval = 60.0;
constexpr double kMaxOutputInterval = 5.0 * kDay;
constexpr int kMaxSteps = 1'000'000;
/// However many flybys extend it, a prediction stops this long after the start.
constexpr double kLongestPrediction = 60.0 * 365.25 * kDay;

struct Relative {
    Vec3 r;
    Vec3 v;
};

/// Position and velocity relative to a planet.
Relative relativeTo(Body planet, double epoch, const Vec3& r, const Vec3& v) {
    return {r - bodyPosition(planet, epoch), v - bodyVelocity(planet, epoch)};
}

/// Moments the flight watches for. Each has a function g of the state that
/// falls through zero at that moment.
enum class EventKind {
    SunImpact,      ///< g = r − R_sun
    EntryInterface, ///< g = d − (R_earth + 120 km), d = distance from the Earth
    Arrival,        ///< g = d − reachRadius, for the destination
    Closest,        ///< g = −(r_rel · v_rel): the distance to a body stops shrinking
    Surface,        ///< g = d − R: hitting a planet or moon (the Earth has its air instead)
};
struct Event {
    EventKind kind;
    Body planet = Body::Earth; ///< for Closest and Surface
};

/// All of them: the Sun, the Earth's air, the destination, and for every
/// body its closest approaches and its surface.
std::vector<Event> allEvents() {
    std::vector<Event> events{
        {EventKind::SunImpact}, {EventKind::EntryInterface}, {EventKind::Arrival}};
    for (const Body body : kAllBodies) {
        if (body != Body::Earth) events.push_back({EventKind::Surface, body});
    }
    for (const Body body : kAllBodies) events.push_back({EventKind::Closest, body});
    return events;
}

/// Near a moon, a step is held to this fraction of the time to reach it at
/// the speed relative to it, so that no step carries the ship past it
/// unseen; but not below kShortestMoonStep (the step size control takes over
/// there, as the moon's pull grows).
constexpr double kMoonStepFraction = 0.25;
constexpr double kShortestMoonStep = 60.0; ///< s

/// The planets' and moons' positions and velocities at the last two epochs
/// asked for: the events ask about the same moments again and again (the
/// end of one step is the start of the next). A moon is its planet plus
/// its own orbit, so the planet is looked up once.
class PlanetCache {
public:
    const TwoBodyState& at(Body body, double t) const {
        Slot* slot = nullptr;
        for (Slot& s : m_slots) {
            if (s.t == t) slot = &s;
        }
        if (!slot) {
            slot = &m_slots.at(m_next);
            m_next = 1 - m_next;
            slot->t = t;
            slot->known.fill(false);
        }
        return fill(*slot, body, t);
    }

private:
    struct Slot {
        double t = -1e300;
        std::array<bool, kBodyCount> known{};
        std::array<TwoBodyState, kBodyCount> states{};
    };
    const TwoBodyState& fill(Slot& slot, Body body, double t) const {
        const auto i = static_cast<std::size_t>(body);
        if (slot.known.at(i)) return slot.states.at(i);
        if (isMoon(body)) {
            const TwoBodyState planet = fill(slot, planetOf(body), t);
            const MoonState moon = moonFromPlanet(body, t);
            slot.states.at(i) = {planet.r + moon.r, planet.v + moon.v};
        } else {
            slot.states.at(i) = {bodyPosition(body, t), bodyVelocity(body, t)};
        }
        slot.known.at(i) = true;
        return slot.states.at(i);
    }
    mutable std::array<Slot, 2> m_slots{};
    mutable std::size_t m_next = 0;
};

/// Each planet's sphere of influence at the start (index: Body).
std::array<double, kAllPlanets.size()> planetSpheres(double epoch) {
    std::array<double, kAllPlanets.size()> radii{};
    for (const Body planet : kAllPlanets) {
        radii.at(static_cast<std::size_t>(planet)) = sphereOfInfluence(planet, epoch);
    }
    return radii;
}

/// Can a ship parked around `body` with its far point at `apoapsis` be
/// moved by Kepler's equations alone? Not if it reaches out among the
/// moons: it must stay three of the innermost moon's spheres of influence
/// inside that moon's low point.
bool keplerWillDo(Body body, double apoapsis) {
    const auto moons = moonsOf(body);
    if (moons.empty()) return true;
    const Body inner = moons.front();
    const MoonOrbit orbit = moonOrbit(inner);
    const double innerLow = orbit.semiMajorAxis * (1.0 - orbit.eccentricity);
    return apoapsis < innerLow - 3.0 * sphereOfInfluence(inner, 0.0);
}

class VoyageFlight {
public:
    VoyageFlight(const Voyage& voyage, const VoyageOptions& options)
        : m_voyage(&voyage), m_opt(&options),
          m_stepper(
              [](double t, const State& y) {
                  return State{y.v, heliocentricGravity(y.r, t, true), 0.0};
              },
              options.tolerances),
          m_events(allEvents()), m_spheres(planetSpheres(voyage.startEpoch)),
          m_destinationSoi(sphereOfInfluence(voyage.destination, voyage.startEpoch)),
          m_destinationReach(reachRadius(voyage.destination, voyage.startEpoch)) {}

    VoyageResult run();

private:
    const Voyage* m_voyage;
    const VoyageOptions* m_opt;
    DormandPrince45 m_stepper;
    std::vector<Event> m_events;
    std::array<double, kAllPlanets.size()> m_spheres; ///< each planet's sphere of influence
    double m_destinationSoi;                          ///< the destination's
    double m_destinationReach;                        ///< reachRadius of the destination
    PlanetCache m_planets;
    double m_t = 0.0;
    State m_y;
    double m_nextOutput = 0.0;
    /// When the flight stops: the look-ahead past the last burn, pushed
    /// on by each flyby (a pass changes the path as a burn does).
    double m_end = 0.0;
    double m_lookAhead = 0.0;
    /// The body the last burn was made inside the sphere of influence of,
    /// until the ship leaves it: the low point of the path out is a
    /// departure, not a flyby.
    std::optional<Body> m_leaving;
    VoyageResult m_out;

    Relative relative(Body planet, double t, const State& y) const {
        const TwoBodyState& p = m_planets.at(planet, t);
        return {y.r - p.r, y.v - p.v};
    }
    /// Inside the planet's sphere of influence (where its moons matter)?
    bool inSystem(Body planet, double t, const State& y) const {
        return norm(relative(planet, t, y).r) < m_spheres.at(static_cast<std::size_t>(planet));
    }
    double g(Event event, double t, const State& y) const;
    double maxStep(double t, const State& y) const;
    bool coastParked(double tEnd);
    double findCrossing(Event event, double t0, double t1) const;
    double outputInterval(double t, const State& y) const;
    void addSample() { m_out.samples.push_back({m_t, m_y.r, m_y.v, m_y.m, std::nullopt}); }
    void noteApproach();
    void notePass(Body planet, bool atMinimum);
    void notePasses();
    bool coastTo(double tEnd);
    bool handle(Event event);
    void burn(const Burn& planned);
    std::optional<VoyageEnd> settled() const;
    void finish(VoyageEnd end);
};

double VoyageFlight::g(Event event, double t, const State& y) const {
    switch (event.kind) {
    case EventKind::SunImpact:
        return norm(y.r) - kRadiusSun;
    case EventKind::EntryInterface:
        return norm(relative(Body::Earth, t, y).r) - (kRadiusEarth + m_opt->entryInterfaceAltitude);
    case EventKind::Arrival:
        return norm(relative(m_voyage->destination, t, y).r) - m_destinationReach;
    case EventKind::Closest: {
        const Relative rel = relative(event.planet, t, y);
        return -dot(rel.r, rel.v);
    }
    case EventKind::Surface:
        return norm(relative(event.planet, t, y).r) - bodyRadius(event.planet);
    }
    return 1.0;
}

/// Where in [t0, t1] the event's g falls through zero, by bisection on the
/// integrator's dense output. Returns the first moment on the far side.
double VoyageFlight::findCrossing(Event event, double t0, double t1) const {
    double near = t0;
    double far = t1;
    for (int i = 0; i < 80 && far - near > 1e-6; ++i) {
        const double mid = 0.5 * (near + far);
        (g(event, mid, m_stepper.interpolate(mid)) > 0.0 ? near : far) = mid;
    }
    return far;
}

/// The longest step allowed here: a day near any planet (or the Sun), up to
/// a month far from all of them, so that a step never carries the ship past
/// a planet it could have flown close to; and among a planet's moons, a
/// fraction of the time to reach each one.
double VoyageFlight::maxStep(double t, const State& y) const {
    double nearest = norm(y.r);
    double step = kMaxFarStep;
    for (const Body planet : kAllPlanets) {
        const double d = norm(relative(planet, t, y).r);
        nearest = std::min(nearest, d);
        if (d > m_spheres.at(static_cast<std::size_t>(planet))) continue;
        for (const Body moon : moonsOf(planet)) {
            const Relative rel = relative(moon, t, y);
            const double closing = std::max(norm(rel.v), 1.0);
            step = std::min(step,
                            std::max(kShortestMoonStep, kMoonStepFraction * norm(rel.r) / closing));
        }
    }
    return std::min(step, std::clamp(0.2 * nearest / kFastestClosing, kMaxStep, kMaxFarStep));
}

/// Samples close enough to draw the path smoothly: a fraction of √(r³/μ)
/// around the Sun, or around a planet or moon whose sphere of influence the
/// ship is in, whichever is shorter.
double VoyageFlight::outputInterval(double t, const State& y) const {
    const double r = norm(y.r);
    double interval = kOutputFraction * std::sqrt(r * r * r / kMuSun);
    const auto around = [&](Body body) {
        const double d = norm(relative(body, t, y).r);
        interval = std::min(interval, kOutputFraction * std::sqrt(d * d * d / bodyMu(body)));
    };
    for (const Body planet : kAllPlanets) {
        if (!inSystem(planet, t, y)) continue;
        around(planet);
        for (const Body moon : moonsOf(planet)) {
            if (norm(relative(moon, t, y).r) < sphereOfInfluence(moon, t)) around(moon);
        }
    }
    return std::clamp(interval, kMinOutputInterval, kMaxOutputInterval);
}

/// Is this the closest to the Earth so far?
void VoyageFlight::noteApproach() {
    const Relative rel = relative(Body::Earth, m_t, m_y);
    const double d = norm(rel.r);
    EarthApproach& c = m_out.closest;
    if (c.valid && d >= c.distance) return;
    c.valid = true;
    c.epoch = m_t;
    c.distance = d;
    c.speed = norm(rel.v);
    c.relativePosition = rel.r;
    c.relativeVelocity = rel.v;
    c.orbit = elementsFromState(rel.r, rel.v, bodyMu(Body::Earth), kRadiusEarth);
}

/// A pass by a planet: the closest to it so far, and, at a closest
/// approach inside its sphere of influence, a flyby.
void VoyageFlight::notePass(Body planet, bool atMinimum) {
    PlanetPass pass = passAt(planet, m_t, m_y.r, m_y.v);
    pass.turning = atMinimum;
    PlanetPass& closest = m_out.closestPasses.at(static_cast<std::size_t>(planet));
    if (!closest.valid || pass.distance < closest.distance) closest = pass;
    // A flyby: a pass that comes in and leaves again (not each low point of
    // a ship going round and round).
    if (!atMinimum || pass.vInfinity <= 0.0 || pass.distance >= sphereOfInfluence(planet, m_t) ||
        m_leaving == planet) {
        return;
    }
    m_out.flybys.push_back(pass);
    // Look as far ahead past a flyby as past a burn. A moon's only if the
    // ship isn't still going round its planet afterwards: then nothing
    // more is to come from it.
    const bool leaves =
        !isMoon(planet) || passAt(planetOf(planet), m_t, m_y.r, m_y.v).vInfinity > 0.0;
    if (!m_opt->stopAt && leaves) {
        m_end =
            std::max(m_end, std::min(m_t + m_lookAhead, m_voyage->startEpoch + kLongestPrediction));
    }
}

/// The closest to each body so far (at the start, after a burn, and at
/// the end: the closest may be there rather than at a turning point).
void VoyageFlight::notePasses() {
    for (const Body body : kAllBodies) notePass(body, false);
}

/// Parked around a planet: move by Kepler's equations to tEnd (or the end
/// of the flight), with a sample a day (sampleAt fills in between). False
/// if not parked.
bool VoyageFlight::coastParked(double tEnd) {
    const auto planet = parkedAround(m_t, m_y.r, m_y.v);
    if (!planet) return false;
    const double mu = bodyMu(*planet);
    const Relative start = relative(*planet, m_t, m_y);
    if (!keplerWillDo(*planet, elementsFromState(start.r, start.v, mu, 0.0).apoapsis)) return false;
    const double t0 = m_t;
    const double end = std::min(tEnd, m_end);
    if (m_out.samples.empty() || m_out.samples.back().epoch != m_t) addSample();
    m_out.samples.back().parked = planet;
    const auto stateAt = [&](double t) {
        const TwoBodyState rel = propagateKepler({start.r, start.v}, t - t0, mu);
        const TwoBodyState p = m_planets.at(*planet, t);
        return State{p.r + rel.r, p.v + rel.v, m_y.m};
    };
    const auto days = static_cast<int>(std::ceil((end - t0) / kDay)) - 1;
    for (int day = 1; day <= days; ++day) {
        const double t = t0 + day * kDay;
        const State y = stateAt(t);
        m_out.samples.push_back({t, y.r, y.v, y.m, planet});
    }
    m_t = end;
    m_y = stateAt(end);
    m_nextOutput = m_t + outputInterval(m_t, m_y);
    return true;
}

/// Coast to tEnd, or to the end of the flight if that comes first. True if
/// the flight ended on the way.
bool VoyageFlight::coastTo(double tEnd) {
    if (coastParked(tEnd)) return false;
    double h = 1.0;
    for (int stepCount = 0; m_t < std::min(tEnd, m_end); ++stepCount) {
        if (stepCount >= kMaxSteps) {
            finish(VoyageEnd::Horizon);
            return true;
        }
        const double limit = std::min(tEnd, m_end);
        double hStep = std::min(h, maxStep(m_t, m_y));
        if (m_t + hStep > limit) hStep = limit - m_t;
        const auto result = m_stepper.step(m_t, m_y, hStep);
        h = result.nextStep;
        if (!result.accepted) continue;

        // The earliest event within the step, if any. A moon's only matter
        // inside its planet's sphere of influence.
        double tStop = m_t + hStep;
        std::optional<Event> fired;
        std::array<bool, kAllPlanets.size()> among{};
        for (const Body planet : kAllPlanets) {
            among.at(static_cast<std::size_t>(planet)) =
                !moonsOf(planet).empty() &&
                (inSystem(planet, m_t, m_y) || inSystem(planet, m_t + hStep, result.y));
        }
        for (const Event event : m_events) {
            if (isMoon(event.planet) &&
                !among.at(static_cast<std::size_t>(planetOf(event.planet)))) {
                continue;
            }
            if (g(event, m_t, m_y) <= 0.0 || g(event, m_t + hStep, result.y) > 0.0) continue;
            const double crossing = findCrossing(event, m_t, m_t + hStep);
            if (!fired || crossing < tStop) {
                tStop = crossing;
                fired = event;
            }
        }
        const State yStop = fired ? m_stepper.interpolate(tStop) : result.y;
        while (m_nextOutput < tStop) {
            const State y = m_stepper.interpolate(m_nextOutput);
            m_out.samples.push_back({m_nextOutput, y.r, y.v, y.m, std::nullopt});
            m_nextOutput += outputInterval(m_nextOutput, y);
        }
        m_t = tStop;
        m_y = yStop;
        if (m_leaving &&
            norm(relative(*m_leaving, m_t, m_y).r) > sphereOfInfluence(*m_leaving, m_t)) {
            m_leaving.reset();
        }
        if (fired && handle(*fired)) return true;
    }
    return false;
}

/// React to an event. True if it ends the flight.
bool VoyageFlight::handle(Event event) {
    addSample();
    switch (event.kind) {
    case EventKind::SunImpact:
        finish(VoyageEnd::HitSun);
        return true;
    case EventKind::Closest: {
        if (event.planet == Body::Earth) noteApproach();
        notePass(event.planet, true);
        // A close pass by the destination, if that's the goal.
        if (event.planet != m_voyage->destination || m_voyage->goal != Goal::ClosePass ||
            !m_opt->stopAtGoal) {
            return false;
        }
        const double d = norm(relative(event.planet, m_t, m_y).r);
        if (d > m_opt->closePassRadii * bodyRadius(event.planet)) return false;
        finish(VoyageEnd::PassedClose);
        return true;
    }
    case EventKind::Surface:
        m_out.hitPlanet = event.planet;
        // Recorded as a pass: the path's B-plane is still well defined, so
        // aiming can tell how far off it is.
        notePass(event.planet, true);
        finish(VoyageEnd::HitPlanet);
        return true;
    case EventKind::Arrival:
        if (!m_out.arrival) m_out.arrival = m_t;
        if (!m_opt->stopAtGoal) return false;
        if (m_voyage->goal == Goal::ReachEarth) {
            finish(VoyageEnd::ReachedEarth);
            return true;
        }
        if (m_voyage->goal == Goal::ReachPlanet) {
            finish(VoyageEnd::ReachedPlanet);
            return true;
        }
        return false;
    case EventKind::EntryInterface: {
        // The flight-path angle γ is the angle between the velocity and the
        // local horizontal: sin γ = (r · v) / (|r| |v|), negative coming down.
        noteApproach();
        const Relative rel = relative(Body::Earth, m_t, m_y);
        m_out.entrySpeed = norm(rel.v);
        m_out.entryAngle = std::asin(dot(rel.r, rel.v) / (norm(rel.r) * m_out.entrySpeed));
        if (m_out.entrySpeed > m_opt->fastestEntry) {
            finish(VoyageEnd::TooFast);
        } else if (m_out.entryAngle < m_opt->steepestEntry) {
            finish(VoyageEnd::TooSteep);
        } else if (m_out.entryAngle > m_opt->shallowestEntry) {
            finish(VoyageEnd::Skipped);
        } else {
            finish(VoyageEnd::Reentered);
        }
        return true;
    }
    }
    return false;
}

void VoyageFlight::burn(const Burn& planned) {
    addSample(); // before: the path has a corner here
    const BurnFrame frame = burnFrameAt(m_t, m_y.r, m_y.v);
    Vec3 deltaV = burnVector(planned, frame);
    double size = norm(deltaV);

    // Tsiolkovsky: what is left in the tank is worth v_e ln(m / m_dry).
    const Ship& ship = m_voyage->ship;
    const double ve = exhaustVelocity(ship.isp);
    const double available = deltaVAvailable(ship, m_y.m);
    BurnReport report{planned, frame.planet, size, 0.0, false};
    if (size > available) {
        deltaV = size > 0.0 ? deltaV * (available / size) : deltaV;
        size = available;
        report.deltaV = available;
        report.shortOfFuel = true;
    }
    // m_after = m e^(−Δv / v_e): the rocket equation turned round.
    const double massAfter = ship.unlimitedFuel ? m_y.m : m_y.m * std::exp(-size / ve);
    report.propellantUsed = m_y.m - massAfter;
    m_out.deltaVUsed += size;
    // Leaving an orbit (bound around the body before the burn): its low
    // point on the way out is no flyby. (A burn on the way in to a pass
    // doesn't hide the pass.)
    if (frame.planet) {
        const Relative rel = relative(*frame.planet, m_t, m_y);
        const double energy = 0.5 * normSquared(rel.v) - bodyMu(*frame.planet) / norm(rel.r);
        if (energy < 0.0) m_leaving = frame.planet;
    }
    m_y.v += deltaV;
    m_y.m = massAfter;
    m_out.burns.push_back(report);
    addSample();
}

/// With no burns to come: left on a closed orbit around the destination
/// that clears its air (or surface), so nothing more will happen?
std::optional<VoyageEnd> VoyageFlight::settled() const {
    if (m_voyage->goal == Goal::Explore) return std::nullopt; // free roam: nowhere to settle
    const Body planet = m_voyage->destination;
    const Relative rel = relative(planet, m_t, m_y);
    if (norm(rel.r) > m_destinationSoi) return std::nullopt;
    const double radius = bodyRadius(planet);
    const OrbitalElements el = elementsFromState(rel.r, rel.v, bodyMu(planet), radius);
    if (!el.bound || el.periapsis < lowestOrbit(planet, *m_opt)) return std::nullopt;
    if (planet == Body::Earth) {
        if (el.apoapsis <= m_opt->captureMaxApogee) return VoyageEnd::Captured;
        return std::nullopt;
    }
    if (el.apoapsis <= m_destinationSoi) return VoyageEnd::OrbitingPlanet;
    return std::nullopt;
}

void VoyageFlight::finish(VoyageEnd end) {
    m_out.end = end;
    m_out.endEpoch = m_t;
}

VoyageResult VoyageFlight::run() {
    std::vector<Burn> burns;
    for (const Burn& b : m_voyage->burns) {
        if (b.epoch >= m_voyage->startEpoch) burns.push_back(b);
    }
    std::ranges::stable_sort(burns, {}, &Burn::epoch);

    const double lastEvent = burns.empty() ? m_voyage->startEpoch : burns.back().epoch;
    const double horizon = m_voyage->horizon > 0.0 ? m_voyage->horizon : m_opt->horizon;
    m_lookAhead = std::max(horizon, m_voyage->lookAhead);
    m_end = m_opt->stopAt ? *m_opt->stopAt : lastEvent + m_lookAhead;

    const Ship& ship = m_voyage->ship;
    m_t = m_voyage->startEpoch;
    m_y = {m_voyage->position, m_voyage->velocity, ship.dryMass + ship.propellant};
    m_nextOutput = m_t + outputInterval(m_t, m_y);
    addSample();
    noteApproach();
    notePasses();

    bool ended = false;
    for (std::size_t i = 0; i < burns.size() && !ended; ++i) {
        if (burns[i].epoch > m_end) break;
        ended = coastTo(burns[i].epoch);
        if (ended) break;
        burn(burns[i]);
        noteApproach();
        notePasses();
        if (i + 1 == burns.size() && m_opt->stopAtGoal) {
            if (const auto end = settled()) {
                finish(*end);
                ended = true;
            }
        }
    }
    if (!ended) {
        ended = coastTo(m_end);
        if (!ended) {
            addSample();
            noteApproach();
            notePasses();
            finish(m_opt->stopAt ? VoyageEnd::StopTime : VoyageEnd::Horizon);
        }
    }

    if (ended) notePasses(); // the closest may be where it ended (in the air, say)
    m_out.goalMet = goalMet(m_voyage->goal, m_out.end);
    m_out.propellantLeft = m_y.m - ship.dryMass;
    m_out.deltaVLeft = deltaVAvailable(ship, m_y.m);
    return std::move(m_out);
}

} // namespace

PlanetPass passAt(Body planet, double epoch, const Vec3& r, const Vec3& v) {
    PlanetPass pass;
    pass.valid = true;
    pass.planet = planet;
    pass.epoch = epoch;
    const Relative rel = relativeTo(planet, epoch, r, v);
    pass.relativePosition = rel.r;
    pass.relativeVelocity = rel.v;
    pass.distance = norm(rel.r);
    pass.speed = norm(rel.v);
    pass.sunSpeedBefore = pass.sunSpeedAfter =
        isMoon(planet) ? norm(v - bodyVelocity(planetOf(planet), epoch)) : norm(v);

    // The path around the planet as if only it pulled: a hyperbola if the
    // ship comes from far away and leaves again (energy v²/2 − μ/r > 0).
    const double mu = bodyMu(planet);
    const double v2 = pass.speed * pass.speed;
    const double energy = v2 / 2.0 - mu / pass.distance;
    const Vec3 h = cross(rel.r, rel.v);
    if (energy <= 0.0 || norm(h) == 0.0) return pass;
    const Vec3 eVec = ((v2 - mu / pass.distance) * rel.r - dot(rel.r, rel.v) * rel.v) / mu;
    const double e = norm(eVec);
    if (e <= 1.0) return pass;
    pass.vInfinity = std::sqrt(2.0 * energy);
    // The asymptotes are at true anomaly ±ν∞, cos ν∞ = −1/e: the ship
    // comes in along ŝ_in = ê/e + √(1 − 1/e²) (ĥ × ê) and leaves along
    // ŝ_out = −ê/e + √(1 − 1/e²) (ĥ × ê), turned by δ = 2 asin(1/e).
    const Vec3 eHat = eVec / e;
    const Vec3 across = std::sqrt(1.0 - 1.0 / (e * e)) * cross(normalized(h), eHat);
    const Vec3 in = eHat / e + across;
    const Vec3 out = -eHat / e + across;
    pass.turnAngle = 2.0 * std::asin(1.0 / e);
    // Around the Sun, the planet's velocity plus v∞ each way: the pass
    // leaves the speed relative to the planet unchanged but turns it, so
    // the speed around the Sun changes (patched conics). A moon does the
    // same around its planet.
    const Vec3 planetV = isMoon(planet)
                             ? bodyVelocity(planet, epoch) - bodyVelocity(planetOf(planet), epoch)
                             : bodyVelocity(planet, epoch);
    pass.sunSpeedBefore = norm(planetV + pass.vInfinity * in);
    pass.sunSpeedAfter = norm(planetV + pass.vInfinity * out);
    return pass;
}

double deltaVCapacity(const Ship& ship) {
    return exhaustVelocity(ship.isp) * std::log((ship.dryMass + ship.propellant) / ship.dryMass);
}

double deltaVAvailable(const Ship& ship, double mass) {
    if (ship.unlimitedFuel) return std::numeric_limits<double>::infinity();
    return exhaustVelocity(ship.isp) * std::log(mass / ship.dryMass);
}

double propellantFor(double deltaV, double mass, double isp) {
    return mass * (1.0 - std::exp(-deltaV / exhaustVelocity(isp)));
}

double burnSize(const Burn& burn) {
    return std::sqrt(burn.prograde * burn.prograde + burn.radial * burn.radial +
                     burn.normal * burn.normal);
}

double reachRadius(Body body, double epoch) {
    return std::max(sphereOfInfluence(body, epoch), 10.0 * bodyRadius(body));
}

double lowestOrbit(Body body, const VoyageOptions& options) {
    const double radius = bodyRadius(body);
    switch (body) {
    case Body::Earth:
        return radius + options.captureMinPerigeeAltitude;
    case Body::Titan: // its haze reaches about 1,200 km up
        return radius + 1200e3;
    default:
        return options.orbitMinPeriapsisRadii * radius;
    }
}

std::optional<Body> dominantBody(double epoch, const Vec3& r) {
    // The planet first (their spheres don't overlap), then one of its
    // moons, whose spheres lie inside the planet's.
    std::optional<Body> best;
    Vec3 bestPosition;
    double bestFraction = 1.0;
    for (const Body planet : kAllPlanets) {
        const Vec3 p = bodyPosition(planet, epoch);
        const double fraction = norm(r - p) / sphereOfInfluence(planet, epoch);
        if (fraction < bestFraction) {
            best = planet;
            bestPosition = p;
            bestFraction = fraction;
        }
    }
    if (!best) return best;
    for (const Body moon : moonsOf(*best)) {
        const Vec3 m = bestPosition + moonFromPlanet(moon, epoch).r;
        if (norm(r - m) < sphereOfInfluence(moon, epoch)) return moon;
    }
    return best;
}

std::optional<Body> parkedAround(double epoch, const Vec3& r, const Vec3& v) {
    const auto planet = dominantBody(epoch, r);
    if (!planet) return std::nullopt;
    const Relative rel = relativeTo(*planet, epoch, r, v);
    const double radius = bodyRadius(*planet);
    const OrbitalElements el = elementsFromState(rel.r, rel.v, bodyMu(*planet), radius);
    if (!el.bound || el.apoapsis > 0.5 * sphereOfInfluence(*planet, epoch)) return std::nullopt;
    // Clear of the ground, and of the Earth's air (where re-entry starts).
    const double floor = *planet == Body::Earth ? radius + 120e3 : radius;
    if (el.periapsis < floor) return std::nullopt;
    return planet;
}

VoyageSample sampleAt(const VoyageResult& result, double epoch) {
    const auto& samples = result.samples;
    if (samples.empty()) return {};
    if (epoch <= samples.front().epoch) return samples.front();
    if (epoch >= samples.back().epoch) return samples.back();
    const auto after = std::ranges::upper_bound(samples, epoch, {}, &VoyageSample::epoch);
    const VoyageSample& b = *after;
    const VoyageSample& a = *(after - 1);
    if (a.parked) {
        // Kepler's equations around the planet, from the sample before.
        const Body planet = *a.parked;
        const Vec3 pa = bodyPosition(planet, a.epoch);
        const Vec3 va = bodyVelocity(planet, a.epoch);
        const TwoBodyState rel =
            propagateKepler({a.r - pa, a.v - va}, epoch - a.epoch, bodyMu(planet));
        return {epoch, bodyPosition(planet, epoch) + rel.r, bodyVelocity(planet, epoch) + rel.v,
                a.mass, a.parked};
    }
    const double span = b.epoch - a.epoch;
    const double f = span > 0.0 ? (epoch - a.epoch) / span : 1.0;
    return {epoch, a.r + f * (b.r - a.r), a.v + f * (b.v - a.v), a.mass + f * (b.mass - a.mass),
            std::nullopt};
}

BurnFrame burnFrameAt(double epoch, const Vec3& r, const Vec3& v) {
    BurnFrame frame;
    Relative rel{r, v};
    if (const auto planet = dominantBody(epoch, r)) {
        frame.planet = planet;
        rel = relativeTo(*planet, epoch, r, v);
    }
    // prograde along the velocity, normal along r × v (the orbit's pole),
    // and radial completing the set: prograde × normal points away from the
    // body, perpendicular to the velocity.
    frame.prograde = normalized(rel.v);
    frame.normal = normalized(cross(rel.r, rel.v));
    frame.radial = cross(frame.prograde, frame.normal);
    return frame;
}

Vec3 burnVector(const Burn& burn, const BurnFrame& frame) {
    return burn.prograde * frame.prograde + burn.radial * frame.radial + burn.normal * frame.normal;
}

Burn burnFromVector(double epoch, const Vec3& deltaV, const BurnFrame& frame) {
    return {epoch, dot(deltaV, frame.prograde), dot(deltaV, frame.radial),
            dot(deltaV, frame.normal)};
}

std::string_view describe(VoyageEnd end) {
    switch (end) {
    case VoyageEnd::Horizon:
        return "Still out there";
    case VoyageEnd::StopTime:
        return "Flying";
    case VoyageEnd::ReachedEarth:
        return "Reached the Earth";
    case VoyageEnd::Reentered:
        return "Re-entered safely";
    case VoyageEnd::TooSteep:
        return "Came in too steeply and burned up";
    case VoyageEnd::TooFast:
        return "Came in too fast and burned up";
    case VoyageEnd::Skipped:
        return "Skipped off the atmosphere";
    case VoyageEnd::Captured:
        return "In orbit around the Earth";
    case VoyageEnd::HitSun:
        return "Fell into the Sun";
    case VoyageEnd::ReachedPlanet:
        return "Arrived";
    case VoyageEnd::PassedClose:
        return "Flew close past it";
    case VoyageEnd::OrbitingPlanet:
        return "In orbit around it";
    case VoyageEnd::HitPlanet:
        return "Crashed into a planet";
    }
    return "";
}

bool goalMet(Goal goal, VoyageEnd end) {
    switch (goal) {
    case Goal::ReachEarth:
        return end == VoyageEnd::ReachedEarth;
    case Goal::Reentry:
        return end == VoyageEnd::Reentered;
    case Goal::EarthOrbit:
        return end == VoyageEnd::Captured;
    case Goal::ReachPlanet:
        return end == VoyageEnd::ReachedPlanet;
    case Goal::ClosePass:
        return end == VoyageEnd::PassedClose;
    case Goal::PlanetOrbit:
        return end == VoyageEnd::OrbitingPlanet;
    case Goal::Explore:
        return false;
    }
    return false;
}

VoyageResult flyVoyage(const Voyage& voyage, const VoyageOptions& options) {
    return VoyageFlight(voyage, options).run();
}

VoyageSample voyageStateAt(const Voyage& voyage, double epoch, const VoyageOptions& options) {
    VoyageOptions stop = options;
    stop.stopAt = epoch;
    stop.stopAtGoal = false;
    VoyageResult result = flyVoyage(voyage, stop);
    return result.samples.back();
}

} // namespace orbit
