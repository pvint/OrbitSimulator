// Flies one rocket from launch until it crashes, escapes or completes an orbit.
//
// FRAME
// A 2D inertial (non-rotating) frame in the plane of the trajectory, with
// Earth's centre at the origin. The launch site starts at (0, R) at the top
// of the Earth, and "downrange" is +x, so the rocket travels clockwise.
//
// EARTH'S ROTATION
// The ground under the launch site moves east at ω·R·cos(latitude). Only the
// part of that motion along the launch direction lies in our plane. With
// azimuth measured clockwise from north (90° = east, 270° = west), that part is
//     ω_plane = ω · cos(latitude) · sin(azimuth)
// The rocket starts with velocity ω_plane·R along +x. The same ω_plane is used
// to work out velocity relative to the ground for the gravity turn.
// (This treats the trajectory plane as containing the launch direction. That
// is exact for due east or west launches and approximate otherwise.)
//
// FORCES
//   gravity  a = −μ r / |r|³              (point-mass Earth)
//   thrust   a = F/m along the guidance direction, while the engine runs
//   mass     dm/dt = −F / (Isp·g0)
// No drag and no staging, yet.
//
// FLIGHT SEQUENCE
// With autoCircularize off, the engine burns until the propellant runs out,
// following the pitch program in guidance.js, and then the rocket coasts.
// With it on:
//   ASCENT             pitch program, until the apoapsis reaches the target altitude
//   COAST_TO_APOAPSIS  engine off, climbing towards the high point
//   CIRCULARIZE        at apoapsis, burn towards circular-orbit velocity
//                      (see guidance.js) until the orbit is circular
//   COAST              engine off
// The engine also stops if the propellant runs out partway through.

import { MU_EARTH, R_EARTH, OMEGA_EARTH, KARMAN_LINE } from './constants.js';
import { vec, add, sub, scale, length } from './vec.js';
import { massFlowRate, exhaustVelocity } from './rocket.js';
import {
  PHASE, guidancePhase, phaseChangeTimes, thrustDirection, circularizationDeltaV,
} from './guidance.js';
import { rk4Step } from './integrator.js';
import { orbitalElements } from './orbit.js';

/** Simulation settings. Any of them can be overridden through simulate()'s `options`. */
export const SIM_DEFAULTS = {
  mu: MU_EARTH,
  burnStep: 0.1,              // s: step size while the engine is firing
  // While coasting the step grows with distance from Earth:
  //   dt = coastStepFraction · √(r³/μ)   (√(r³/μ) is a circular period / 2π)
  // so each orbit takes roughly 1250 steps, close in or far out.
  coastStepFraction: 0.005,
  minCoastStep: 0.5,          // s
  maxCoastStep: 300,          // s
  sampleInterval: 1,          // s: store one sample per second during a burn
  orbitsAfterBurnout: 1,      // stop after this many revolutions of coasting
  escapeDistance: 10 * R_EARTH, // stop escape trajectories once this far out
  maxTime: 3 * 86400,         // s: hard limit on simulated time
  maxSteps: 500_000,
};

export const OUTCOME = Object.freeze({
  CRASHED: 'crashed',       // hit the ground without reaching space (100 km)
  SUBORBITAL: 'suborbital', // reached space, but the periapsis is underground
  ORBIT: 'orbit',           // the whole orbit stays above the surface
  ESCAPE: 'escape',         // energy ≥ 0: never comes back
});

export const FLIGHT_MODE = Object.freeze({
  ASCENT: 'ascent',
  COAST_TO_APOAPSIS: 'coastToApoapsis',
  CIRCULARIZE: 'circularize',
  COAST: 'coast',
});

/** Events recorded in result.events, each with the sample where it happened. */
export const EVENT = Object.freeze({
  CUTOFF: 'Engine cutoff (apoapsis at target)',
  CIRC_START: 'Circularization burn start',
  CIRC_END: 'Circularization burn end',
  BURNOUT: 'Propellant exhausted',
});

