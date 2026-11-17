// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/lunar_targeting.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/earth.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/mission_parameters.hpp"

#include <cmath>
#include <limits>

namespace orbit {

namespace {

/// From liftoff to the start of the transfer: ascent, then on average about
/// half a parking orbit of waiting.
constexpr double kTimeBeforeInjection = 1.2 * 3600.0;

/// Where the Moon will be when the rocket arrives (unit vector).
Vec3 moonDirectionAtArrival(const Mission& mission) {
    return normalized(moonPosition(mission.launchEpoch + estimatedArrivalTime(mission)));
}

/// Purely geometric azimuth: the great circle from the launch site through
/// the Moon's arrival direction, flown in its eastward sense.
double geometricAzimuth(const Mission& mission) {
    const Vec3 pad =
        rotateZ(positionFromLatLon(mission.site.latitude, mission.site.longitude, kRadiusEarth),
                earthRotationAngleAt(mission.launchEpoch));
    const LocalFrame frame = localFrame(pad);
    // Horizontal direction at the pad pointing along the great circle to the
    // Moon: the Moon's direction with its "up" part removed.
    const Vec3 moon = moonDirectionAtArrival(mission);
    Vec3 heading = normalized(moon - dot(moon, frame.up) * frame.up);
    if (dot(heading, frame.east) < 0.0) heading = -heading; // prograde: east-going
    return wrapTwoPi(std::atan2(dot(heading, frame.east), dot(heading, frame.north)));
}

/// How far the Moon (at arrival) lies out of the parking orbit's plane, as
/// the sine of the angle, after flying the ascent with this azimuth.
std::optional<double> outOfPlane(Mission mission, double azimuth) {
    mission.guidance.azimuth = azimuth;
    mission.plan = FlightPlan::Orbit;
    FlightProgram ascent = flightProgramFor(mission);
    ascent.resize(3); // ascent, coast to apoapsis, circularization
    const SimResult r = simulateProgram(mission, ascent);
    if (r.endReason == EndReason::Impact || !r.cutoff) return std::nullopt;
    const Vec3 h = normalized(cross(r.cutoff->state.r, r.cutoff->state.v));
    return dot(h, moonDirectionAtArrival(mission));
}

} // namespace

double estimatedArrivalTime(const Mission& mission) {
    return kTimeBeforeInjection + lunarTransferTime(mission);
}

std::optional<double> azimuthToMoon(const Mission& mission) {
    // Secant method on "Moon out of plane" as a function of azimuth.
    double a0 = geometricAzimuth(mission);
    double a1 = a0 + degToRad(0.5);
    std::optional<double> e0 = outOfPlane(mission, a0);
    std::optional<double> e1 = outOfPlane(mission, a1);
    if (!e0 || !e1) return std::nullopt;
    for (int i = 0; i < 8 && std::abs(*e1) > 1e-5 && *e1 != *e0; ++i) {
        const double a2 = a1 - *e1 * (a1 - a0) / (*e1 - *e0);
        a0 = a1;
        e0 = e1;
        a1 = a2;
        e1 = outOfPlane(mission, a1);
        if (!e1) return std::nullopt;
    }
    return wrapTwoPi(a1);
}

double bestLaunchHourForMoon(const Mission& mission) {
    // Scan the day in 5-minute steps for the azimuth nearest due east.
    Mission m = mission;
    double bestHour = launchHourOfDay(mission);
    double bestScore = std::numeric_limits<double>::infinity();
    for (int minute = 0; minute < 24 * 60; minute += 5) {
        const double hour = minute / 60.0;
        setLaunchHourOfDay(m, hour);
        const double offEast = std::abs(wrapTwoPi(geometricAzimuth(m) - kPi / 2.0 + kPi) - kPi);
        if (offEast < bestScore) {
            bestScore = offEast;
            bestHour = hour;
        }
    }
    return bestHour;
}

} // namespace orbit
