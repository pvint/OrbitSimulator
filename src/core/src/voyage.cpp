// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/voyage.hpp"

#include "orbit/core/constants.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/gravity.hpp"
#include "orbit/core/solar_system.hpp"
#include "orbit/core/state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace orbit {

namespace {

constexpr double kDay = 86400.0;
/// The longest integration step: the step size adapts anyway, but the cap
/// makes sure a step never jumps clean over a close pass by the Earth.
constexpr double kMaxStep = kDay;
/// Output samples are spaced by this fraction of √(r³/μ) (a circular period
/// over 2π), around the Sun, or around the Earth when that is shorter.
constexpr double kOutputFraction = 0.005;
constexpr double kMinOutputInterval = 60.0;
constexpr double kMaxOutputInterval = 5.0 * kDay;
constexpr int kMaxSteps = 1'000'000;

struct Relative {
    Vec3 r;
    Vec3 v;
};

/// Position and velocity relative to the Earth.
Relative relativeToEarth(double epoch, const Vec3& r, const Vec3& v) {
    return {r - planetPosition(Planet::Earth, epoch), v - planetVelocity(Planet::Earth, epoch)};
}

/// Moments the flight watches for. Each has a function g of the state that
/// falls through zero at that moment.
enum class Event {
    SunImpact,      ///< g = r − R_sun
    EntryInterface, ///< g = d − (R_earth + 120 km), d = distance from the Earth
    EarthArrival,   ///< g = d − (sphere of influence)
    EarthClosest,   ///< g = −(r_rel · v_rel): the distance stops shrinking
};
constexpr std::array kEvents{Event::SunImpact, Event::EntryInterface, Event::EarthArrival,
                             Event::EarthClosest};

class VoyageFlight {
public:
    VoyageFlight(const Voyage& voyage, const VoyageOptions& options)
        : m_voyage(&voyage), m_opt(&options),
          m_stepper(
              [](double t, const State& y) {
                  return State{y.v, heliocentricGravity(y.r, t, true), 0.0};
              },
              options.tolerances),
          m_soi(sphereOfInfluence(Planet::Earth, voyage.startEpoch)) {}

    VoyageResult run();

private:
    const Voyage* m_voyage;
    const VoyageOptions* m_opt;
    DormandPrince45 m_stepper;
    double m_soi;
    double m_t = 0.0;
    State m_y;
    double m_nextOutput = 0.0;
    VoyageResult m_out;

