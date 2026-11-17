// Number formatting, the flight stats panel and the derived-values panel.

import { OUTCOME, inPlaneRotationRate } from '../simulate.js';
import { burnTime, massFlowRate, idealDeltaV, initialThrustToWeight } from '../rocket.js';
import { R_EARTH } from '../constants.js';

export const OUTCOME_STYLE = {
  [OUTCOME.CRASHED]: { label: 'Crashed', color: '#f87171' },
  [OUTCOME.SUBORBITAL]: { label: 'Suborbital', color: '#fb923c' },
  [OUTCOME.ORBIT]: { label: 'Orbit', color: '#4ade80' },
  [OUTCOME.ESCAPE]: { label: 'Escape', color: '#c084fc' },
};

const num = (v, digits = 0) =>
  v.toLocaleString(undefined, { minimumFractionDigits: digits, maximumFractionDigits: digits });

/** Altitude in km. Infinity (an escape trajectory's apoapsis) shows as ∞. */
export function formatAltitude(m) {
  if (!Number.isFinite(m)) return '∞';
  const km = m / 1000;
  return `${num(km, Math.abs(km) < 10 ? 1 : 0)} km`;
}

export function formatDuration(s) {
  if (!Number.isFinite(s)) return '∞';
  if (s < 120) return `${num(s, 1)} s`;
  if (s < 7200) return `${num(s / 60, 1)} min`;
  if (s < 3 * 86400) return `${Math.floor(s / 3600)} h ${Math.round((s % 3600) / 60)} min`;
  return `${num(s / 86400, 1)} days`;
}

export const formatSpeed = (v) => `${num(v)} m/s`;
export const formatMass = (kg) => (kg >= 1000 ? `${num(kg / 1000, 1)} t` : `${num(kg)} kg`);

export function outcomeBadge(outcome) {
  const { label, color } = OUTCOME_STYLE[outcome];
  return `<span class="badge" style="--badge-color:${color}">${label}</span>`;
}

function renderList(dl, rows) {
  dl.innerHTML = rows
    .map(([term, value, title = '']) => `<dt title="${title}">${term}</dt><dd>${value}</dd>`)
    .join('');
}

/** The flight stats panel for one simulation result. */
export function renderStats(dl, result) {
  if (!result) {
    dl.innerHTML = '';
    return;
  }
  const { orbit, cutoff } = result;
  const belowSurface = (m) => (m < 0 ? ' <span class="muted">(underground)</span>' : '');
  const rows = [
    ['Outcome', outcomeBadge(result.outcome)],
    ['Periapsis', formatAltitude(orbit.periapsisAltitude) + belowSurface(orbit.periapsisAltitude),
      'Lowest point of the orbit after the last engine shutdown'],
    ['Apoapsis', formatAltitude(orbit.apoapsisAltitude), 'Highest point of the orbit'],
    ['Period', orbit.bound ? formatDuration(orbit.period) : '—', 'Time for one full orbit'],
    ['Eccentricity', num(orbit.eccentricity, 4), '0 = circle, <1 ellipse, ≥1 escape'],
    ['Max altitude', formatAltitude(result.maxAltitude), 'Highest point reached during the simulation'],
    ['Δv used', formatSpeed(result.deltaVUsed), 'Isp·g0·ln(m0 / m_final)'],
    ['Propellant left', formatMass(result.propellantLeft)],
  ];
  if (cutoff) {
    rows.push(
      ['Last engine cutoff', `T+${formatDuration(cutoff.t)}`],
      ['↳ altitude / speed', `${formatAltitude(cutoff.altitude)} / ${formatSpeed(cutoff.speed)}`],
    );
  }
  rows.push(['Simulated time', formatDuration(result.duration)]);
  renderList(dl, rows);
}

/** Values that follow directly from the parameters. */
export function renderDerived(dl, params) {
  const mdot = massFlowRate(params.thrust, params.isp);
  const twr = initialThrustToWeight(params);
  const boost = inPlaneRotationRate(params.latitude, params.azimuth) * R_EARTH;
  renderList(dl, [
    ['Burn time', formatDuration(burnTime(params)), 'Propellant ÷ mass flow, at full thrust'],
    ['Mass flow', `${num(mdot, 1)} kg/s`, 'F / (Isp·g0)'],
    ['Liftoff T/W', `${num(twr, 2)}${twr <= 1 ? ' <span class="warn">too low to lift off</span>' : ''}`,
      'Thrust ÷ weight on the pad. Must be above 1.'],
    ['Ideal Δv', formatSpeed(idealDeltaV(params)), 'Tsiolkovsky: Isp·g0·ln(m0 / m_dry)'],
    ['Earth rotation boost', `${boost >= 0 ? '+' : ''}${formatSpeed(boost)}`,
      'Speed the launch pad already has along the launch direction: ω·R·cos(lat)·sin(azimuth)'],
  ]);
}
