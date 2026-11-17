# Orbit Simulator: Plan

## Context
The goal is to build intuition for orbital mechanics with a graphical simulator. A rocket launches from a site on Earth and its trajectory is drawn. You can also sweep one variable (mass, burn time, pitch angle, ...) and see a family of trajectories next to each other. The project directory (`~/Documents/src/OrbitSumulator`) is currently empty.

Decisions from the clarification round:
- **Web browser** app, starting with a **2D** view. 3D comes later.
- Physics: point-mass Earth gravity plus **Earth's rotation** (launch latitude and direction matter). No drag or staging yet.
- Steering: a **simple pitch program** (vertical rise, pitch-over, then gravity turn).
- Sweeps: **both** a live slider with fading "ghost" paths and a batch sweep (variable, range and steps give N colour-coded lines).
- Readouts: **orbit stats** and **time-series graphs** (altitude, speed, mass).
- Code style: clean and well commented so you can **read and tweak** it, especially the physics.

## Approach
Plain HTML, CSS and JavaScript ES modules with **no build step and no dependencies**. All drawing uses Canvas 2D. Physics lives in pure modules with no DOM access, so Node can unit-test it. To run it: `python3 -m http.server` in the project folder, then open `localhost:8000`.

### File layout
```
index.html            layout: canvas, control panel, stats panel, graph area
style.css
src/
  constants.js        G·M_earth, R_earth, ω_earth, g0 (SI units, commented)
  vec.js              tiny 2D vector helpers
  rocket.js           rocket parameters → thrust, mass flow (F / (Isp·g0)), burn time
  guidance.js         pitch program: vertical → pitch-over kick → gravity turn
  integrator.js       RK4 step for state {x, y, vx, vy, m}
  simulate.js         simulate(params) → { samples[], outcome, orbit }
  orbit.js            state vector → energy, a, e, periapsis, apoapsis, period
  sweep.js            runSweep(baseParams, varName, min, max, steps) → results[]
  ui/
    params.js         parameter definitions (name, unit, default, min, max) used by the UI and the sweep dropdown
    view.js           trajectory canvas: Earth, paths, pan/zoom, colour legend
    graphs.js         small canvas line charts (altitude, speed, mass vs time)
    controls.js       sliders, ghost-path history, sweep form, wiring
  main.js
test/
  physics.test.js     node:test checks (see Verification)
README.md             how to run, plus a short "physics in this sim" explainer
```

### Physics model (in `simulate.js` and the modules it uses)
- **Frame:** a 2D inertial frame in the plane of the trajectory, with Earth centred at the origin. The launch site sits on the surface. It starts with Earth-rotation velocity `ω·R·cos(latitude)`, projected onto the launch direction (east gets the full boost, west a penalty). Simplification: the trajectory plane contains the launch direction. This is exact for launches due east or west and approximate for other directions.
- **Forces:** gravity `−μ r/|r|³` plus thrust `F` along the guidance direction while there is propellant left. Mass drops at `ṁ = F/(Isp·g0)`.
- **Guidance:** vertical until `pitchStartTime`. The rocket then tilts to `pitchKickAngle` from vertical and holds that angle for a few seconds. After that, thrust points along the velocity vector (gravity turn).
- **Integrator:** fixed-step RK4. A small step (0.1 s) during the burn and a larger one (about 1–5 s) when coasting, so orbits stay closed and accurate.
- **End conditions:** impact (r < R), escape (positive energy and r far from Earth), completing N orbits, or a maximum simulated time.
- **Outcome:** `crashed`, `suborbital` (periapsis below the surface), `orbit` (with periapsis and apoapsis altitudes), or `escape`.

### Parameters you can adjust and sweep (`ui/params.js`)
Dry mass, propellant mass, thrust, Isp, launch latitude, launch direction (east/west), pitch start time, pitch kick angle. Derived values (burn time, initial thrust-to-weight ratio, ideal Δv from the rocket equation) are shown live. Burn time can also be swept directly: in that mode propellant is adjusted to match the burn time.

### UI
- **Main canvas:** Earth (with a thin "100 km line" ring), trajectories, and a marker for the launch site. Mouse wheel zooms, dragging pans, and there are "fit to Earth" and "fit to trajectory" buttons.
- **Live mode:** sliders re-simulate on every change. The last ~8 paths stay as fading ghosts, and a "clear ghosts" button removes them.
- **Sweep mode:** choose a variable, min, max and number of steps, then click Run. You get N lines on a colour gradient, and a legend lists each value with its outcome and periapsis/apoapsis. Clicking a legend entry highlights that line and shows it in the graphs.
- **Stats panel:** outcome, periapsis/apoapsis altitude, period, eccentricity, Δv used, and maximum altitude.
- **Graphs:** altitude, speed and mass against time for the selected run, with the other sweep runs overlaid faintly.

### Build order (each step leaves something you can run)
1. Physics core, orbit maths and Node tests.
2. Canvas view that draws one trajectory, with fixed parameters.
3. Sliders, live re-simulation and ghost paths.
4. Stats panel and time-series graphs.
5. Batch sweep with legend and highlighting.
6. README with run instructions and a short physics explainer.

Later ideas (not in this plan): atmospheric drag, staging, a 3D globe, an editable burn sequence for maneuvers like circularizing, and the Moon.

## Verification
- `node --test test/` checks the physics:
  - A circular orbit started at 400 km with v = √(μ/r) keeps its radius within 0.1% and its energy constant over 10 orbits.
  - The period matches `2π√(a³/μ)`.
  - `orbit.js` returns the right periapsis and apoapsis for a known elliptical state.
  - The rocket's Δv matches the Tsiolkovsky equation `Isp·g0·ln(m0/mf)`.
  - An eastward launch from the equator reaches higher speed than a westward one with the same rocket (a difference of about 930 m/s).
- Manual checks in the browser (`python3 -m http.server`, then `localhost:8000`):
  - The default rocket reaches a low orbit.
  - A sweep of propellant mass shows the change from crash → suborbital → orbit → escape.
  - Ghost paths fade as you drag sliders.
  - The graphs update when you select a legend entry.
  - I can also open it in Chrome to check for console errors.
