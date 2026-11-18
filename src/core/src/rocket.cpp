// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/rocket.hpp"

#include "orbit/core/constants.hpp"

#include <algorithm>
#include <cmath>

namespace orbit {

double exhaustVelocity(double isp) { return isp * kG0; }

double seaLevelIsp(const Stage& stage) { return stage.isp * (1.0 - stage.seaLevelLoss); }

double thrustAt(const Stage& stage, double pressure) {
    // The mass flow is set by the engine; the outside air only takes away
    // push, in proportion to its pressure.
    return std::max(0.0, stage.thrust * (1.0 - stage.seaLevelLoss * pressure / kSeaLevelPressure));
}

double seaLevelThrust(const Stage& stage) { return thrustAt(stage, kSeaLevelPressure); }

double massFlowRate(const Stage& stage) { return stage.thrust / exhaustVelocity(stage.isp); }

double burnTime(const Stage& stage) { return stage.propellantMass / massFlowRate(stage); }

double propellantForBurnTime(const Stage& stage, double seconds) {
    return seconds * massFlowRate(stage);
}

bool canFire(const Stage& stage) { return stage.propellantMass > 0.0 && stage.thrust > 0.0; }

double massAbove(const Rocket& rocket, int k) {
    double mass = rocket.payloadMass;
    for (int j = k + 1; j < rocket.stageCount; ++j) {
        const Stage& s = rocket.stages.at(static_cast<std::size_t>(j));
        mass += s.dryMass + s.propellantMass;
    }
    return mass;
}

double liftoffMass(const Rocket& rocket) { return massAbove(rocket, -1); }

double stageDeltaV(const Rocket& rocket, int k) {
    const Stage& s = rocket.stages.at(static_cast<std::size_t>(k));
    const double above = massAbove(rocket, k);
    // Tsiolkovsky, with the stages above and the payload as the load.
    return exhaustVelocity(s.isp) *
           std::log((s.dryMass + s.propellantMass + above) / (s.dryMass + above));
}

double idealDeltaV(const Rocket& rocket) {
    double total = 0.0;
    for (int k = 0; k < rocket.stageCount; ++k) total += stageDeltaV(rocket, k);
    return total;
}

double thrustToWeightAtIgnition(const Rocket& rocket, int k) {
    const Stage& s = rocket.stages.at(static_cast<std::size_t>(k));
    const double mass = s.dryMass + s.propellantMass + massAbove(rocket, k);
    const double surfaceGravity = kMuEarth / (kRadiusEarth * kRadiusEarth);
    // The first stage lights on the pad; the others high up, in near-vacuum.
    const double thrust = k == 0 ? seaLevelThrust(s) : s.thrust;
    return thrust / (mass * surfaceGravity);
}

double liftoffThrustToWeight(const Rocket& rocket) { return thrustToWeightAtIgnition(rocket, 0); }

} // namespace orbit
