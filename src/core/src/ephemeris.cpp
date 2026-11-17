// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/ephemeris.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/constants.hpp"

#include <cmath>

namespace orbit {

namespace {

double sinDeg(double degrees) { return std::sin(degToRad(degrees)); }
double cosDeg(double degrees) { return std::cos(degToRad(degrees)); }

/// Obliquity of the ecliptic: the tilt of Earth's axis against the plane of
/// its orbit, 23.44° and very slowly decreasing.
double obliquityDeg(double T) { return 23.439291 - 0.0130042 * T; }

/// Ecliptic coordinates (longitude, latitude, distance) → equatorial vector.
Vec3 eclipticToEquatorial(double lonDeg, double latDeg, double distance, double T) {
    const double eps = obliquityDeg(T);
    const double cb = cosDeg(latDeg);
    return distance * Vec3{cb * cosDeg(lonDeg),
                           cosDeg(eps) * cb * sinDeg(lonDeg) - sinDeg(eps) * sinDeg(latDeg),
                           sinDeg(eps) * cb * sinDeg(lonDeg) + cosDeg(eps) * sinDeg(latDeg)};
}

struct Ecliptic {
    double longitudeDeg;
    double latitudeDeg;
    double distance; ///< m
};

Ecliptic sunEcliptic(double T) {
    const double meanLongitude = 280.460 + 36000.771 * T;
    const double meanAnomaly = 357.5291092 + 35999.05034 * T;
    // The equation of centre: the elliptical orbit's correction to uniform motion.
    const double longitude =
        meanLongitude + 1.914666471 * sinDeg(meanAnomaly) + 0.019994643 * sinDeg(2.0 * meanAnomaly);
    const double distanceAu =
        1.000140612 - 0.016708617 * cosDeg(meanAnomaly) - 0.000139589 * cosDeg(2.0 * meanAnomaly);
    return {longitude, 0.0, distanceAu * kAstronomicalUnit};
}

Ecliptic moonEcliptic(double T) {
    // Mean longitude plus the largest periodic terms: the elliptical orbit,
    // the evection and variation (caused by the Sun), and the annual equation.
    const double longitude =
        218.32 + 481267.8813 * T + 6.29 * sinDeg(134.9 + 477198.85 * T) -
        1.27 * sinDeg(259.2 - 413335.38 * T) + 0.66 * sinDeg(235.7 + 890534.23 * T) +
        0.21 * sinDeg(269.9 + 954397.70 * T) - 0.19 * sinDeg(357.5 + 35999.05 * T) -
        0.11 * sinDeg(186.6 + 966404.05 * T);
    // The Moon's orbit is tilted about 5.1° to the ecliptic.
    const double latitude =
        5.13 * sinDeg(93.3 + 483202.03 * T) + 0.28 * sinDeg(228.2 + 960400.87 * T) -
        0.28 * sinDeg(318.3 + 6003.18 * T) - 0.17 * sinDeg(217.6 - 407332.20 * T);
    // Horizontal parallax: the angle Earth's radius subtends seen from the
    // Moon, which gives the distance.
    const double parallax =
        0.9508 + 0.0518 * cosDeg(134.9 + 477198.85 * T) + 0.0095 * cosDeg(259.2 - 413335.38 * T) +
        0.0078 * cosDeg(235.7 + 890534.23 * T) + 0.0028 * cosDeg(269.9 + 954397.70 * T);
    // The series uses the equatorial radius 6378.137 km for the parallax.
    return {longitude, latitude, 6.378137e6 / sinDeg(parallax)};
}

} // namespace

Vec3 sunPosition(double epoch) {
    const double T = julianCenturies(epoch);
    const Ecliptic e = sunEcliptic(T);
    return eclipticToEquatorial(e.longitudeDeg, e.latitudeDeg, e.distance, T);
}

Vec3 moonPosition(double epoch) {
    const double T = julianCenturies(epoch);
    const Ecliptic e = moonEcliptic(T);
    return eclipticToEquatorial(e.longitudeDeg, e.latitudeDeg, e.distance, T);
}

Vec3 moonVelocity(double epoch) {
    constexpr double h = 60.0; // s
    return (moonPosition(epoch + h) - moonPosition(epoch - h)) / (2.0 * h);
}

double moonElongation(double epoch) {
    const double T = julianCenturies(epoch);
    return wrapTwoPi(degToRad(moonEcliptic(T).longitudeDeg - sunEcliptic(T).longitudeDeg));
}

} // namespace orbit
