// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/asteroid_belt.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/orbit.hpp"
#include "orbit/core/solar_system.hpp"

#include <array>
#include <cmath>
#include <random>

namespace orbit {

namespace {

/// Each gap: Jupiter's period over the asteroid's (k in T = T_J / k), and
/// its half-width (au), wider for the stronger resonances.
struct Resonance {
    double k;
    double halfWidth;
};
constexpr std::array kResonances{Resonance{3.0, 0.035}, Resonance{2.5, 0.025},
                                 Resonance{7.0 / 3.0, 0.012}, Resonance{2.0, 0.04}};

/// Uniform in [0, 1): from the engine's raw bits, the same on every
/// platform (std::uniform_real_distribution isn't).
double unit(std::mt19937_64& engine) { return static_cast<double>(engine() >> 11U) * 0x1.0p-53; }

/// A standard normal number (Box–Muller).
double normal(std::mt19937_64& engine) {
    const double u = std::max(unit(engine), 1e-300);
    const double v = unit(engine);
    return std::sqrt(-2.0 * std::log(u)) * std::cos(kTwoPi * v);
}

} // namespace

std::vector<double> kirkwoodGaps() {
    const double aJupiter = planetOrbit(Planet::Jupiter, 0.0).semiMajorAxis;
    std::vector<double> gaps;
    for (const Resonance& r : kResonances) gaps.push_back(aJupiter * std::pow(r.k, -2.0 / 3.0));
    return gaps;
}

std::vector<Asteroid> makeAsteroidBelt(std::size_t count, std::uint64_t seed) {
    std::mt19937_64 engine(seed);
    const std::vector<double> gaps = kirkwoodGaps();
    std::vector<Asteroid> belt;
    belt.reserve(count);
    while (belt.size() < count) {
        // Most between 2.2 and 3.2 au: a broad hump, none in the gaps.
        const double a = 2.65 + 0.33 * normal(engine);
        if (a < 2.1 || a > 3.3) continue;
        bool inGap = false;
        for (std::size_t g = 0; g < gaps.size(); ++g) {
            if (std::abs(a * kAstronomicalUnit - gaps[g]) <
                kResonances.at(g).halfWidth * kAstronomicalUnit) {
                inGap = true;
            }
        }
        if (inGap) continue;
        Asteroid asteroid;
        asteroid.semiMajorAxis = a * kAstronomicalUnit;
        asteroid.eccentricity = std::min(0.3, std::abs(0.1 * normal(engine)));
        asteroid.inclination = degToRad(std::min(30.0, std::abs(7.0 * normal(engine))));
        asteroid.raan = kTwoPi * unit(engine);
        asteroid.argumentOfPeriapsis = kTwoPi * unit(engine);
        asteroid.meanAnomaly = kTwoPi * unit(engine);
        belt.push_back(asteroid);
    }
    return belt;
}

Vec3 asteroidPosition(const Asteroid& asteroid, double epoch) {
    const double a = asteroid.semiMajorAxis;
    const double e = asteroid.eccentricity;
    const double n = std::sqrt(kMuSun / (a * a * a));
    const double meanAnomaly = std::fmod(asteroid.meanAnomaly + n * epoch, kTwoPi);
    const double bigE = eccentricAnomaly(meanAnomaly, e);
    // From the eccentric anomaly to the true one: tan(ν/2) = √((1+e)/(1−e)) tan(E/2).
    const double nu = 2.0 * std::atan2(std::sqrt(1.0 + e) * std::sin(bigE / 2.0),
                                       std::sqrt(1.0 - e) * std::cos(bigE / 2.0));
    return stateFromElements(a, e, asteroid.inclination, asteroid.raan,
                             asteroid.argumentOfPeriapsis, nu, kMuSun)
        .r;
}

} // namespace orbit
