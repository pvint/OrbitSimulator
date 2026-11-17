// UI widgets: parameter sliders, ghost-path history, the sweep form and its legend.

import { GROUPS, PARAMS, TOGGLES, SWEEPABLE, findParam } from './params.js';
import { OUTCOME_STYLE, formatAltitude } from './stats.js';

/**
 * Build a slider plus number box for every parameter, grouped under headings.
 * onChange(key, value) fires with SI values (booleans for toggles).
 * Returns helpers for updating the controls from outside.
 */
export function buildParamControls(container, params, onChange) {
  const rows = {};

  for (const group of GROUPS) {
    const section = el('section', 'param-group');
    section.append(el('h2', null, group));

    for (const toggle of TOGGLES.filter((t) => t.group === group)) {
      const row = el('label', 'toggle');
      const box = el('input');
      box.type = 'checkbox';
      box.checked = params[toggle.key];
      box.addEventListener('change', () => {
        onChange(toggle.key, box.checked);
        refreshDependents();
      });
      row.append(box, ` ${toggle.label}`);
      if (toggle.hint) row.title = toggle.hint;
      section.append(row);
      rows[toggle.key] = { row, input: box };
    }

    for (const p of PARAMS.filter((q) => q.group === group)) {
      const row = el('div', 'param');
      const label = el('label', null, p.label);
      label.htmlFor = `p-${p.key}`;

      const numberBox = el('input', 'num');
      numberBox.type = 'number';
      numberBox.step = p.step;

      const slider = el('input');
      slider.type = 'range';
      slider.id = `p-${p.key}`;
      Object.assign(slider, { min: p.min, max: p.max, step: p.step });

      const setDisplay = (display) => {
        slider.value = display;
        numberBox.value = +display.toFixed(4);
      };
      setDisplay(params[p.key] / p.toSI);

      slider.addEventListener('input', () => {
        numberBox.value = slider.value;
        onChange(p.key, Number(slider.value) * p.toSI);
      });
      // The number box accepts values outside the slider range on purpose.
      numberBox.addEventListener('input', () => {
        const v = Number(numberBox.value);
        if (numberBox.value === '' || !Number.isFinite(v)) return;
        slider.value = v;
        onChange(p.key, v * p.toSI);
      });

      row.append(label, numberBox, el('span', 'unit', p.unit), slider);
      if (p.hint) row.append(el('div', 'hint', p.hint));
      section.append(row);
      rows[p.key] = { row, slider, numberBox, setDisplay };
    }
    container.append(section);
  }

  /** Grey out parameters whose toggle is off (target altitude without auto-circularize). */
  function refreshDependents() {
    for (const p of PARAMS) {
      if (p.requires) rows[p.key].row.classList.toggle('inactive', !rows[p.requires].input.checked);
    }
  }
  refreshDependents();

  return {
    /** Lock a parameter (it is being swept) or unlock it. */
    setLocked(key, locked, reason = '') {
      const r = rows[key];
      if (!r) return;
      r.row.classList.toggle('locked', locked);
      r.slider.disabled = locked;
      r.numberBox.disabled = locked;
      r.row.title = locked ? reason : '';
    },
  };
}

/**
 * Remembers recent live-mode results, drawn as fading "ghost" paths.
 * While a slider is dragged, re-simulation happens on every frame. Keeping
 * every result would fill the history with near-identical paths, so a result
 * is only kept if at least minInterval ms have passed since the last one.
 */
export class GhostHistory {
  constructor(maxGhosts = 8, minInterval = 120) {
    this.maxGhosts = maxGhosts;
    this.minInterval = minInterval;
    this.ghosts = []; // newest first
    this.lastAdded = -Infinity;
  }

  offer(result) {
    const now = performance.now();
    if (now - this.lastAdded < this.minInterval) return;
    this.ghosts.unshift(result);
    this.ghosts.length = Math.min(this.ghosts.length, this.maxGhosts);
    this.lastAdded = now;
  }

  clear() {
    this.ghosts = [];
  }

  /** Opacity for the ghost at a given age (0 = newest). */
  alphaFor(index) {
    return 0.45 * (1 - index / this.maxGhosts);
  }
}

/**
 * Fill the sweep form's variable dropdown and keep min/max/unit in step with
 * the chosen variable. onRun({varKey, min, max, steps}) receives SI values.
 */
export function buildSweepForm({ form, select, min, max, steps, unit }, onRun) {
  for (const p of SWEEPABLE) {
    const option = el('option', null, `${p.label} (${p.unit})`);
    option.value = p.key;
    select.append(option);
  }
  select.value = 'propellantMass';

  const applyDefaults = () => {
    const p = findParam(select.value);
    [min.value, max.value] = p.sweep;
    unit.textContent = p.unit;
  };
  applyDefaults();
  select.addEventListener('change', applyDefaults);

  const read = () => {
    const p = findParam(select.value);
    return {
      varKey: p.key,
      min: Number(min.value) * p.toSI,
      max: Number(max.value) * p.toSI,
      steps: Math.max(1, Math.min(60, Math.round(Number(steps.value) || 1))),
    };
  };
  form.addEventListener('submit', (e) => {
    e.preventDefault();
    onRun(read());
  });
  return { read };
}

/** Colour for position t ∈ [0, 1] along the sweep (blue → cyan → green → yellow → red). */
const GRADIENT = ['#3b82f6', '#06b6d4', '#22c55e', '#eab308', '#f97316', '#ef4444'];
export function gradientColor(t) {
  const x = Math.min(Math.max(t, 0), 1) * (GRADIENT.length - 1);
  const i = Math.min(Math.floor(x), GRADIENT.length - 2);
  const [a, b] = [hexToRgb(GRADIENT[i]), hexToRgb(GRADIENT[i + 1])];
  const f = x - i;
  return `rgb(${a.map((c, k) => Math.round(c + (b[k] - c) * f)).join(',')})`;
}

/** One legend row per sweep run. Clicking a row calls onSelect(index). */
export function renderLegend(list, runs, varKey, selectedIndex, onSelect) {
  const p = findParam(varKey);
  list.innerHTML = '';
  runs.forEach((run, i) => {
    const { outcome, orbit } = run.result;
    const item = el('li', i === selectedIndex ? 'selected' : null);
    const swatch = el('span', 'swatch');
    swatch.style.background = run.color;
    const value = el('span', 'value', `${+(run.value / p.toSI).toPrecision(4)} ${p.unit}`);
    const badge = el('span', 'outcome', OUTCOME_STYLE[outcome].label);
    badge.style.color = OUTCOME_STYLE[outcome].color;
    const detail = el('span', 'detail',
      outcome === 'orbit' || outcome === 'escape'
        ? `${formatAltitude(orbit.periapsisAltitude)} × ${formatAltitude(orbit.apoapsisAltitude)}`
        : `max ${formatAltitude(run.result.maxAltitude)}`);
    item.append(swatch, value, badge, detail);
    item.addEventListener('click', () => onSelect(i));
    list.append(item);
  });
}

function hexToRgb(hex) {
  const n = parseInt(hex.slice(1), 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}
