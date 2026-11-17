// SPDX-License-Identifier: GPL-3.0-or-later
// Rocket engine and mass relationships.
//
// A single-stage rocket with one engine that burns at full, constant thrust
// until the propellant is gone (or guidance shuts it down).
#pragma once

namespace orbit {

struct Rocket {
    double dryMass = 0.0;        ///< kg, structure + payload, without propellant
    double propellantMass = 0.0; ///< kg at liftoff
    double thrust = 0.0;         ///< N
    double isp = 0.0;            ///< s, specific impulse (engine efficiency)
};

/// Exhaust velocity v_e = Isp · g0 (m/s).
double exhaustVelocity(double isp);

/// Propellant mass flow ṁ = F / v_e (kg/s). More thrust, or a less efficient
/// engine (lower Isp), burns propellant faster.
double massFlowRate(const Rocket& rocket);

/// How long the engine can run at full thrust (s).
double burnTime(const Rocket& rocket);

/// Propellant needed for a given burn time (s). Used when sweeping burn time.
double propellantForBurnTime(const Rocket& rocket, double seconds);

/// Ideal Δv from the Tsiolkovsky rocket equation: v_e · ln(m0 / m_dry) (m/s).
/// "Ideal" means without gravity: in flight, some of it is spent holding
/// the rocket up against gravity instead of building speed.
double idealDeltaV(const Rocket& rocket);

/// Thrust-to-weight ratio on the launch pad. Must be above 1 to lift off.
double liftoffThrustToWeight(const Rocket& rocket);

} // namespace orbit
