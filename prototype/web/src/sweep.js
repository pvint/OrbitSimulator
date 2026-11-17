// Batch sweeps: run the same rocket many times, changing one parameter.

import { simulate } from './simulate.js';
import { propellantForBurnTime } from './rocket.js';

/** Burn time is not a parameter in its own right. Sweeping it changes the
 *  propellant load so that the engine runs for that long. */
export const BURN_TIME = 'burnTime';

/** A copy of params with varName set to value (in SI units). */
export function applySweepValue(params, varName, value) {
  if (varName === BURN_TIME) {
    return { ...params, propellantMass: propellantForBurnTime(params, value) };
  }
  return { ...params, [varName]: value };
}

/** `steps` evenly spaced values from min to max, including both ends. */
export function sweepValues(min, max, steps) {
  if (steps <= 1) return [min];
  return Array.from({ length: steps }, (_, i) => min + ((max - min) * i) / (steps - 1));
}

/** Runs one simulation per value. Returns [{value, params, result}]. */
export function runSweep(baseParams, varName, min, max, steps, options = {}) {
  return sweepValues(min, max, steps).map((value) => {
    const params = applySweepValue(baseParams, varName, value);
    return { value, params, result: simulate(params, options) };
  });
}