const DEG = Math.PI / 180;

/** How fast the ground turns within our 2D plane (rad/s). See the header comment. */
export function inPlaneRotationRate(latitudeDeg, azimuthDeg) {
  return OMEGA_EARTH * Math.cos(latitudeDeg * DEG) * Math.sin(azimuthDeg * DEG);
}

/** Gravity from a point-mass Earth: −μ r / |r|³. */
export function gravityAcceleration(pos, mu = MU_EARTH) {
  const r = length(pos);
  return scale(pos, -mu / (r * r * r));
}

/**
 * Run one flight.
 *
 * @param params  {dryMass, propellantMass, thrust, isp,           (kg, kg, N, s)
 *                 latitude, azimuth,                              (degrees)
 *                 pitchStartTime, pitchHoldTime, pitchKickAngle,  (s, s, degrees)
 *                 autoCircularize, targetAltitude}                (boolean, m)
 * @param options overrides for SIM_DEFAULTS
 * @returns {{
 *   samples: Array<{t, x, y, vx, vy, m, altitude, speed, burning}>,
 *   events: Array<{type, sample}>,  // engine cutoffs, circularization start and end
 *   cutoff: object,         // sample at the last engine shutdown (null if never)
 *   orbit: object,          // orbitalElements() of the final coasting trajectory
 *   outcome: string,        // one of OUTCOME
 *   maxAltitude, deltaVUsed, propellantLeft, endReason, duration
 * }}
 */
