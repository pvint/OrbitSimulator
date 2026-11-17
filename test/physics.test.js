// Physics checks. Run with:  node --test test/

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { MU_EARTH, R_EARTH, OMEGA_EARTH, G0 } from '../src/constants.js';
import { rk4Step } from '../src/integrator.js';
import { orbitalElements, orbitalPeriod, circularSpeed } from '../src/orbit.js';
import { idealDeltaV, burnTime, massFlowRate } from '../src/rocket.js';
import { simulate, gravityAcceleration, SIM_DEFAULTS, OUTCOME, EVENT } from '../src/simulate.js';
import { runSweep, BURN_TIME } from '../src/sweep.js';
import { defaultParams } from '../src/ui/params.js';

/** Equations of motion with gravity only, as simulate() uses while coasting. */
function coastDeriv(t, s) {
  const a = gravityAcceleration({ x: s.x, y: s.y });
  return { x: s.vx, y: s.vy, vx: a.x, vy: a.y };
}

/** The coast step simulate() would choose at radius r. */
const coastStep = (r) => SIM_DEFAULTS.coastStepFraction * Math.sqrt(r ** 3 / MU_EARTH);

const specificEnergy = (s) => (s.vx ** 2 + s.vy ** 2) / 2 - MU_EARTH / Math.hypot(s.x, s.y);

/** Orbit with the given periapsis and apoapsis radii, starting at periapsis on the +x axis. */
function ellipseAtPeriapsis(rp, ra) {
  const a = (rp + ra) / 2;
  const vp = Math.sqrt(MU_EARTH * (2 / rp - 1 / a)); // vis-viva equation
  return { x: rp, y: 0, vx: 0, vy: vp };
}

test('circular orbit at 400 km keeps its radius and energy over 10 orbits', () => {
  const r0 = R_EARTH + 400e3;
  let s = { x: r0, y: 0, vx: 0, vy: circularSpeed(r0) };
  const e0 = specificEnergy(s);
  const dt = coastStep(r0);
  const steps = Math.ceil((10 * orbitalPeriod(r0)) / dt);

  let worstRadiusError = 0;
  for (let i = 0; i < steps; i++) {
    s = rk4Step(coastDeriv, i * dt, s, dt);
    worstRadiusError = Math.max(worstRadiusError, Math.abs(Math.hypot(s.x, s.y) - r0) / r0);
  }
  const energyDrift = Math.abs((specificEnergy(s) - e0) / e0);

  assert.ok(worstRadiusError < 1e-3, `radius drifted by ${(worstRadiusError * 100).toFixed(4)}%`);
  assert.ok(energyDrift < 1e-6, `energy drifted by ${energyDrift}`);
});

test('measured period of an ellipse matches 2π√(a³/μ)', () => {
  const rp = R_EARTH + 300e3;
  const ra = R_EARTH + 3000e3;
  const expected = orbitalPeriod((rp + ra) / 2);

  // Integrate until the craft has swept a full turn (crossing the +x axis
  // from below), then interpolate the crossing time.
  let s = ellipseAtPeriapsis(rp, ra);
  const dt = 1;
  let t = 0;
  let measured = null;
  while (t < 2 * expected) {
    const next = rk4Step(coastDeriv, t, s, dt);
    if (t > expected / 2 && s.y < 0 && next.y >= 0) {
      measured = t + dt * (-s.y / (next.y - s.y));
      break;
    }
    s = next;
    t += dt;
  }

  assert.ok(measured !== null, 'never completed an orbit');
  assert.ok(Math.abs(measured - expected) < 1, `measured ${measured} s, expected ${expected} s`);
  assert.ok(Math.abs(orbitalElements(ellipseAtPeriapsis(rp, ra)).period - expected) < 1e-6);
});

test('orbitalElements gives the right periapsis and apoapsis for a known ellipse', () => {
  const rp = R_EARTH + 250e3;
  const ra = R_EARTH + 20000e3;
  const el = orbitalElements(ellipseAtPeriapsis(rp, ra));

  assert.ok(Math.abs(el.periapsis - rp) < 1, `periapsis ${el.periapsis}`);
  assert.ok(Math.abs(el.apoapsis - ra) < 1, `apoapsis ${el.apoapsis}`);
  assert.ok(Math.abs(el.semiMajorAxis - (rp + ra) / 2) < 1);
  assert.ok(Math.abs(el.eccentricity - (ra - rp) / (ra + rp)) < 1e-9);
  assert.ok(el.bound);

  // The same orbit seen from somewhere else along it gives the same answer.
  let s = ellipseAtPeriapsis(rp, ra);
  for (let i = 0; i < 500; i++) s = rk4Step(coastDeriv, i * 5, s, 5);
  const later = orbitalElements(s);
  assert.ok(Math.abs(later.periapsis - rp) < 10);
  assert.ok(Math.abs(later.apoapsis - ra) < 10);
});

