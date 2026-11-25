// SPDX-License-Identifier: GPL-3.0-or-later
// A natural body's flight: it coasts under gravity, nothing else.
//
// The body is placed at its epoch (natural_body.hpp), so the simulation
// runs in two passes from there: backwards in time over the span before the
// epoch, then forwards over the span after it. The integrator doesn't mind
// which way time runs: a negative step size integrates backwards, because
// gravity, like all of Newton's mechanics, works the same both ways.
// (Running the film backwards shows a body leaving along the path it came
// in on.)
//
// AROUND THE EARTH (Earth-centred, equator of date), it watches for
//   impact            the distance from the Earth's centre falls to its radius
//   closest approach  r·v changes sign from − to +: the distance stops
//                     falling and starts rising (a periapsis)
//   capture/release   the energy relative to the Earth, ε = v²/2 − μ/r,
//                     changes sign. Below zero the body can't get away from
//                     the Earth on its own: it is bound, a temporary moon.
//                     (The Sun's tide can still pull it free again, which is
//                     how "mini-moons" come and go.)
//   the Moon          its sphere of influence, closest approach and surface
// AROUND THE SUN (Sun-centred, ecliptic J2000), it watches for
//   impact            on the Sun or on any planet
//   perihelion        r·v from − to +, and aphelion, from + to −
//   a planet          its closest approach, while inside the planet's sphere
//                     of influence, r_SOI = a (m_planet / m_sun)^(2/5): where
//                     the planet's pull, not the Sun's, shapes the path
// Each is a function g(t, state) whose sign change is pinned down by
// bisection on the integrator's dense output, as for the rocket.
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/gravity.hpp"
#include "orbit/core/simulation.hpp"
#include "orbit/core/solar_system.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace orbit {

namespace {

/// Things to watch for. Each g is negative before and positive after the
/// event, in the direction of time given in the comment.
enum class Kind {
    // These end the pass. g > 0 is the forbidden side, whichever way time
    // runs: going backwards, an "impact" means the body rose from the ground.
    CentreImpact,  ///< R − r (the Earth's surface, or the Sun's)
    MoonImpact,    ///< R_moon − d
    PlanetImpact,  ///< R_planet − d
    DistanceLimit, ///< r − limit
    // These are events, defined forward in time.
    Periapsis,       ///< r·v
    Apoapsis,        ///< −r·v (only around the Sun: aphelion)
    Capture,         ///< −ε, relative to the Earth
    Release,         ///< ε
    EnterMoonSphere, ///< SOI − d
    LeaveMoonSphere, ///< d − SOI
    PlanetPeriapsis, ///< (r − r_p)·(v − v_p) inside the planet's SOI; −1 outside
};

struct Trigger {
    Kind kind;
    Planet planet = Planet::Earth; ///< for the planet triggers
};

bool endsPass(Kind kind) {
    return kind == Kind::CentreImpact || kind == Kind::MoonImpact || kind == Kind::PlanetImpact ||
           kind == Kind::DistanceLimit;
}

/// One pass, forwards or backwards from the epoch. Times here are τ, seconds
/// from the epoch (negative before it).
class BodyPass {
public:
    BodyPass(const Mission& mission, const SimOptions& options, double direction)
        : m_body(&mission.body), m_opt(&options), m_direction(direction),
          m_sun(mission.body.centre == BodyCentre::Sun),
          m_stepper([this](double t, const State& y) { return derivative(t, y); },
                    options.tolerances) {
        buildTriggers();
    }

    struct Output {
        std::vector<Sample> samples; ///< in the order flown (backwards: latest first)
        std::vector<FlightEvent> events;
        EndReason endReason = EndReason::SpanEnd;
        std::string impacted; ///< what it hit, if it did
    };

    /// Fly from `start` at τ0 until τ_end (earlier than τ0 going backwards).
    Output run(const State& start, double tau0, double tauEnd);

private:
    const NaturalBody* m_body;
    const SimOptions* m_opt;
    double m_direction;  ///< +1 forwards in time, −1 backwards
    bool m_sun;          ///< around the Sun, rather than the Earth
    double m_tau0 = 0.0; ///< where the pass started
    std::vector<Trigger> m_triggers;
    std::array<double, 8> m_soi{}; ///< each planet's sphere of influence
    DormandPrince45 m_stepper;
    Output m_out;
    double m_tau = 0.0;
    State m_y;