    double g(Event event, double t, const State& y) const;
    double findCrossing(Event event, double t0, double t1) const;
    double outputInterval(double t, const State& y) const;
    void addSample() { m_out.samples.push_back({m_t, m_y.r, m_y.v, m_y.m}); }
    void noteApproach();
    bool coastTo(double tEnd);
    bool handle(Event event);
    void burn(const Burn& planned);
    bool capturedAndDone() const;
    void finish(VoyageEnd end);
};

double VoyageFlight::g(Event event, double t, const State& y) const {
    switch (event) {
    case Event::SunImpact:
        return norm(y.r) - kRadiusSun;
    case Event::EntryInterface:
        return norm(y.r - planetPosition(Planet::Earth, t)) -
               (kRadiusEarth + m_opt->entryInterfaceAltitude);
    case Event::EarthArrival:
        return norm(y.r - planetPosition(Planet::Earth, t)) - m_soi;
    case Event::EarthClosest: {
        const Relative rel = relativeToEarth(t, y.r, y.v);
        return -dot(rel.r, rel.v);
    }
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

double VoyageFlight::outputInterval(double t, const State& y) const {
    const double r = norm(y.r);
    double interval = kOutputFraction * std::sqrt(r * r * r / kMuSun);
    const double d = norm(y.r - planetPosition(Planet::Earth, t));
    if (d < m_soi) {
        interval =
            std::min(interval, kOutputFraction * std::sqrt(d * d * d / planetMu(Planet::Earth)));
    }
    return std::clamp(interval, kMinOutputInterval, kMaxOutputInterval);
}

/// Is this the closest to the Earth so far?
void VoyageFlight::noteApproach() {
    const Relative rel = relativeToEarth(m_t, m_y.r, m_y.v);
    const double d = norm(rel.r);
    EarthApproach& c = m_out.closest;
    if (c.valid && d >= c.distance) return;
    c.valid = true;
    c.epoch = m_t;
    c.distance = d;
    c.speed = norm(rel.v);
    c.relativePosition = rel.r;
    c.relativeVelocity = rel.v;
    c.orbit = elementsFromState(rel.r, rel.v, planetMu(Planet::Earth), kRadiusEarth);
}

/// Coast to tEnd. True if the flight ended on the way.
bool VoyageFlight::coastTo(double tEnd) {
    double h = 1.0;
    for (int stepCount = 0; m_t < tEnd; ++stepCount) {
        if (stepCount >= kMaxSteps) {
            finish(VoyageEnd::Horizon);
            return true;
        }
        double hStep = std::min(h, kMaxStep);
        if (m_t + hStep > tEnd) hStep = tEnd - m_t;
        const auto result = m_stepper.step(m_t, m_y, hStep);
        h = result.nextStep;
        if (!result.accepted) continue;

        // The earliest event within the step, if any.
        double tStop = m_t + hStep;
        std::optional<Event> fired;
        for (const Event event : kEvents) {
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
            m_out.samples.push_back({m_nextOutput, y.r, y.v, y.m});
            m_nextOutput += outputInterval(m_nextOutput, y);
        }
        m_t = tStop;
        m_y = yStop;
        if (fired && handle(*fired)) return true;
    }
    return false;
}

/// React to an event. True if it ends the flight.
bool VoyageFlight::handle(Event event) {
    addSample();
    switch (event) {
    case Event::SunImpact:
        finish(VoyageEnd::HitSun);
        return true;
    case Event::EarthClosest:
        noteApproach();
        return false;
    case Event::EarthArrival:
        if (!m_out.earthArrival) m_out.earthArrival = m_t;
        if (m_voyage->goal != Goal::ReachEarth) return false;
        finish(VoyageEnd::ReachedEarth);
        return true;
    case Event::EntryInterface: {
        // The flight-path angle γ is the angle between the velocity and the
        // local horizontal: sin γ = (r · v) / (|r| |v|), negative coming down.
        noteApproach();
        const Relative rel = relativeToEarth(m_t, m_y.r, m_y.v);
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
    const double ve = exhaustVelocity(m_voyage->ship.isp);
    const double available = ve * std::log(m_y.m / m_voyage->ship.dryMass);
    BurnReport report{planned, frame.body, size, 0.0, false};
    if (size > available) {
        deltaV = size > 0.0 ? deltaV * (available / size) : deltaV;
        size = available;
        report.deltaV = available;
        report.shortOfFuel = true;
    }
    const double massAfter = m_y.m * std::exp(-size / ve);
    report.propellantUsed = m_y.m - massAfter;
    m_y.v += deltaV;
    m_y.m = massAfter;
    m_out.burns.push_back(report);
    addSample();
}

/// Left on a closed orbit around the Earth that clears the air, with no
/// burns to come: nothing more will happen.
bool VoyageFlight::capturedAndDone() const {
    const Relative rel = relativeToEarth(m_t, m_y.r, m_y.v);
    if (norm(rel.r) > m_soi) return false;
    const OrbitalElements el =
        elementsFromState(rel.r, rel.v, planetMu(Planet::Earth), kRadiusEarth);
    return el.bound && el.periapsisAltitude >= m_opt->captureMinPerigeeAltitude &&
           el.apoapsis <= m_opt->captureMaxApogee;
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
    const double end = m_opt->stopAt ? *m_opt->stopAt : lastEvent + m_opt->horizon;

    const Ship& ship = m_voyage->ship;
    m_t = m_voyage->startEpoch;
    m_y = {m_voyage->position, m_voyage->velocity, ship.dryMass + ship.propellant};
    m_nextOutput = m_t + outputInterval(m_t, m_y);
    addSample();
    noteApproach();

    bool ended = false;
    for (std::size_t i = 0; i < burns.size() && !ended; ++i) {
        if (burns[i].epoch > end) break;
        ended = coastTo(burns[i].epoch);
        if (ended) break;
        burn(burns[i]);
        noteApproach();
        if (i + 1 == burns.size() && capturedAndDone()) {
            finish(VoyageEnd::Captured);
            ended = true;
        }
    }
    if (!ended && !coastTo(end)) {
        addSample();
        noteApproach();
        finish(m_opt->stopAt ? VoyageEnd::StopTime : VoyageEnd::Horizon);
    }

    m_out.goalMet = goalMet(m_voyage->goal, m_out.end);
    m_out.propellantLeft = m_y.m - ship.dryMass;
    m_out.deltaVLeft = exhaustVelocity(ship.isp) * std::log(m_y.m / ship.dryMass);
    return std::move(m_out);
}

} // namespace

double deltaVCapacity(const Ship& ship) {
    return exhaustVelocity(ship.isp) * std::log((ship.dryMass + ship.propellant) / ship.dryMass);
}

double propellantFor(double deltaV, double mass, double isp) {
    return mass * (1.0 - std::exp(-deltaV / exhaustVelocity(isp)));
}

double burnSize(const Burn& burn) {
    return std::sqrt(burn.prograde * burn.prograde + burn.radial * burn.radial +
                     burn.normal * burn.normal);
}

BurnFrame burnFrameAt(double epoch, const Vec3& r, const Vec3& v) {
    BurnFrame frame;
    Relative rel{r, v};
    const Relative earth = relativeToEarth(epoch, r, v);
    if (norm(earth.r) < sphereOfInfluence(Planet::Earth, epoch)) {
        frame.body = ReferenceBody::Earth;
        rel = earth;
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
    }
    return false;
}

VoyageResult flyVoyage(const Voyage& voyage, const VoyageOptions& options) {
    return VoyageFlight(voyage, options).run();
}

VoyageSample voyageStateAt(const Voyage& voyage, double epoch, const VoyageOptions& options) {
    VoyageOptions stop = options;
    stop.stopAt = epoch;
    VoyageResult result = flyVoyage(voyage, stop);
    return result.samples.back();
}

} // namespace orbit
