// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/rocket.hpp"

#include "orbit/core/constants.hpp"

#include <cmath>

namespace orbit {

double exhaustVelocity(double isp) { return isp * kG0; }

double massFlowRate(const Rocket& rocket) { return rocket.thrust / exhaustVelocity(rocket.isp); }

double burnTime(const Rocket& rocket) { return rocket.propellantMass / massFlowRate(rocket); }

double propellantForBurnTime(const Rocket& rocket, double seconds) {
    return seconds * massFlowRate(rocket);
}

double idealDeltaV(const Rocket& rocket) {
    const double m0 = rocket.dryMass + rocket.propellantMass;
    return exhaustVelocity(rocket.isp) * std::log(m0 / rocket.dryMass);
}

double liftoffThrustToWeight(const Rocket& rocket) {
    const double m0 = rocket.dryMass + rocket.propellantMass;
    const double surfaceGravity = kMuEarth / (kRadiusEarth * kRadiusEarth);
    return rocket.thrust / (m0 * surfaceGravity);
}

} // namespace orbit
