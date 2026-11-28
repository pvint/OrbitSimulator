// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/natural_body.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/solar_system.hpp"

#include <algorithm>
#include <cmath>

namespace orbit {

std::string_view vehicleKey(Vehicle vehicle) {
    switch (vehicle) {
    case Vehicle::Rocket:
        return "rocket";
    case Vehicle::NaturalBody:
        return "natural-body";
    }
    return {};
}

std::string_view vehicleLabel(Vehicle vehicle) {
    switch (vehicle) {
    case Vehicle::Rocket:
        return "Rocket";
    case Vehicle::NaturalBody:
        return "Natural body";
    }
    return {};
}

bool vehicleFromKey(std::string_view key, Vehicle& vehicle) {
    for (const Vehicle v : kAllVehicles) {
        if (vehicleKey(v) == key) {
            vehicle = v;
            return true;
        }
    }
    return false;
}

std::string_view bodyGivenKey(BodyGiven given) {
    switch (given) {
    case BodyGiven::Elements:
        return "elements";
    case BodyGiven::StateVector:
        return "state-vector";
    }
    return {};
}

std::string_view bodyGivenLabel(BodyGiven given) {
    switch (given) {
    case BodyGiven::Elements:
        return "Orbital elements";
    case BodyGiven::StateVector:
        return "Position and velocity";
    }
    return {};
}

bool bodyGivenFromKey(std::string_view key, BodyGiven& given) {
    for (const BodyGiven g : kAllBodyGivens) {
        if (bodyGivenKey(g) == key) {
            given = g;
            return true;
        }
    }
    return false;
}

std::string_view bodyCentreKey(BodyCentre centre) {
    switch (centre) {
    case BodyCentre::Earth:
        return "earth";
    case BodyCentre::Sun:
        return "sun";
    }
    return {};
}

std::string_view bodyCentreLabel(BodyCentre centre) {
    switch (centre) {
    case BodyCentre::Earth:
        return "The Earth";
    case BodyCentre::Sun:
        return "The Sun";
    }
    return {};
}

bool bodyCentreFromKey(std::string_view key, BodyCentre& centre) {
    for (const BodyCentre c : kAllBodyCentres) {
        if (bodyCentreKey(c) == key) {
            centre = c;
            return true;
        }
    }
    return false;
}

double centreMu(BodyCentre centre) { return centre == BodyCentre::Sun ? kMuSun : kMuEarth; }

NaturalBody apophisFlyby() {
    // Osculating elements around the Earth at closest approach, worked out
    // from JPL Horizons' geocentric state vector (ICRF) at 21:45 UTC.
    NaturalBody body;
    body.given = BodyGiven::Elements;
    body.periapsisDistance = 38011.3e3;
    body.eccentricity = 4.2539;
    body.inclination = degToRad(140.641);
    body.raan = degToRad(167.397);
    body.argumentOfPeriapsis = degToRad(49.692);
    // The periapsis is passed 4 s after Horizons' 21:45:00 sample.
    body.epoch = epochFromCalendar({2029, 4, 13, 21, 45, 4.0});
    const PositionVelocity state = bodyStateJ2000(body);
    body.position = state.r;
    body.velocity = state.v;
    return body;
}

PositionVelocity bodyStateJ2000(const NaturalBody& body) {
    if (body.given == BodyGiven::StateVector) return {body.position, body.velocity};
    return stateFromPeriapsis(body.periapsisDistance, body.eccentricity, body.inclination,
                              body.raan, body.argumentOfPeriapsis, 0.0, centreMu(body.centre));
}

PositionVelocity bodyStateAtEpoch(const NaturalBody& body) {
    const PositionVelocity j2000 = bodyStateJ2000(body);
    if (body.centre == BodyCentre::Sun) return j2000; // the simulation's frame already
    // The frame turns by a few arcseconds a year: far too slowly for its
    // turning to change the velocity measurably, so both are simply rotated.
    return {precessionFromJ2000(j2000.r, body.epoch), precessionFromJ2000(j2000.v, body.epoch)};
}

void setBodyGiven(NaturalBody& body, BodyGiven given) {
    if (given == body.given) return;
    const double mu = centreMu(body.centre);
    const PositionVelocity state = bodyStateJ2000(body);
    if (given == BodyGiven::StateVector) {
        body.position = state.r;
        body.velocity = state.v;
        body.given = given;
        return;
    }

    // To elements: the orbit through the state, and when it passes periapsis.
    const OrbitalElements el = elementsFromState(state.r, state.v, mu);
    const double sincePeriapsis =
        timeSincePeriapsis(el.periapsis, el.eccentricity, el.trueAnomaly, mu);
    const double spanStart = body.epoch - body.timeBefore;
    const double spanEnd = body.epoch + body.timeAfter;
    body.epoch -= sincePeriapsis;
    body.timeBefore = std::max(0.0, body.epoch - spanStart);
    body.timeAfter = std::max(0.0, spanEnd - body.epoch);

    body.periapsisDistance = el.periapsis;
    body.eccentricity = el.eccentricity;
    body.inclination = el.inclination;
    body.raan = el.raan;
    body.argumentOfPeriapsis = el.argumentOfPeriapsis;
    body.given = given;
}

void setBodyCentre(NaturalBody& body, BodyCentre centre) {
    if (centre == body.centre) return;
    setBodyGiven(body, BodyGiven::StateVector);

    // r_sun = r_earth + R_earth(t), with the geocentric part turned from the
    // equator to the ecliptic; the same for the velocity.
    const Vec3 earthR = bodyPosition(Body::Earth, body.epoch);
    const Vec3 earthV = bodyVelocity(Body::Earth, body.epoch);
    if (centre == BodyCentre::Sun) {
        body.position = earthR + equatorialToEcliptic(body.position);
        body.velocity = earthV + equatorialToEcliptic(body.velocity);
        body.timeBefore = std::max(body.timeBefore, 365.25 * 86400.0);
        body.timeAfter = std::max(body.timeAfter, 365.25 * 86400.0);
    } else {
        body.position = eclipticToEquatorial(body.position - earthR);
        body.velocity = eclipticToEquatorial(body.velocity - earthV);
        body.timeBefore = std::min(body.timeBefore, 30.0 * 86400.0);
        body.timeAfter = std::min(body.timeAfter, 30.0 * 86400.0);
    }
    body.centre = centre;
}

namespace {

/// Rotation of a vector about the y axis by an angle (counterclockwise
/// looking down the y axis towards the origin).
Vec3 rotateY(const Vec3& a, double angle) {
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return {c * a.x + s * a.z, a.y, -s * a.x + c * a.z};
}

struct PrecessionAngles {
    double zeta;
    double z;
    double theta;
};

/// The IAU 1976 precession angles from J2000 to the epoch (radians); T in
/// Julian centuries. (Meeus, eq. 21.3, with the starting epoch J2000.)
PrecessionAngles precessionAngles(double epoch) {
    const double t = julianCenturies(epoch);
    const auto arcseconds = [](double value) { return degToRad(value / 3600.0); };
    return {arcseconds(((0.017998 * t + 0.30188) * t + 2306.2181) * t),
            arcseconds(((0.018203 * t + 1.09468) * t + 2306.2181) * t),
            arcseconds(((-0.041833 * t - 0.42665) * t + 2004.3109) * t)};
}

} // namespace

Vec3 precessionFromJ2000(const Vec3& v, double epoch) {
    const PrecessionAngles a = precessionAngles(epoch);
    return rotateZ(rotateY(rotateZ(v, a.zeta), -a.theta), a.z);
}

Vec3 precessionToJ2000(const Vec3& v, double epoch) {
    const PrecessionAngles a = precessionAngles(epoch);
    return rotateZ(rotateY(rotateZ(v, -a.z), a.theta), -a.zeta);
}

} // namespace orbit
