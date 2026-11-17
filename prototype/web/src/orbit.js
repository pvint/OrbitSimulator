// Turns a position and velocity into the shape of the orbit (a conic section).
//
// Two quantities stay constant along a coasting orbit:
//   specific energy            ε = v²/2 − μ/r
//   specific angular momentum  h = r × v
// Together they fix the orbit's shape:
//   semi-major axis  a = −μ / (2ε)            (negative for escape trajectories)
//   eccentricity     e = √(1 + 2εh²/μ²)       (0 = circle, <1 ellipse, ≥1 escape)
//   semi-latus rect. p = h²/μ
//   periapsis        r_p = p / (1 + e)        (closest point to Earth's centre)
//   apoapsis         r_a = 2a − r_p           (furthest point, bound orbits only)
//   period           T = 2π √(a³/μ)
// (r_a = 2a − r_p equals p / (1 − e) for an ordinary ellipse, but it also works
// for a straight up-and-down path, where h = 0 and e = 1 even though the path is bound.)

import { MU_EARTH, R_EARTH } from './constants.js';

export function orbitalElements(state, mu = MU_EARTH, bodyRadius = R_EARTH) {
  const r = Math.hypot(state.x, state.y);
  const v2 = state.vx * state.vx + state.vy * state.vy;

  const energy = v2 / 2 - mu / r;
  const h = Math.abs(state.x * state.vy - state.y * state.vx);
  const p = (h * h) / mu;
  // Rounding can push the value under the square root slightly below zero for a perfect circle.
  const e = Math.sqrt(Math.max(0, 1 + (2 * energy * h * h) / (mu * mu)));

  const bound = energy < 0;
  const a = energy !== 0 ? -mu / (2 * energy) : Infinity;
  const periapsis = p / (1 + e);
  const apoapsis = bound ? 2 * a - periapsis : Infinity;

  return {
    energy,
    angularMomentum: h,
    semiMajorAxis: a,
    eccentricity: e,
    periapsis,
    apoapsis,
    periapsisAltitude: periapsis - bodyRadius,
    apoapsisAltitude: apoapsis - bodyRadius,
    period: bound ? orbitalPeriod(a, mu) : Infinity,
    bound,
  };
}

/** Kepler's third law: T = 2π √(a³/μ). */
export function orbitalPeriod(a, mu = MU_EARTH) {
  return 2 * Math.PI * Math.sqrt((a * a * a) / mu);
}

/** Speed needed for a circular orbit of radius r: √(μ/r). */
export function circularSpeed(r, mu = MU_EARTH) {
  return Math.sqrt(mu / r);
}

/** Speed at which energy reaches zero, so the craft never comes back: √(2μ/r). */
export function escapeSpeed(r, mu = MU_EARTH) {
  return Math.sqrt((2 * mu) / r);
}
