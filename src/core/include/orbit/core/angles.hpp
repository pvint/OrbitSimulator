// SPDX-License-Identifier: GPL-3.0-or-later
// Angle helpers. Inside the core, angles are always radians. Degrees appear
// only at the edges (UI, CLI, defaults written for humans).
#pragma once

#include <cmath>
#include <numbers>

namespace orbit {

inline constexpr double kPi = std::numbers::pi;
inline constexpr double kTwoPi = 2.0 * std::numbers::pi;

constexpr double degToRad(double degrees) { return degrees * (kPi / 180.0); }
constexpr double radToDeg(double radians) { return radians * (180.0 / kPi); }

/// Wraps an angle into [0, 2π).
inline double wrapTwoPi(double angle) {
    const double wrapped = std::fmod(angle, kTwoPi);
    return wrapped < 0.0 ? wrapped + kTwoPi : wrapped;
}

} // namespace orbit
