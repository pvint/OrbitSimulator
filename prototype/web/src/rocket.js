// Rocket engine and mass relationships.
//
// A rocket here is { dryMass, propellantMass, thrust, isp } in SI units
// (kg, kg, N, s). The engine burns at full, constant thrust until the
// propellant is gone.

import { G0, MU_EARTH, R_EARTH } from './constants.js';

/** Exhaust velocity v_e = Isp · g0 (m/s). */
export function exhaustVelocity(isp) {
  return isp * G0;
}

/** Propellant mass flow ṁ = F / v_e (kg/s). More thrust or a less efficient
 *  engine (lower Isp) burns propellant faster. */
export function massFlowRate(thrust, isp) {
  return thrust / exhaustVelocity(isp);
}

/** How long the engine can run at full thrust (s). */
export function burnTime(rocket) {
  return rocket.propellantMass / massFlowRate(rocket.thrust, rocket.isp);
}

/** Propellant needed for a given burn time (s). Used when sweeping burn time. */
export function propellantForBurnTime(rocket, seconds) {
  return seconds * massFlowRate(rocket.thrust, rocket.isp);
}

/** Ideal Δv from the Tsiolkovsky rocket equation: v_e · ln(m0 / m_dry).
 *  "Ideal" means no gravity: in flight, some of this goes to fighting gravity. */
export function idealDeltaV(rocket) {
  const m0 = rocket.dryMass + rocket.propellantMass;
  return exhaustVelocity(rocket.isp) * Math.log(m0 / rocket.dryMass);
}

/** Thrust-to-weight ratio on the launch pad. Must be above 1 to lift off. */
export function initialThrustToWeight(rocket) {
  const m0 = rocket.dryMass + rocket.propellantMass;
  const surfaceGravity = MU_EARTH / (R_EARTH * R_EARTH);
  return rocket.thrust / (m0 * surfaceGravity);
}
