// Classic fourth-order Runge–Kutta (RK4) integrator.
//
// A state is a plain object of numbers, e.g. {x, y, vx, vy, m}.
// deriv(t, state) must return an object with the same keys holding the rates of
// change: {x: vx, y: vy, vx: ax, vy: ay, m: dm/dt}.
//
// RK4 samples the derivative four times per step (start, two midpoints, end)
// and takes a weighted average. Its error shrinks like dt⁴, so a modest step
// still keeps orbits closed for many revolutions, where simple Euler
// integration would spiral outwards.

/** Returns state + k·h, key by key. */
function addScaled(state, k, h) {
  const out = {};
  for (const key in state) out[key] = state[key] + k[key] * h;
  return out;
}

export function rk4Step(deriv, t, state, dt) {
  const k1 = deriv(t, state);
  const k2 = deriv(t + dt / 2, addScaled(state, k1, dt / 2));
  const k3 = deriv(t + dt / 2, addScaled(state, k2, dt / 2));
  const k4 = deriv(t + dt, addScaled(state, k3, dt));

  const out = {};
  for (const key in state) {
    out[key] = state[key] + (dt / 6) * (k1[key] + 2 * k2[key] + 2 * k3[key] + k4[key]);
  }
  return out;
}
