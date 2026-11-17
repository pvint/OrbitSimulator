// Every adjustable parameter, used to build the sliders and the sweep dropdown.
//
// Values in the UI are in `unit`. Multiply by `toSI` to get the value the
// physics uses (angles stay in degrees, since the physics expects degrees).
// `sweep` is the default [min, max] offered when the parameter is picked for a sweep.

import { BURN_TIME } from '../sweep.js';

export const GROUPS = ['Rocket', 'Launch site', 'Pitch program', 'Orbit insertion'];

export const PARAMS = [
  { key: 'dryMass', group: 'Rocket', label: 'Dry mass', unit: 't', toSI: 1000,
    min: 0.5, max: 50, step: 0.1, default: 4, sweep: [1, 10] },
  { key: 'propellantMass', group: 'Rocket', label: 'Propellant', unit: 't', toSI: 1000,
    min: 0, max: 200, step: 0.5, default: 60, sweep: [5, 100] },
  { key: 'thrust', group: 'Rocket', label: 'Thrust', unit: 'kN', toSI: 1000,
    min: 50, max: 5000, step: 10, default: 1200, sweep: [600, 3000] },
  { key: 'isp', group: 'Rocket', label: 'Specific impulse', unit: 's', toSI: 1,
    min: 150, max: 500, step: 1, default: 340, sweep: [250, 450] },

  { key: 'latitude', group: 'Launch site', label: 'Latitude', unit: '°', toSI: 1,
    min: -90, max: 90, step: 0.5, default: 28.5, sweep: [0, 90] },
  { key: 'azimuth', group: 'Launch site', label: 'Direction (azimuth)', unit: '°', toSI: 1,
    min: 0, max: 360, step: 1, default: 90, sweep: [0, 360], hint: '90° = east, 270° = west' },

  { key: 'pitchStartTime', group: 'Pitch program', label: 'Pitch start', unit: 's', toSI: 1,
    min: 0, max: 60, step: 0.5, default: 10, sweep: [2, 30] },
  { key: 'pitchKickAngle', group: 'Pitch program', label: 'Kick angle', unit: '°', toSI: 1,
    min: 0, max: 60, step: 0.25, default: 25, sweep: [0, 45] },
  { key: 'pitchHoldTime', group: 'Pitch program', label: 'Kick hold', unit: 's', toSI: 1,
    min: 0, max: 60, step: 0.5, default: 10, sweep: [2, 40] },

  { key: 'targetAltitude', group: 'Orbit insertion', label: 'Target altitude', unit: 'km', toSI: 1000,
    min: 80, max: 2000, step: 5, default: 200, sweep: [100, 1000], requires: 'autoCircularize' },
];

/** On/off switches. These can't be swept. */
export const TOGGLES = [
  { key: 'autoCircularize', group: 'Orbit insertion', label: 'Auto-circularize at apoapsis', default: true,
    hint: 'Cut the engine when apoapsis reaches the target, coast up, then burn to circularize. ' +
          'Off: burn until the propellant runs out.' },
];

/** Extra sweep-only variable (see sweep.js). */
export const BURN_TIME_PARAM = {
  key: BURN_TIME, label: 'Burn time (adjusts propellant)', unit: 's', toSI: 1, sweep: [30, 300],
};

export const SWEEPABLE = [...PARAMS, BURN_TIME_PARAM];

export function findParam(key) {
  return SWEEPABLE.find((p) => p.key === key);
}

/** Default parameter set, in SI units. */
export function defaultParams() {
  const params = {};
  for (const p of PARAMS) params[p.key] = p.default * p.toSI;
  for (const t of TOGGLES) params[t.key] = t.default;
  return params;
}
