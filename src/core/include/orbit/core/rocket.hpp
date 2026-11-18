// SPDX-License-Identifier: GPL-3.0-or-later
// Rocket engine and mass relationships.
//
// A rocket is a stack of up to three stages with a payload on top. Each
// stage has its own tanks and engine, which burns at full, constant thrust.
// The bottom stage fires first; when its propellant is gone it is dropped
// (stage separation) and the one above it ignites at once. The payload is
// what's left at the very end.
//
// WHY STAGES?
// The rocket equation, Δv = v_e · ln(m0 / m_final), punishes carrying dead
// weight: once a stage's tanks are empty they are just mass that the engine
// still has to accelerate. Dropping them lets the stages above start from a
// much lighter rocket. Each stage contributes its own Δv,
//     Δv_k = v_e,k · ln((m_k + m_above) / (m_dry,k + m_above))
// where m_above is everything stacked on top of stage k (the stages above
// and the payload), and the total is their sum. With the same propellant and
// structure, two stages can give far more Δv than one.
//
// The rocket's shape, for drag, is boiled down to two numbers for the whole
// stack: a drag coefficient and a diameter (aerodynamics.hpp).
#pragma once

#include <array>

namespace orbit {

/// The most stages a rocket can have.
inline constexpr int kMaxStages = 3;

struct Stage {
    double dryMass = 0.0;        ///< kg: tanks, structure and engine, without propellant
    double propellantMass = 0.0; ///< kg when full
    double thrust = 0.0;         ///< N
    double isp = 0.0;            ///< s, specific impulse (engine efficiency)
};

struct Rocket {
    /// Bottom (first to fire) first. Only the first `stageCount` are fitted;
    /// the others keep their settings, so adding a stage back restores them.
    std::array<Stage, kMaxStages> stages{};
    int stageCount = 1;
    double payloadMass = 0.0; ///< kg, carried on top of the last stage
    /// Drag coefficient Cd below the speed of sound; it rises near Mach 1
    /// (aerodynamics.hpp). 0 means no drag at all, as in a vacuum.
    double dragCoefficient = 0.0;
    double diameter = 0.0; ///< m, sets the frontal area that meets the air
};

/// Exhaust velocity v_e = Isp · g0 (m/s).
double exhaustVelocity(double isp);

/// Propellant mass flow ṁ = F / v_e (kg/s). More thrust, or a less efficient
/// engine (lower Isp), burns propellant faster.
double massFlowRate(const Stage& stage);

/// How long the stage's engine can run at full thrust (s).
double burnTime(const Stage& stage);

/// Propellant needed for a given burn time (s). Used when sweeping burn time.
double propellantForBurnTime(const Stage& stage, double seconds);

/// Can this stage fire at all? (It has propellant and an engine.)
bool canFire(const Stage& stage);

/// Everything stacked on top of stage k: the fitted stages above it, full,
/// and the payload (kg). For the last stage, just the payload.
double massAbove(const Rocket& rocket, int k);

/// The whole rocket on the pad (kg).
double liftoffMass(const Rocket& rocket);

/// Ideal Δv of stage k, from the rocket equation with everything above it
/// as its load (m/s).
double stageDeltaV(const Rocket& rocket, int k);

/// Ideal Δv of the whole rocket: the sum over its stages (m/s). "Ideal"
/// means without gravity or air: in flight, some of it is spent holding the
/// rocket up against gravity and pushing through the air instead.
double idealDeltaV(const Rocket& rocket);

/// Thrust-to-weight ratio of stage k when it ignites, measured against the
/// Earth's surface gravity. For the first stage this is the liftoff
/// value, which must be above 1 to leave the pad. Upper stages fire when the
/// rocket is already moving fast, sideways, so below 1 is common.
double thrustToWeightAtIgnition(const Rocket& rocket, int k);

/// Liftoff thrust-to-weight (thrustToWeightAtIgnition of the first stage).
double liftoffThrustToWeight(const Rocket& rocket);

} // namespace orbit