    double epochAt(double tau) const { return m_body->epoch + tau; }
    double centreRadius() const { return m_sun ? kRadiusSun : kRadiusEarth; }
    Vec3 moonAt(double tau) const { return moonPosition(epochAt(tau)); }
    State derivative(double tau, const State& y) const {
        const double epoch = epochAt(tau);
        const bool third = m_opt->thirdBodyGravity;
        return {y.v,
                m_sun ? heliocentricGravity(y.r, epoch, third)
                      : gravityAcceleration(y.r, epoch, third),
                0.0};
    }
    void buildTriggers();
    double g(const Trigger& trigger, double tau, const State& y) const;
    double outputInterval(double tau, const State& y) const;
    double findCrossing(const Trigger& trigger, double tau0, double tau1) const;
    void record(EventType type, std::string description);
};

void BodyPass::buildTriggers() {
    const bool third = m_opt->thirdBodyGravity;
    m_triggers = {{Kind::CentreImpact}, {Kind::DistanceLimit}, {Kind::Periapsis}};
    if (m_sun) {
        m_triggers.push_back({Kind::Apoapsis});
        if (!third) return;
        for (const Planet planet : kAllPlanets) {
            m_triggers.push_back({Kind::PlanetImpact, planet});
            m_triggers.push_back({Kind::PlanetPeriapsis, planet});
            m_soi.at(static_cast<std::size_t>(planet)) = sphereOfInfluence(planet, m_body->epoch);
        }
        return;
    }
    m_triggers.push_back({Kind::Capture});
    m_triggers.push_back({Kind::Release});
    if (!third) return;
    m_triggers.push_back({Kind::MoonImpact});
    m_triggers.push_back({Kind::EnterMoonSphere});
    m_triggers.push_back({Kind::LeaveMoonSphere});
}

double BodyPass::g(const Trigger& trigger, double tau, const State& y) const {
    const auto energy = [&] { return normSquared(y.v) / 2.0 - kMuEarth / norm(y.r); };
    const double epoch = epochAt(tau);
    switch (trigger.kind) {
    case Kind::CentreImpact:
        return centreRadius() - norm(y.r);
    case Kind::MoonImpact:
        return kRadiusMoon - norm(y.r - moonAt(tau));
    case Kind::PlanetImpact:
        return planetRadius(trigger.planet) - norm(y.r - planetPosition(trigger.planet, epoch));
    case Kind::DistanceLimit:
        return norm(y.r) - (m_sun ? m_opt->heliocentricDistanceLimit : m_opt->bodyDistanceLimit);
    case Kind::Periapsis:
        return dot(y.r, y.v);
    case Kind::Apoapsis:
        return -dot(y.r, y.v);
    case Kind::Capture:
        return -energy();
    case Kind::Release:
        return energy();
    case Kind::EnterMoonSphere:
        return kMoonSphereOfInfluence - norm(y.r - moonAt(tau));
    case Kind::LeaveMoonSphere:
        return norm(y.r - moonAt(tau)) - kMoonSphereOfInfluence;
    case Kind::PlanetPeriapsis: {
        const Vec3 rel = y.r - planetPosition(trigger.planet, epoch);
        if (norm(rel) > m_soi.at(static_cast<std::size_t>(trigger.planet))) return -1.0;
        return dot(rel, y.v - planetVelocity(trigger.planet, epoch));
    }
    }
    return -1.0;
}

/// Samples a fixed fraction of the local orbital period apart: around the
/// centre, or around the Moon or a planet when that is shorter (inside its
/// sphere of influence), as for the rocket.
double BodyPass::outputInterval(double tau, const State& y) const {
    const double fraction = m_opt->coastOutputFraction;
    const double r = norm(y.r);
    if (m_sun) {
        double interval = fraction * std::sqrt(r * r * r / kMuSun);
        if (m_opt->thirdBodyGravity) {
            for (const Planet planet : kAllPlanets) {
                const double d = norm(y.r - planetPosition(planet, epochAt(tau)));
                if (d > m_soi.at(static_cast<std::size_t>(planet))) continue;
                interval = std::min(interval, fraction * std::sqrt(d * d * d / planetMu(planet)));
            }
        }
        return std::clamp(interval, m_opt->minHeliocentricOutputInterval,
                          m_opt->maxHeliocentricOutputInterval);
    }
    double interval = fraction * std::sqrt(r * r * r / kMuEarth);
    if (m_opt->thirdBodyGravity) {
        const double d = norm(y.r - moonAt(tau));
        interval = std::min(interval, fraction * std::sqrt(d * d * d / kMuMoon));
    }
    return std::clamp(interval, m_opt->burnOutputInterval, m_opt->maxBodyOutputInterval);
}

/// Where in [τ0, τ1] (either order) the trigger's g changes sign, by
/// bisection on the dense output. Returns the first moment on the far side.
double BodyPass::findCrossing(const Trigger& trigger, double tau0, double tau1) const {
    const bool startPositive = g(trigger, tau0, m_stepper.interpolate(tau0)) >= 0.0;
    double near = tau0;
    double far = tau1;
    for (int i = 0; i < 80 && std::abs(far - near) > 1e-6; ++i) {
        const double mid = 0.5 * (near + far);
        if ((g(trigger, mid, m_stepper.interpolate(mid)) >= 0.0) == startPositive) {
            near = mid;
        } else {
            far = mid;
        }
    }
    return far;
}

void BodyPass::record(EventType type, std::string description) {
    const Sample sample{m_tau, m_y, false, 0};
    m_out.samples.push_back(sample);
    m_out.events.push_back({type, std::move(description), sample});
}

BodyPass::Output BodyPass::run(const State& start, double tau0, double tauEnd) {
    m_y = start;
    m_tau = tau0;
    m_tau0 = tau0;
    m_out.samples.push_back({tau0, m_y, false, 0});
    // Is a before b, in the direction of travel?
    const auto before = [&](double a, double b) { return m_direction * (a - b) < 0.0; };
    if (!before(tau0, tauEnd)) return std::move(m_out);

    const double maxStep = m_sun ? m_opt->maxHeliocentricStep : m_opt->maxCoastStep;
    double h = m_direction * m_opt->initialStep;
    double nextOutput = tau0 + m_direction * outputInterval(tau0, m_y);

    for (int stepCount = 0;; ++stepCount) {
        if (stepCount >= m_opt->maxSteps) {
            m_out.endReason = EndReason::MaxSteps;
            break;
        }
        if (!before(m_tau, tauEnd)) {
            m_out.endReason = EndReason::SpanEnd;
            break;
        }
        double hStep = m_direction * std::min(std::abs(h), maxStep);
        if (before(tauEnd, m_tau + hStep)) hStep = tauEnd - m_tau;

        const auto result = m_stepper.step(m_tau, m_y, hStep);
        h = result.nextStep;
        if (!result.accepted) continue;
        double tauStop = m_tau + hStep;

        // The earliest trigger (in the direction of travel) within the step.
        std::optional<Trigger> fired;
        for (const Trigger& trigger : m_triggers) {
            const double g0 = g(trigger, m_tau, m_y);
            const double g1 = g(trigger, m_tau + hStep, result.y);
            // Events are defined forwards in time; going backwards, the end
            // of the step is the earlier moment.
            const bool forwards = endsPass(trigger.kind) || m_direction > 0.0;
            const double gEarly = forwards ? g0 : g1;
            const double gLate = forwards ? g1 : g0;
            if (gEarly >= 0.0 || gLate < 0.0) continue;
            const double crossing = findCrossing(trigger, m_tau, m_tau + hStep);
            // A body placed at periapsis starts with r·v = 0, give or take
            // rounding, which differs between compilers. That periapsis is
            // recorded once, at the epoch (simulateNaturalBody), not here.
            const bool apsis = trigger.kind == Kind::Periapsis || trigger.kind == Kind::Apoapsis;
            if (apsis && std::abs(crossing - m_tau0) < 1e-3) continue;
            if (!fired || before(crossing, tauStop)) {
                tauStop = crossing;
                fired = trigger;
            }
        }
        const State yStop = fired ? m_stepper.interpolate(tauStop) : result.y;

        while (before(nextOutput, tauStop)) {
            const State y = m_stepper.interpolate(nextOutput);
            m_out.samples.push_back({nextOutput, y, false, 0});
            nextOutput += m_direction * outputInterval(nextOutput, y);
        }
        m_tau = tauStop;
        m_y = yStop;
        if (!fired) continue;

        const std::string planet(planetName(fired->planet));
        switch (fired->kind) {
        case Kind::CentreImpact:
            // Exactly on the surface (the interpolated point is within mm).
            m_y.r = centreRadius() * normalized(m_y.r);
            m_out.samples.push_back({m_tau, m_y, false, 0});
            m_out.endReason = EndReason::Impact;
            m_out.impacted = m_sun ? "the Sun" : "the Earth";
            return std::move(m_out);
        case Kind::PlanetImpact:
            m_out.samples.push_back({m_tau, m_y, false, 0});
            m_out.endReason = EndReason::Impact;
            m_out.impacted = planet;
            return std::move(m_out);
        case Kind::MoonImpact:
            m_out.samples.push_back({m_tau, m_y, false, 0});
            m_out.endReason = EndReason::MoonImpact;
            m_out.impacted = "the Moon";
            return std::move(m_out);
        case Kind::DistanceLimit:
            m_out.samples.push_back({m_tau, m_y, false, 0});
            m_out.endReason = EndReason::DistanceLimit;
            return std::move(m_out);
        case Kind::Periapsis:
            if (m_sun) {
                record(EventType::Perihelion, "Perihelion: closest to the Sun");
            } else {
                record(EventType::EarthClosestApproach, "Closest approach to the Earth");
            }
            break;
        case Kind::Apoapsis:
            record(EventType::Aphelion, "Aphelion: furthest from the Sun");
            break;
        case Kind::Capture:
            record(EventType::EarthCapture, "Captured: bound to the Earth");
            break;
        case Kind::Release:
            record(EventType::EarthRelease, "Released: no longer bound to the Earth");
            break;
        case Kind::EnterMoonSphere:
            record(EventType::EnterMoonSphere, "Entered the Moon's sphere of influence");
            break;
        case Kind::LeaveMoonSphere:
            record(EventType::LeaveMoonSphere, "Left the Moon's sphere of influence");
            break;
        case Kind::PlanetPeriapsis:
            record(EventType::PlanetClosestApproach,
                   std::string("Closest approach to ") +
                       (fired->planet == Planet::Earth ? "the Earth" : planet));
            break;
        }
    }
    if (m_out.samples.back().t < m_tau || m_out.samples.back().t > m_tau) {
        m_out.samples.push_back({m_tau, m_y, false, 0});
    }
    return std::move(m_out);
}

double bodyEnergy(const State& y) { return normSquared(y.v) / 2.0 - kMuEarth / norm(y.r); }

/// Speed left over far from the Earth, v∞ = √(2ε), for an unbound orbit.
double excessSpeed(const State& y) {
    const double energy = bodyEnergy(y);
    return energy > 0.0 ? std::sqrt(2.0 * energy) : 0.0;
}

/// Elements whose periapsis lies inside the centre (the Earth, or the Sun)
/// describe a body that hits it on its way in, before it gets there. Where
/// that happens: the moment τ (s from the epoch, negative) and the state,
/// from the two-body orbit (the conic equation r = p / (1 + e cos ν) solved
/// for r = R, on the way in, ν < 0). Nothing if the whole orbit is inside.
struct GroundCrossing {
    double tau;
    State state;
};
std::optional<GroundCrossing> incomingGroundCrossing(const NaturalBody& body, double radius) {
    const double q = body.periapsisDistance;
    const double e = body.eccentricity;
    const double mu = centreMu(body.centre);
    const double p = q * (1.0 + e);
    const double cosNu = e > 0.0 ? (p / radius - 1.0) / e : 2.0;
    if (cosNu < -1.0 || cosNu > 1.0) return std::nullopt; // never reaches the surface
    const double nu = -std::acos(cosNu);
    NaturalBody atSurface = body;
    const PositionVelocity j2000 =
        stateFromPeriapsis(q, e, body.inclination, body.raan, body.argumentOfPeriapsis, nu, mu);
    const double tau = timeSincePeriapsis(q, e, nu, mu);
    atSurface.epoch = body.epoch + tau;
    atSurface.given = BodyGiven::StateVector;
    atSurface.position = j2000.r;
    atSurface.velocity = j2000.v;
    const PositionVelocity state = bodyStateAtEpoch(atSurface);
    return GroundCrossing{tau, {state.r, state.v, 0.0}};
}

/// What a flight around the Earth came to (N1).
void summariseAroundEarth(SimResult& result, const SimOptions& options, double epochTime,
                          bool startedFromGround) {
    EncounterSummary encounter;
    encounter.epochTime = epochTime;
    encounter.closest = result.samples.front();
    double boundTime = 0.0;
    bool alwaysBound = true;
    for (std::size_t i = 0; i < result.samples.size(); ++i) {
        const Sample& s = result.samples[i];
        result.maxAltitude = std::max(result.maxAltitude, s.altitude());
        if (norm(s.state.r) < norm(encounter.closest.state.r)) encounter.closest = s;
        if (options.thirdBodyGravity) {
            const double moonAltitude =
                norm(s.state.r - moonPosition(result.startEpoch + s.t)) - kRadiusMoon;
            if (moonAltitude < result.lunar.closestApproachAltitude) {
                result.lunar.closestApproachAltitude = moonAltitude;
                result.lunar.closestApproachTime = s.t;
            }
        }
        alwaysBound = alwaysBound && bodyEnergy(s.state) < 0.0;
        // Time bound to the Earth: whole intervals between bound samples
        // (the capture and release events are samples too, so the edges are
        // exact).
        if (i > 0 && bodyEnergy(result.samples[i - 1].state) < 0.0 && bodyEnergy(s.state) < 0.0) {
            boundTime += s.t - result.samples[i - 1].t;
        }
    }
    for (const FlightEvent& e : result.events) {
        if (e.type == EventType::EnterMoonSphere) result.lunar.enteredSphereOfInfluence = true;
    }
    const State& first = result.samples.front().state;
    const State& last = result.samples.back().state;
    encounter.initialOrbit = elementsFromState(first.r, first.v);
    encounter.finalOrbit = elementsFromState(last.r, last.v);
    encounter.arrivalSpeed = excessSpeed(first);
    encounter.departureSpeed = excessSpeed(last);
    encounter.timeBound = boundTime;
    if (encounter.arrivalSpeed > 0.0) {
        encounter.impactParameter = norm(cross(first.r, first.v)) / encounter.arrivalSpeed;
    }
    result.orbit = encounter.finalOrbit;
    if (options.thirdBodyGravity) {
        const double epoch = result.startEpoch + result.duration;
        const Vec3 rMoon = last.r - moonPosition(epoch);
        if (norm(rMoon) < kMoonSphereOfInfluence) {
            result.lunar.finalOrbit =
                elementsFromState(rMoon, last.v - moonVelocity(epoch), kMuMoon, kRadiusMoon);
        }
    }

    // What became of it.
    if (result.endReason == EndReason::Impact) {
        result.outcome = Outcome::Impact;
    } else if (result.endReason == EndReason::MoonImpact) {
        result.outcome = Outcome::LunarImpact;
    } else if (alwaysBound && !startedFromGround && encounter.finalOrbit.periapsis > kRadiusEarth) {
        result.outcome = Outcome::Orbit;
    } else if (boundTime > 0.0) {
        result.outcome = Outcome::TemporaryCapture;
    } else {
        result.outcome = Outcome::Flyby;
    }
    result.encounter = encounter;
}

/// What a flight around the Sun came to.
void summariseAroundSun(SimResult& result, double epochTime, const std::string& impacted) {
    result.frame = ResultFrame::SunEclipticJ2000;
    SolarSummary solar;
    solar.epochTime = epochTime;
    solar.perihelion = result.samples.front();
    solar.impacted = impacted;
    for (const Sample& s : result.samples) {
        if (norm(s.state.r) < norm(solar.perihelion.state.r)) solar.perihelion = s;
        // How near each planet came (the closest-approach events are
        // samples too, so the minima are exact where they were found).
        for (const Planet planet : kAllPlanets) {
            const double d = norm(s.state.r - planetPosition(planet, result.startEpoch + s.t));
            SolarSummary::Approach& a = solar.approaches.at(static_cast<std::size_t>(planet));
            if (d < a.distance) a = {d, s.t};
        }
    }
    const State& first = result.samples.front().state;
    const State& last = result.samples.back().state;
    solar.initialOrbit = elementsFromState(first.r, first.v, kMuSun, kRadiusSun);
    solar.finalOrbit = elementsFromState(last.r, last.v, kMuSun, kRadiusSun);
    result.orbit = solar.finalOrbit;
    if (result.endReason == EndReason::Impact) {
        result.outcome = Outcome::Impact;
    } else {
        result.outcome = solar.finalOrbit.bound ? Outcome::Orbit : Outcome::Escape;
    }
    result.solar = std::move(solar);
}

} // namespace