test('a straight up-and-down path is bound with a finite apoapsis', () => {
  const r = R_EARTH;
  const v = 3000;
  const el = orbitalElements({ x: 0, y: r, vx: 0, vy: v });
  // Energy conservation: v²/2 − μ/r = −μ/r_max
  const rMax = 1 / (1 / r - v * v / (2 * MU_EARTH));
  assert.ok(el.bound);
  assert.ok(Math.abs(el.apoapsis - rMax) < 1e-3 * rMax);
});

test('simulated burn matches the Tsiolkovsky rocket equation', () => {
  // No gravity, no Earth rotation (latitude 90°) and no pitch-over, so the
  // whole Δv goes into speed.
  const params = {
    ...defaultParams(),
    latitude: 90, pitchStartTime: 1e9, autoCircularize: false,
  };
  const result = simulate(params, { mu: 0 });
  const burnout = result.events.find((e) => e.type === EVENT.BURNOUT).sample;
  const expected = params.isp * G0 * Math.log((params.dryMass + params.propellantMass) / params.dryMass);

  assert.ok(Math.abs(expected - idealDeltaV(params)) < 1e-9);
  assert.ok(Math.abs(burnout.speed - expected) < 0.01, `speed ${burnout.speed}, expected ${expected}`);
  assert.ok(Math.abs(burnout.t - burnTime(params)) < 1e-6, 'burnout at the wrong time');
  assert.ok(Math.abs(result.deltaVUsed - expected) < 1e-6);
});

test('eastward launch from the equator gets a ~930 m/s boost over westward', () => {
  // Isp raised so that even the westward rocket has enough Δv to reach orbit.
  const base = { ...defaultParams(), latitude: 0, isp: 420 };
  const east = simulate({ ...base, azimuth: 90 });
  const west = simulate({ ...base, azimuth: 270 });

  // On the pad, velocity along the launch direction (+x) is +ω·R going east
  // and −ω·R going west.
  const padDifference = east.samples[0].vx - west.samples[0].vx;
  assert.ok(Math.abs(padDifference - 2 * OMEGA_EARTH * R_EARTH) < 1e-6);
  assert.ok(Math.abs(padDifference - 929) < 2, `pad difference ${padDifference}`);

  // Both reach the same 200 km circular orbit, but going west costs about
  // that much more Δv. (Not exactly 929, because the two climbs differ.)
  assert.equal(east.outcome, OUTCOME.ORBIT);
  assert.equal(west.outcome, OUTCOME.ORBIT);
  const extraDeltaV = west.deltaVUsed - east.deltaVUsed;
  assert.ok(Math.abs(extraDeltaV - 929) < 100, `west needed ${extraDeltaV.toFixed(0)} m/s more`);
});

test('the default rocket reaches a low, nearly circular orbit', () => {
  const result = simulate(defaultParams());
  assert.equal(result.outcome, OUTCOME.ORBIT);
  assert.ok(result.orbit.periapsisAltitude > 150e3, `periapsis ${result.orbit.periapsisAltitude}`);
  assert.ok(result.orbit.apoapsisAltitude < 250e3, `apoapsis ${result.orbit.apoapsisAltitude}`);
  assert.equal(result.endReason, 'orbitsComplete');
});

test('obvious failures are classified correctly', () => {
  const base = defaultParams();
  // Thrust below weight: never leaves the pad.
  assert.equal(simulate({ ...base, thrust: 100e3 }).outcome, OUTCOME.CRASHED);
  // Straight up with a small propellant load: goes to space and falls back.
  const hop = simulate({ ...base, propellantMass: 10e3, pitchKickAngle: 0, autoCircularize: false });
  assert.equal(hop.outcome, OUTCOME.SUBORBITAL);
  // A huge Isp gives far more Δv than escape needs. (A gentle kick, because
  // the rocket accelerates so hard that a big kick turns it into the ground.)
  const escape = simulate({ ...base, isp: 1000, pitchKickAngle: 5, autoCircularize: false });
  assert.equal(escape.outcome, OUTCOME.ESCAPE);
});

test('burn-time sweep sets the propellant for the requested burn time', () => {
  const base = defaultParams();
  const runs = runSweep(base, BURN_TIME, 50, 150, 3);
  assert.deepEqual(runs.map((r) => r.value), [50, 100, 150]);
  for (const run of runs) {
    assert.ok(Math.abs(burnTime(run.params) - run.value) < 1e-9);
    assert.ok(Math.abs(run.params.propellantMass - run.value * massFlowRate(base.thrust, base.isp)) < 1e-6);
  }
});
