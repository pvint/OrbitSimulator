// Entry point: holds the app state and connects the controls, the simulation
// and the displays.
//
// Two modes:
//   live   one rocket. Every slider change re-simulates, and earlier paths stay as fading ghosts.
//   sweep  one parameter runs from min to max over N runs. Slider changes
//          re-run the whole sweep.

import { simulate } from './simulate.js';
import { runSweep, BURN_TIME } from './sweep.js';
import { defaultParams, PARAMS } from './ui/params.js';
import { TrajectoryView } from './ui/view.js';
import { drawChart } from './ui/graphs.js';
import {
  buildParamControls, GhostHistory, buildSweepForm, gradientColor, renderLegend,
} from './ui/controls.js';
import { renderStats, renderDerived } from './ui/stats.js';

const $ = (id) => document.getElementById(id);
const LIVE_COLOR = '#38bdf8';
const GHOST_COLOR = '#94a3b8';

const state = {
  params: defaultParams(),
  mode: 'live',
  current: null,               // latest live-mode result
  ghosts: new GhostHistory(),
  sweep: null,                 // { spec, runs: [{value, params, result, color}], selected }
};

const view = new TrajectoryView($('view'));

const paramControls = buildParamControls($('param-groups'), state.params, (key, value) => {
  state.params[key] = value;
  scheduleUpdate();
});

const sweepForm = buildSweepForm({
  form: $('sweep-form'),
  select: $('sweep-var'),
  min: $('sweep-min'),
  max: $('sweep-max'),
  steps: $('sweep-steps'),
  unit: $('sweep-unit'),
}, (spec) => {
  runSweepNow(spec);
  render();
  view.fitTrajectory();
});

// Slider events can arrive faster than the screen refreshes. Re-simulate at
// most once per frame.
let updatePending = false;
function scheduleUpdate() {
  if (updatePending) return;
  updatePending = true;
  requestAnimationFrame(() => {
    updatePending = false;
    update();
  });
}

/** Re-simulate for the current parameters, then redraw. */
function update() {
  if (state.mode === 'live') {
    if (state.current) state.ghosts.offer(state.current);
    state.current = simulate(state.params);
  } else {
    runSweepNow(state.sweep.spec);
  }
  render();
}

function runSweepNow(spec) {
  const runs = runSweep(state.params, spec.varKey, spec.min, spec.max, spec.steps)
    .map((run, i, all) => ({ ...run, color: gradientColor(all.length > 1 ? i / (all.length - 1) : 0.5) }));

  // Keep the selected line if the sweep has the same shape as before.
  const prev = state.sweep;
  const sameShape = prev && prev.spec.varKey === spec.varKey && prev.runs.length === runs.length;
  state.sweep = { spec, runs, selected: sameShape ? prev.selected : Math.floor((runs.length - 1) / 2) };

  // The swept parameter's slider has no effect during a sweep, so lock it.
  const lockedKey = spec.varKey === BURN_TIME ? 'propellantMass' : spec.varKey;
  for (const p of PARAMS) paramControls.setLocked(p.key, p.key === lockedKey, 'Being swept, see the Sweep panel');
}

function setMode(mode) {
  state.mode = mode;
  for (const tab of document.querySelectorAll('.tab')) tab.classList.toggle('active', tab.dataset.mode === mode);
  $('live-panel').hidden = mode !== 'live';
  $('sweep-panel').hidden = mode !== 'sweep';
  $('legend-section').hidden = mode !== 'sweep';

  if (mode === 'sweep') {
    runSweepNow(sweepForm.read());
    render();
  } else {
    for (const p of PARAMS) paramControls.setLocked(p.key, false);
    update();
  }
}

function render() {
  renderDerived($('derived'), state.params);

  if (state.mode === 'live') {
    const { ghosts } = state.ghosts;
    // Oldest ghost first so newer ones are drawn on top.
    const paths = ghosts
      .map((g, i) => ({ samples: g.samples, color: GHOST_COLOR, alpha: state.ghosts.alphaFor(i), width: 1 }))
      .reverse();
    paths.push({ samples: state.current.samples, events: state.current.events, color: LIVE_COLOR, showMarkers: true });
    view.setPaths(paths);
    renderStats($('stats'), state.current);
    drawGraphs(state.current, LIVE_COLOR, []);
  } else {
    const { runs, selected, spec } = state.sweep;
    const paths = runs.map((run, i) => ({
      samples: run.result.samples,
      events: run.result.events,
      color: run.color,
      alpha: i === selected ? 1 : 0.55,
      width: i === selected ? 2.5 : 1.2,
      showMarkers: i === selected,
    }));
    paths.push(paths.splice(selected, 1)[0]); // selected line on top
    view.setPaths(paths);
    renderStats($('stats'), runs[selected].result);
    renderLegend($('legend'), runs, spec.varKey, selected, (i) => {
      state.sweep.selected = i;
      render();
    });
    drawGraphs(runs[selected].result, runs[selected].color, runs.filter((_, i) => i !== selected));
  }
}

/** Altitude, speed and mass against time. The main result is drawn bold, and
 *  `others` (the rest of a sweep) faintly behind it. */
function drawGraphs(main, color, others) {
  const markers = main.events.map((e) => e.sample.t);
  // "Zoom to powered flight": up to the last engine event, plus a little.
  const xMax = $('graph-ascent').checked
    ? Math.max(markers.length ? markers[markers.length - 1] : main.duration, 10) * 1.05
    : main.duration;

  const chart = (canvasId, title, unit, pick) => {
    const toSeries = (result, seriesColor, width, alpha) => ({
      xs: result.samples.map((s) => s.t),
      ys: result.samples.map(pick),
      color: seriesColor, width, alpha,
    });
    drawChart($(canvasId), {
      title, unit, xMax, markers,
      series: [
        ...others.map((o) => toSeries(o.result, o.color, 1, 0.3)),
        toSeries(main, color, 2, 1),
      ],
    });
  };
  chart('graph-altitude', 'Altitude', 'km', (s) => s.altitude / 1000);
  chart('graph-speed', 'Speed (inertial)', 'm/s', (s) => s.speed);
  chart('graph-mass', 'Mass', 't', (s) => s.m / 1000);
}

// Buttons and toggles.
for (const tab of document.querySelectorAll('.tab')) {
  tab.addEventListener('click', () => setMode(tab.dataset.mode));
}
$('clear-ghosts').addEventListener('click', () => {
  state.ghosts.clear();
  render();
});
$('fit-earth').addEventListener('click', () => view.fitEarth());
$('fit-trajectory').addEventListener('click', () => view.fitTrajectory());
$('graph-ascent').addEventListener('change', render);
window.addEventListener('resize', render);

update();
view.fitTrajectory();