SimResult simulateNaturalBody(const Mission& mission, const SimOptions& options) {
    const NaturalBody& body = mission.body;
    const bool sun = body.centre == BodyCentre::Sun;
    const double radius = sun ? kRadiusSun : kRadiusEarth;
    const PositionVelocity placed = bodyStateAtEpoch(body);
    State start{placed.r, placed.v, 0.0};
    double tauStart = 0.0;

    // Given inside the centre: start where it hits, and only look back.
    const bool underground = norm(start.r) < radius;
    if (underground) {
        const std::optional<GroundCrossing> crossing =
            body.given == BodyGiven::Elements ? incomingGroundCrossing(body, radius) : std::nullopt;
        if (crossing) {
            tauStart = crossing->tau;
            start = crossing->state;
        }
        start.r = radius * normalized(start.r); // exactly on the surface
    }

    BodyPass::Output back = BodyPass(mission, options, -1.0).run(start, tauStart, -body.timeBefore);
    BodyPass::Output ahead = BodyPass(mission, options, 1.0)
                                 .run(start, tauStart, underground ? tauStart : body.timeAfter);
    if (underground) {
        ahead.endReason = EndReason::Impact;
        ahead.impacted = sun ? "the Sun" : "the Earth";
    }

    // Put the two passes end to end, from the earliest moment, with t = 0
    // at the start: t = τ + (how far back the first pass got).
    const double offset = -back.samples.back().t;
    SimResult result;
    result.mission = mission;
    result.startEpoch = body.epoch - offset;
    std::ranges::reverse(back.samples);
    std::ranges::reverse(back.events);
    result.samples = std::move(back.samples);
    result.samples.pop_back(); // the epoch, which the second pass starts with
    result.samples.insert(result.samples.end(), ahead.samples.begin(), ahead.samples.end());
    for (Sample& s : result.samples) s.t += offset;

    // The epoch itself is an event: where the body was placed (unless it
    // hit the ground before getting there).
    const bool elements = body.given == BodyGiven::Elements;
    if (!underground) {
        back.events.push_back({EventType::Epoch,
                               elements ? "Epoch: the orbital elements hold here"
                                        : "Epoch: the position and velocity hold here",
                               {0.0, start, false, 0}});
        // Placed at its closest approach (always, for elements): that is an
        // event too. The passes skip it, as rounding can put it either side.
        const double radial = dot(start.r, start.v) / (norm(start.r) * norm(start.v));
        if (std::abs(radial) < 1e-9) {
            back.events.push_back(sun ? FlightEvent{EventType::Perihelion,
                                                    "Perihelion: closest to the Sun",
                                                    {0.0, start, false, 0}}
                                      : FlightEvent{EventType::EarthClosestApproach,
                                                    "Closest approach to the Earth",
                                                    {0.0, start, false, 0}});
        }
    }
    for (auto* events : {&back.events, &ahead.events}) {
        for (FlightEvent& e : *events) {
            e.sample.t += offset;
            result.events.push_back(std::move(e));
        }
    }
    // Events and samples at the same moment: keep them in time order.
    std::ranges::stable_sort(result.events, {}, [](const FlightEvent& e) { return e.sample.t; });
    std::ranges::stable_sort(result.samples, {}, &Sample::t);

    result.endReason = ahead.endReason;
    result.duration = result.samples.back().t;
    if (sun) {
        summariseAroundSun(result, offset, ahead.impacted);
    } else {
        summariseAroundEarth(result, options, offset, back.endReason == EndReason::Impact);
    }
    return result;
}

} // namespace orbit