export function simulate(params, options = {}) {
  const opt = { ...SIM_DEFAULTS, ...options };
  const { mu } = opt;

  const mdot = massFlowRate(params.thrust, params.isp);
  const m0 = params.dryMass + params.propellantMass;
  const targetRadius = R_EARTH + params.targetAltitude;
  const omega = inPlaneRotationRate(params.latitude, params.azimuth);
  const groundVelocityAt = (pos) => vec(omega * pos.y, -omega * pos.x); // clockwise spin

  // Each loop step sets these two. The equations of motion read them.
  let engineOn = false;
  let phase = PHASE.VERTICAL;

  /** Equations of motion: the rate of change of {x, y, vx, vy, m}. */
  function deriv(t, s) {
    const pos = vec(s.x, s.y);
    let acc = gravityAcceleration(pos, mu);
    let massRate = 0;
    if (engineOn) {
      const vel = vec(s.vx, s.vy);
      const relVel = sub(vel, groundVelocityAt(pos));
      const dir = thrustDirection(phase, pos, vel, relVel, params, mu);
      acc = add(acc, scale(dir, params.thrust / s.m));
      massRate = -mdot;
    }
    return { x: s.vx, y: s.vy, vx: acc.x, vy: acc.y, m: massRate };
  }

  // Quantities that tell the sequencer when to switch modes.
  const radialVelocity = (s) => (s.x * s.vx + s.y * s.vy) / Math.hypot(s.x, s.y);
  const apoapsisOf = (s) => orbitalElements(s, mu).apoapsis;
  const circularizationRemaining = (s) =>
    length(circularizationDeltaV(vec(s.x, s.y), vec(s.vx, s.vy), mu));

  /** In each mode, the switch to the next mode happens when this
   *  function goes from negative to ≥ 0. (CIRCULARIZE is handled by step
   *  size instead; see the main loop.) */
  function switchCondition(mode) {
    switch (mode) {
      case FLIGHT_MODE.ASCENT:
        return params.autoCircularize ? (s) => apoapsisOf(s) - targetRadius : null;
      case FLIGHT_MODE.COAST_TO_APOAPSIS:
        return (s) => -radialVelocity(s); // climbing → falling
      default:
        return null;
    }
  }

  /** One RK4 step. If the switch condition crosses zero during the step,
   *  shorten the step (by linear interpolation) so it ends close to the
   *  crossing. This lets the engine stop near the exact moment rather than up to a whole step late. */
  function advance(t, state, dt, condition) {
    let next = rk4Step(deriv, t, state, dt);
    if (!condition) return { next, dt, crossed: false };
    const g0 = condition(state);
    const g1 = condition(next);
    if (!(g0 < 0 && g1 >= 0)) return { next, dt, crossed: false };
    if (Number.isFinite(g1)) {
      dt *= g0 / (g0 - g1);
      next = rk4Step(deriv, t, state, dt);
    }
    return { next, dt, crossed: true };
  }

  // `burning` says whether the engine fires on the stretch of path that starts
  // at this sample. The view uses it to draw burns differently from coasts.
  const makeSample = (t, s, isBurning) => ({
    t, x: s.x, y: s.y, vx: s.vx, vy: s.vy, m: s.m,
    altitude: Math.hypot(s.x, s.y) - R_EARTH,
    speed: Math.hypot(s.vx, s.vy),
    burning: isBurning,
  });

  let t = 0;
  let state = { x: 0, y: R_EARTH, vx: omega * R_EARTH, vy: 0, m: m0 };
  const hasEngine = params.propellantMass > 0 && params.thrust > 0;
  let mode = hasEngine ? FLIGHT_MODE.ASCENT : FLIGHT_MODE.COAST;

  const samples = [makeSample(t, state, hasEngine)];
  const events = [];
  let lastSampleTime = 0;
  let cutoff = hasEngine ? null : samples[0];
  let impacted = false;
  let endReason = 'maxTime';

  // While coasting we add up the angle swept round Earth to count orbits.
  let sweptAngle = 0;
  let prevAngle = Math.atan2(state.y, state.x);

  const phaseTimes = phaseChangeTimes(params);

  for (let step = 0; step < opt.maxSteps && t < opt.maxTime; step++) {
    engineOn = mode === FLIGHT_MODE.ASCENT || mode === FLIGHT_MODE.CIRCULARIZE;

    // Choose a step size. During a burn, shorten the step so it ends exactly
    // on the next pitch-program change or on burnout, so no step straddles one.
    // When circularizing, a final short step delivers exactly the Δv still needed.
    let dt;
    let finalCircularizationStep = false;
    if (engineOn) {
      dt = opt.burnStep;
      if (mode === FLIGHT_MODE.ASCENT) {
        for (const tc of phaseTimes) if (tc > t + 1e-9) dt = Math.min(dt, tc - t);
      } else {
        const timeToFinish = circularizationRemaining(state) / (params.thrust / state.m);
        if (timeToFinish <= dt) {
          dt = timeToFinish;
          finalCircularizationStep = true;
        }
      }
      dt = Math.min(dt, (state.m - params.dryMass) / mdot);
    } else {
      const r = Math.hypot(state.x, state.y);
      dt = opt.coastStepFraction * Math.sqrt((r * r * r) / mu);
      dt = Math.min(opt.maxCoastStep, Math.max(opt.minCoastStep, dt));
    }

    phase = mode === FLIGHT_MODE.CIRCULARIZE ? PHASE.CIRCULARIZE : guidancePhase(t + dt / 2, params);
    const prev = state;
    const stepResult = advance(t, state, dt, switchCondition(mode));
    state = stepResult.next;
    t += stepResult.dt;

    // Hit the ground? Interpolate back to the moment of impact so the path
    // ends on the surface rather than under it.
    const r = Math.hypot(state.x, state.y);
    if (r < R_EARTH) {
      const rPrev = Math.hypot(prev.x, prev.y);
      const f = (rPrev - R_EARTH) / (rPrev - r);
      const hit = {};
      for (const k in state) hit[k] = prev[k] + (state[k] - prev[k]) * f;
      // Interpolating along a straight chord lands slightly inside the circle,
      // so push the point back out onto the surface.
      const rHit = Math.hypot(hit.x, hit.y);
      hit.x *= R_EARTH / rHit;
      hit.y *= R_EARTH / rHit;
      samples.push(makeSample(t - stepResult.dt * (1 - f), hit, engineOn));
      state = hit;
      impacted = true;
      endReason = 'impact';
      break;
    }

    // Mode changes. Running out of propellant ends any burn; otherwise follow
    // the switch condition for the current mode.
    let event = null;
    if (engineOn && state.m - params.dryMass <= 1e-9 * m0) {
      state.m = params.dryMass;
      event = EVENT.BURNOUT;
      mode = FLIGHT_MODE.COAST;
    } else if (stepResult.crossed) {
      if (mode === FLIGHT_MODE.ASCENT) {
        event = EVENT.CUTOFF;
        // Usually we are still climbing and coast up to apoapsis. If we have
        // already passed it, start circularizing straight away.
        mode = radialVelocity(state) > 0 ? FLIGHT_MODE.COAST_TO_APOAPSIS : FLIGHT_MODE.CIRCULARIZE;
      } else if (mode === FLIGHT_MODE.COAST_TO_APOAPSIS) {
        event = EVENT.CIRC_START;
        mode = FLIGHT_MODE.CIRCULARIZE;
      }
    } else if (finalCircularizationStep) {
      event = EVENT.CIRC_END;
      mode = FLIGHT_MODE.COAST;
    }

    // Store samples: every coast step (steps are large), but only one per
    // sampleInterval during a burn (steps there are small), plus every event.
    const burningNow = mode === FLIGHT_MODE.ASCENT || mode === FLIGHT_MODE.CIRCULARIZE;
    if (event || !burningNow || t - lastSampleTime >= opt.sampleInterval - 1e-9) {
      samples.push(makeSample(t, state, burningNow));
      lastSampleTime = t;
    }
    if (event) {
      const sample = samples[samples.length - 1];
      events.push({ type: event, sample });
      if (event === EVENT.CUTOFF && mode === FLIGHT_MODE.CIRCULARIZE) {
        events.push({ type: EVENT.CIRC_START, sample });
      }
      if (event !== EVENT.CIRC_START) cutoff = sample;
      prevAngle = Math.atan2(state.y, state.x);
      sweptAngle = 0;
      continue;
    }

    if (mode === FLIGHT_MODE.COAST) {
      const angle = Math.atan2(state.y, state.x);
      let dAngle = angle - prevAngle;
      if (dAngle > Math.PI) dAngle -= 2 * Math.PI;
      if (dAngle < -Math.PI) dAngle += 2 * Math.PI;
      sweptAngle += dAngle;
      prevAngle = angle;

      const v2 = state.vx * state.vx + state.vy * state.vy;
      if (v2 / 2 - mu / r >= 0 && r > opt.escapeDistance) {
        endReason = 'escapeDistance';
        break;
      }
      if (Math.abs(sweptAngle) >= 2 * Math.PI * opt.orbitsAfterBurnout) {
        endReason = 'orbitsComplete';
        break;
      }
    }
  }

  // The trajectory the rocket coasts on after its last engine shutdown. If the
  // engine never shut down, use wherever the rocket ended up.
  const orbit = orbitalElements(cutoff ?? state, mu, R_EARTH);
  const maxAltitude = samples.reduce((best, s) => Math.max(best, s.altitude), 0);

  return {
    params,
    samples,
    events,
    cutoff,
    orbit,
    outcome: classifyOutcome(impacted, orbit, maxAltitude),
    maxAltitude,
    deltaVUsed: exhaustVelocity(params.isp) * Math.log(m0 / state.m),
    propellantLeft: state.m - params.dryMass,
    endReason,
    duration: t,
  };
}

function classifyOutcome(impacted, orbit, maxAltitude) {
  if (!impacted) {
    if (!orbit.bound) return OUTCOME.ESCAPE;
    if (orbit.periapsis > R_EARTH) return OUTCOME.ORBIT;
  }
  return maxAltitude >= KARMAN_LINE ? OUTCOME.SUBORBITAL : OUTCOME.CRASHED;
}
