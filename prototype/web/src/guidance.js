// Pitch program: decides which way the engine points.
//
// Three phases:
//   1. VERTICAL      t < pitchStartTime
//                    Climb straight up, away from Earth's centre.
//   2. KICK          pitchStartTime ≤ t < pitchStartTime + pitchHoldTime
//                    Tilt pitchKickAngle away from vertical, towards
//                    downrange, and hold that angle.
//   3. GRAVITY_TURN  after that
//                    Point along the velocity relative to the ground. Gravity
//                    bends the path towards horizontal, and the thrust follows
//                    it round. This is how real rockets climb through the
//                    atmosphere without turning sideways into the airflow.
//
// A bigger or earlier kick gives a flatter trajectory. Too much and the rocket
// turns over before it is high enough and hits the ground. Too little and it
// goes too high and never builds up enough horizontal speed for orbit.
//
// A fourth phase, CIRCULARIZE, is used when simulate.js runs the
// auto-circularize burn. The engine points along
//     Δv = v_circular − v
// where v_circular is circular-orbit speed √(μ/r) pointing horizontally
// downrange. That is the velocity change that would turn the current motion
// into a circular orbit at this altitude. At apoapsis this is simply
// "burn horizontally". If the rocket starts to sink during the burn, Δv tilts
// upward to cancel that too. The burn ends when Δv reaches zero.

import { vec, add, sub, scale, normalize, length } from './vec.js';
import { MU_EARTH } from './constants.js';

export const PHASE = Object.freeze({
  VERTICAL: 'vertical',
  KICK: 'kick',
  GRAVITY_TURN: 'gravityTurn',
  CIRCULARIZE: 'circularize',
});

const DEG = Math.PI / 180;

export function guidancePhase(t, params) {
  if (t < params.pitchStartTime) return PHASE.VERTICAL;
  if (t < params.pitchStartTime + params.pitchHoldTime) return PHASE.KICK;
  return PHASE.GRAVITY_TURN;
}

/** Times at which the guidance phase changes. The integrator lands a step
 *  exactly on each one so that no step mixes two phases. */
export function phaseChangeTimes(params) {
  return [params.pitchStartTime, params.pitchStartTime + params.pitchHoldTime];
}

/** Local "up" (away from Earth's centre) and "downrange" (horizontal, in the
 *  direction of travel). In this sim the rocket always flies clockwise round
 *  the origin (it starts at the top heading +x), so downrange is up rotated
 *  90° clockwise. */
function localFrame(pos) {
  const up = normalize(pos);
  return { up, downrange: vec(up.y, -up.x) };
}

/** Velocity change still needed to be in a circular orbit at the current radius. */
export function circularizationDeltaV(pos, vel, mu = MU_EARTH) {
  const { downrange } = localFrame(pos);
  const vCircular = scale(downrange, Math.sqrt(mu / length(pos)));
  return sub(vCircular, vel);
}

/**
 * Unit vector the engine points along.
 * @param phase           one of PHASE
 * @param pos             position {x, y} relative to Earth's centre
 * @param vel             inertial velocity
 * @param surfaceRelVel   velocity relative to the rotating ground
 * @param params          needs pitchKickAngle (degrees)
 * @param mu              gravitational parameter (for CIRCULARIZE)
 */
export function thrustDirection(phase, pos, vel, surfaceRelVel, params, mu = MU_EARTH) {
  const { up, downrange } = localFrame(pos);

  switch (phase) {
    case PHASE.VERTICAL:
      return up;
    case PHASE.KICK: {
      const a = params.pitchKickAngle * DEG;
      return add(scale(up, Math.cos(a)), scale(downrange, Math.sin(a)));
    }
    case PHASE.GRAVITY_TURN:
      // If the rocket is hardly moving relative to the ground, its velocity
      // gives no useful direction, so keep pointing up.
      return length(surfaceRelVel) > 1e-3 ? normalize(surfaceRelVel) : up;
    case PHASE.CIRCULARIZE:
      return normalize(circularizationDeltaV(pos, vel, mu));
    default:
      throw new Error(`unknown guidance phase ${phase}`);
  }
}
