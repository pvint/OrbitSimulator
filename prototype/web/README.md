# Orbit Simulator: web prototype

> **This is the original 2D browser prototype**, kept for reference. It works
> and is self-contained. Run the commands below from this folder
> (`prototype/web/`). The full desktop app lives at the top of the repository.

A 2D rocket launch simulator for building intuition about orbital mechanics.
A rocket lifts off from a spinning Earth, follows a simple pitch program, and
you see where it ends up: back on the ground, on a suborbital hop, in orbit or
escaping. Drag sliders and the flight re-simulates instantly, or sweep one parameter to see
a whole family of trajectories side by side.

Plain HTML, CSS and JavaScript modules. No build step, no dependencies.

## Running it

```sh
python3 -m http.server 8000     # or: npm start
```

Then open <http://localhost:8000>. (The page loads ES modules, which browsers
refuse to load from `file://`, so it needs a local server.)

Run the physics tests with Node 18 or newer:

```sh
node --test                      # or: npm test
```

## Using it

- **Live mode**: every slider change re-simulates. Recent paths stay on
  screen as fading grey ghosts so you can compare. *Clear ghosts* removes them.
- **Sweep mode**: pick a variable, a range and a number of steps, then *Run
  sweep*. Each run gets a colour from blue to red, and the legend lists its outcome
  and orbit. Click a legend row to highlight it and show it in the graphs.
  Sliders still work: each change re-runs the whole sweep, and the swept
  parameter's slider is locked.
- **View**: scroll to zoom (around the cursor) and drag to pan. Thick lines
  are engine burns, thin lines are coasting, and small rings mark engine
  events. The dashed ring is the 100 km "edge of space" line, and the orange
  triangle is the launch site.
- **Graphs**: altitude, speed and mass against time. Dashed orange lines mark
  engine events. *Zoom to powered flight* trims the time axis to the burns.

## Things to try

- **East vs west.** Set the direction to 270° (west). The pad now moves
  *against* you, so you need about 930 m/s more Δv at the equator. Sweep
  latitude to see the boost shrink towards the poles. (*Earth rotation boost*
  under *Derived* shows the number.)
- **Kick angle.** Sweep it from 0° to 45°. Too small and the rocket goes up
  steeply, burns most of its propellant fighting gravity, and has none left to
  circularize. Too large and it turns over early and flies into the ground.
- **Propellant and thrust-to-weight ratio.** Sweep propellant with the thrust
  fixed. More propellant means more Δv but a heavier rocket and a lower
  liftoff T/W. Past a point the same kick tips the rocket over and it crashes.
- **Turn off auto-circularize.** Now the engine burns until the tank is empty.
  Getting a clean orbit this way is a knife edge: the gravity turn has to
  level off at exactly the moment orbital speed is reached. This is why real
  rockets coast to apoapsis and do a second burn.
- **Escape.** With auto-circularize off, raise Isp to about 450 s and thrust to
  3000 kN, then sweep propellant. Past about 11 km/s of Δv the rocket leaves
  for good.

## The physics in this sim

Everything lives in `src/` as plain functions with no access to the page.
`simulate.js` is the place to start reading.

**Frame.** A 2D, non-rotating frame centred on Earth, in the plane that
contains the launch direction. The rocket starts at the top of the Earth and
flies clockwise.

**Gravity.** A point-mass Earth: `a = −μ r / |r|³`, with
μ = 3.986×10¹⁴ m³/s². No drag yet.

**Earth's rotation.** The launch pad already moves east at
`ω·R·cos(latitude)` (465 m/s at the equator). Only the part along the launch
direction lies in our plane: `ω·R·cos(lat)·sin(azimuth)`. The rocket starts
with that velocity. This is exact for launches due east or west and
approximate for other directions.

**Rocket.** Constant thrust `F`. Propellant burns at `ṁ = F / (Isp·g0)`. The
ideal Δv is the Tsiolkovsky rocket equation `Isp·g0·ln(m0 / m_dry)`. The Δv
actually delivered is the same, but some of it goes into fighting gravity
("gravity losses") instead of building speed.

**Pitch program** (`guidance.js`):
1. Climb vertically until *pitch start*.
2. Tilt to *kick angle* from vertical and hold it for *kick hold* seconds.
3. Gravity turn: point along the velocity relative to the ground. Gravity
   bends the path over, and the thrust follows it.

**Auto-circularize** (when on):
1. Cut the engine as soon as the apoapsis (high point of the current orbit)
   reaches the target altitude.
2. Coast up to apoapsis.
3. Burn in the direction `Δv = v_circular − v`: the velocity change that turns
   the current motion into a circular orbit at this altitude. At apoapsis
   that means burning horizontally, and it tilts up if the rocket starts to
   sink. Stop when Δv reaches zero.

The burn starts at apoapsis instead of being centred on it, so the result is
a few km short of perfectly circular.

**Integration.** Fourth-order Runge–Kutta (`integrator.js`) with a 0.1 s step
while the engine fires. Steps are shortened to land exactly on guidance
changes, burnout and engine cutoffs. While coasting the step grows with
distance (about 1/1250 of an orbit), so orbits stay closed. The test suite
checks that a 400 km orbit holds its radius to 0.1% over 10 revolutions.

**Orbit stats** (`orbit.js`) come from two numbers that stay constant
while coasting:
- energy `ε = v²/2 − μ/r`
- angular momentum `h = r × v`

From them: semi-major axis `a = −μ/2ε`, eccentricity `e = √(1 + 2εh²/μ²)`,
periapsis `h²/μ / (1+e)`, apoapsis `2a − r_p` and period `2π√(a³/μ)`. The
stats panel shows the orbit after the *last* engine shutdown.

**Outcomes:**
| Outcome | Meaning |
|---|---|
| Crashed | Hit the ground without reaching 100 km |
| Suborbital | Reached space, but the orbit dips underground, so it comes back down |
| Orbit | The whole orbit stays above the surface |
| Escape | Energy ≥ 0, so it never comes back. Its listed periapsis is where the hyperbola *would* have come closest, which may be underground. |

A simulation ends on impact, after one full orbit of coasting, once an escaping
rocket passes 10 Earth radii, or after 3 simulated days.

## Files

```
index.html, style.css     layout and look
src/constants.js          μ, R, ω, g0
src/vec.js                2D vector helpers
src/rocket.js             mass flow, burn time, Δv, T/W
src/guidance.js           pitch program and circularization steering
src/integrator.js         RK4 step
src/simulate.js           one full flight → samples, events, outcome, orbit
src/orbit.js              state vector → orbital elements
src/sweep.js              run one parameter across a range
src/ui/params.js          parameter list (labels, units, ranges, defaults)
src/ui/view.js            trajectory canvas with pan and zoom
src/ui/graphs.js          time-series charts
src/ui/stats.js           number formatting, stats and derived panels
src/ui/controls.js        sliders, ghost history, sweep form, legend
src/main.js               app state and wiring
test/physics.test.js      node:test checks
```

## Ideas for later

Atmospheric drag, staging, a 3D globe, an editable burn sequence (Hohmann
transfers, plane changes), and the Moon.
