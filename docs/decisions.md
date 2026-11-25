# Decisions

A short log of the decisions that shape the project, and why. Add an entry
when making a choice that would be expensive to reverse or that someone
(including future you) might otherwise question.

Format: what was decided, why, and what else was considered.

---

### 1. C++20 with Qt 6 (2026-11-17)

**Decision:** Desktop app in C++20 with Qt 6.8 or newer. CMake with presets, Ninja.

**Why:** a native, cross-platform desktop app with a good 3D story. C++20 rather
than C++23 because MSVC and Apple Clang implement C++23 later than GCC. Qt 6.8
is a long-term-support release and the first with 2D charts in Qt Graphs.

**Considered:** Python + PySide6 (faster to iterate, but larger installers and
slower physics); TypeScript + Tauri + three.js (would reuse the prototype, but
it's a web UI, not Qt).

### 2. The physics core has no Qt dependency (2026-11-17)

**Decision:** `src/core` is plain C++ and the standard library. The CLI, the
tests and the GUI are separate clients of it.

**Why:** the physics can be tested and run without a display, the CLI gives a
quick way to check results, and the GUI can change without touching the
physics. It also keeps Qt types (float-based) out of the numerics.

### 3. Doubles for physics, floats only for drawing (2026-11-17)

**Decision:** all physics in `double`. The 3D view converts to float
*relative to the camera* when drawing.

**Why:** a float only resolves about 0.5 m at Earth's radius and about 30 m
at the Moon's distance. That is visible as jitter in rendering and
unacceptable in an integrator.

### 4. UI: Widgets shell, Qt Quick scenes for 3D and plots (2026-11-17)

**Decision:** QMainWindow with dock widgets and forms (Qt Widgets). The 3D view
(Qt Quick 3D) and the plots (Qt Graphs) are QML scenes embedded with
`QQuickWidget`: one for the 3D view and one for all the plots.

**Why:** Widgets give mature, native-looking forms and docking. Qt Quick 3D
renders through Qt's RHI (Metal, Direct3D, Vulkan or OpenGL), so it suits all
platforms. Qt3D is in maintenance mode. Qt Charts is deprecated since Qt 6.10;
its replacement, Qt Graphs, is Qt Quick-only, hence the embedding.

**Note:** don't call `QQuick3D::idealSurfaceFormat()` before the
QApplication exists. In Qt 6.11 it crashes, and it isn't needed for
`QQuickWidget`.

### 5. Integrator: Dormand–Prince 5(4) with event location (2026-11-17, M1)

**Decision:** an adaptive Runge–Kutta integrator with error control, plus
dense output to pin down events (engine cutoff, apoapsis, impact, entering the
Moon's sphere of influence) precisely.

**Why:** the prototype's fixed-step RK4 with hand-tuned step sizes worked
near Earth. A lunar trajectory needs tiny steps near the Moon and large ones
in between, which an adaptive integrator does automatically.

### 6. Tests with Catch2 v3 (2026-11-17)

**Decision:** Catch2 v3. CMake uses an installed Catch2 if there is one,
otherwise it downloads a pinned version with FetchContent.

**Why:** widely used and needs no Qt. Qt Test would tie the core's tests to Qt.

### 7. Bundling Qt in packages (2026-11-17)

**Decision:** `ORBIT_DEPLOY_QT` bundles the Qt runtime when packaging. It is on
by default on Windows and macOS and off on Linux.

**Why:** on Linux, Qt usually comes from the distribution. Its plugin folders
also hold third-party plugins (here: KDE image-format plugins) that Qt's
deploy step fails to relocate. Distributable Linux builds should be made from
the official Qt (qt.io) in CI and shipped as an AppImage.

### 8. Pure gravity turns need help: auto-circularize (from the prototype)

**Decision:** the default flight program cuts the engine when apoapsis reaches
the target altitude, coasts to apoapsis, then burns towards circular velocity
(`Δv = v_circular − v`). Turning it off gives a burn until the propellant runs out.

**Why:** in the prototype, 40,000 random rocket and pitch settings almost never
produced a clean low orbit from a single burn to depletion. Real launchers
also use closed-loop guidance and a second burn.

### 9. Licence: GPL-3.0-or-later (2026-11-17)

**Decision:** the project is licensed GPL-3.0-or-later. `LICENSE` holds the
full text, and every source file starts with an
`SPDX-License-Identifier: GPL-3.0-or-later` line.

**Why:** the user's choice: anyone may use, change and share it, but
modified versions must stay free. Qt's LGPL is compatible.

### 10. The kick tilts within a launch plane fixed at liftoff (2026-11-17, M1)

**Decision:** "downrange" is the horizontal direction in the plane through
Earth's centre, the launch site and the launch direction, fixed in inertial
space at liftoff. It is not recomputed from the local compass each instant.

**Why:** the local-compass version breaks at the poles: "north" flips as
soon as the rocket moves, the thrust direction oscillates and the
integrator stalls (found when testing a launch from 90°N). The fixed plane is
defined everywhere and is closer to what real ascent guidance does.

### 11. Mission parameters are described once, in the core (2026-11-17, M2)

**Decision:** `orbit/core/mission_parameters.hpp` lists every adjustable number
(key, label, unit, range, getter/setter in display units). The GUI builds
its sliders and the CLI its options from it; sweeps and scenario files will
use the same keys.

**Why:** without it, every new parameter would need edits in three or four
places, and they would drift apart. Keeping it in the core (it's plain
data, no Qt) lets non-GUI tools share it.

### 12. GUI tests with Qt Test, run offscreen (2026-11-17, M2)

**Decision:** the panels and the simulation controller live in a small
library (`orbitsim_ui`) that a Qt Test executable drives the way a user
would (set spin boxes, toggle checkboxes, wait for results). Tests run with
`QT_QPA_PLATFORM=offscreen`, so no display is needed.

**Why:** the user doesn't click through every change, so the wiring between
widgets, the mission and the worker thread needs automatic checks. Qt Test
(QSignalSpy, QTRY_COMPARE) suits widgets and signals better than Catch2.

### 13. Earth texture: Blue Marble Next Generation, 8192 × 4096 (2026-11-17, M3)

**Decision:** the day-side texture is NASA's Blue Marble Next Generation
with topography, July 2004 (public domain; credit "NASA Earth Observatory").
It is downscaled from the 21600 × 10800 original to 8192 × 4096 (3.9 MB JPEG)
and compiled into the executable as a Qt resource.
`tools/make_earth_texture.sh` reproduces it.

**Why:** it's the best freely licensed global true-colour image. The
variant without ocean-floor shading looks like the real view from space. 8192
wide is the largest single texture that is safe on most GPUs; 16384 would
need over 500 MB of video memory with mipmaps. Embedding keeps deployment
simple on all platforms.

**Later options:** night lights (NASA Black Marble, also public domain),
clouds, and a higher-resolution texture as a setting for strong GPUs.

### 14. 3D scene conventions (2026-11-17, M3)

**Decision:** scene units are kilometres, with axes (x, z, −y) of the physics
frame (Quick 3D is y-up). The globe is a custom latitude/longitude mesh, not
Quick 3D's built-in sphere. The trajectory is drawn in the inertial frame,
while the ground track and launch site are children of the rotating Earth.

**Why:** in km a 32-bit float resolves under a metre near Earth and about
30 m at the Moon, so no camera-relative tricks are needed yet. The (x, z, −y)
mapping is a rotation, not a mirror image, so east stays east; a test
checks this. The custom mesh puts texture coordinates exactly on
latitude/longitude; the built-in sphere's mapping is undocumented.

### 15. Scenario files: JSON, versioned, in display units (2026-11-17, M4)

**Decision:** scenarios are JSON with `"format": "orbitsim-scenario"` and an
integer `"version"`. Mission values use the parameter table's keys and
display units (t, kN, °, km); missing keys take defaults; unknown keys are
warnings. Parsing lives in the core (nlohmann/json, header-only, MIT), so the
CLI and app share it. Saving writes every parameter.

**Why:** people should be able to read and hand-edit scenarios, so display
units and readable keys. Defaults for missing keys make small, focused
example files possible. Writing every parameter keeps saved files meaningful
if defaults change. The version number lets older files keep working.
Demo mode (M6) will use the same files, each with an extra caption and
camera section.

**Considered:** TOML (nice to edit, but JSON has better tooling everywhere);
Qt's QJsonDocument (would pull Qt into the core and the CLI).

### 16. Sweeps run in parallel on their own thread pool (2026-11-17, M4)

**Decision:** a request (mission + optional sweep) runs on a worker thread;
a sweep's flights are mapped over a dedicated QThreadPool. Results arrive as
one `RunSet` with a colour per flight.

**Why:** a dedicated pool avoids a classic deadlock: a task in the global pool
blocking on more work for the same pool. One `RunSet` per request keeps
"latest wins" simple, and every panel gets a consistent snapshot.

### 17. Flight programs: a mission is a list of steps (2026-11-17, M5)

**Decision:** the hard-coded ascent → coast → circularize sequence became a
data-driven list of steps (`FlightProgram`): burn or coast, a steering mode,
an end condition, and an optional timeout. Flight plans build programs from
mission settings; `simulateProgram()` runs any program.

**Why:** going to the Moon needed more steps (parking orbit, injection,
capture), and the user wants scripting for complex paths. Scripts will build
or steer programs, rather than reaching into the integrator. Behaviour of the
existing plans was kept identical (the tests guard it).

### 18. Sun and Moon from low-precision analytic series (2026-11-17, M5)

**Decision:** the Astronomical Almanac low-precision formulas (via Vallado)
for the Sun (≈ 0.01°) and Moon (≈ 0.3°), with the IERS Earth rotation
angle. UTC is treated as uniform time (±1 min).

**Why:** they're short, readable, dependency-free and accurate enough to fly
to the Moon in this model. JPL's numerical ephemerides (DE440) would need
large data files and a reader. If the simulator grows into a solar-system
model, the planets can come from JPL's published *approximate* Keplerian
elements, still without data files.

### 19. Reference frames and "up" (2026-11-17, M5)

**Decision:** the simulation frame stays **Earth-centred and equatorial**:
z along Earth's spin axis, x towards the vernal equinox. The 3D view has an
orientation gizmo showing these axes and **ecliptic north**.

**Background (the user asked):** the solar system's natural "up" is the
north pole of the ecliptic, the plane of Earth's orbit. (The invariable
plane, the average of all planets' orbits, is within about 1.6° of it.) It is
tilted 23.4° from Earth's spin axis, which causes the seasons. The galaxy's
plane is tilted about 60° to the ecliptic, so the Milky Way's "up" is a very
different direction, and irrelevant for spaceflight within the solar system.

**Later:** for a solar-system model, the natural frame is heliocentric and
ecliptic (J2000), and the scene's "up" should become ecliptic north. Earth-
and Moon-centred views would remain as rotated frames for local work.

### 20. Sunlight direction is set as an explicit rotation (2026-11-17, M5)

**Decision:** the Sun's light gets its orientation from a quaternion
computed in C++ (turning the light's −z onto the sunlight's direction), not
from QML's `lookAt()`.

**Why:** with `lookAt()` the light never turned (checked with screenshots);
the explicit rotation is unambiguous. Lighting is verified against the
subsolar point (21.3°N 21.9°W at Apollo 11's launch).

**Night side (later):** a second, dim, cool-blue directional light shines
from the opposite direction to the Sun (brightness 0.14 against the Sun's
1.6), plus a very small ambient term. It lights only the night hemisphere,
so continents and craters stay faintly visible there while the day side
and the sharpness of the day/night line are unchanged. (Raising the ambient
alone would have washed out the day side too.)

### 21. Demo tours are data, built from scenarios (2026-11-17, M6)

**Decision:** a tour is a versioned JSON file (`orbitsim-demo`) listing steps.
Each step names an example scenario and adds a title, a caption, a camera
setup (view, centre, distance), a playback speed and start time, and how
long to stay. The app runs it with a small controller and a caption overlay
in the 3D view.

**Why:** the tour reuses the scenario files, so improving an example improves
the tour, and adding a step needs no code. A test keeps tours and scenarios
in step.

### 22. Moon texture: CGI Moon Kit colour map, 8192 × 4096 (2026-11-17)

**Decision:** the Moon uses NASA SVS's CGI Moon Kit colour map
(`lroc_color_16bit_srgb_8k.tif`, from Lunar Reconnaissance Orbiter WAC
imagery; public domain), converted to an 8-bit 8192 × 4096 JPEG (7 MB) by
`tools/make_moon_texture.sh`. The Moon is turned so its near side faces the
Earth and its north is near ecliptic north (libration ignored).

**How it got here:** svs.gsfc.nasa.gov's certificate chains to Let's
Encrypt's new "ISRG Root YR", which this system doesn't trust yet. Rather
than disable certificate checks, the user downloaded the file in a browser.
The texture stays optional in the build (grey Moon without it).

### 23. Atmosphere: 1976 US Standard below 86 km, exponential table above (2026-11-17)

**Decision:** the air's temperature, pressure and density come from the 1976
US Standard Atmosphere's seven layers up to 86 km, then from the exponential
model's densities (Vallado, table 8-4) interpolated exponentially up to
1000 km, starting from the standard's own 86 km density so nothing jumps.
Nothing above 1000 km. The air turns with the Earth. Drag is
`½ρv²·Cd·A` against the velocity through the air, with the subsonic Cd
scaled by a fixed, representative Mach curve (×1.8 just past Mach 1).

**Why:** the standard atmosphere is *the* textbook reference, gives the
temperature (so the speed of sound, so the Mach number) as well as density,
and is short enough to read in one sitting. Above 86 km a "standard" means
little because the Sun heats the thermosphere by hundreds of kelvin; the
exponential table is what textbooks use for orbit decay. A Mach-dependent
Cd makes max-Q and the sound barrier visible, which a constant Cd hides; a
fixed curve keeps it to one number the user can understand. Continuity
everywhere matters for the adaptive integrator.

**Considered:** a single exponential (ρ₀·e^(−h/8.5 km): wrong by a factor of
ten above 100 km); NRLMSISE-00 or JB2008 (need solar-flux and geomagnetic
input data, far more code, more accuracy than a teaching tool needs); a
constant Cd.

**Not yet:** engine thrust doesn't change with air pressure. A real engine
gives less thrust at sea level, where the air pushes back on the exhaust,
than in a vacuum (Isp at sea level vs vacuum).

### 24. Scenario format 2: old files keep their old defaults (2026-11-17)

**Decision:** adding drag bumped the scenario format to version 2. Keys left
out of a version 1 file take *version 1's* defaults (no drag, 25° kick),
not today's. The example scenarios moved to version 2 and were re-checked
with drag (four needed new settings or captions); the pure gravity turn
example stays in a vacuum on purpose, since with air no kick reaches orbit.
The default kick changed from 25° to 20°.

**Why:** "never break existing files" has to mean they fly the same, not
just that they load: hand-written files rely on defaults. With drag, 25°
sits next to a cliff (27° turns over into the ground) and flies a flat,
draggy ascent (max-Q 76 kPa, the ascent burn ending at 65 km at 7 km/s);
20° has margin either side, a 63 kPa max-Q and 127 m/s of drag loss.

**How to apply:** whenever a default changes, add it to
`defaultsForVersion()` in `scenario.cpp` along with the format bump.

### 25. Staging: up to three stages, fixed slots, payload on top (2026-11-17)

**Decision:** a `Rocket` holds three stage slots (`std::array<Stage, 3>`)
and a `stageCount`, plus a payload. Stage 1 keeps the old parameter keys
(`dry-mass`, `thrust`, ...); stages 2 and 3 get `stage-2-...` keys. When a
stage runs dry during a burn it is dropped and the next fires at once, with
no coast between. Dropped stages are flown on their own (gravity and drag)
and shown in grey. The scenario format did not change: old files have no
`stages` key and so fly one stage, exactly as before.

**Why:** fixed slots fit the flat parameter table, so every stage number gets
a slider, a CLI option and a scenario key, and can be swept, with no new
UI. Unused slots keep their values, so going from three stages to two and
back loses nothing. Three stages covers nearly every real launcher. Keeping
stage 1 on the old keys keeps old files meaning the same thing.

**Considered:** a variable-length stage list with its own editor (more
general, but not sweepable, and a lot of UI); a coast between separation and
ignition (real, a few seconds, but another parameter for little insight).

**Defaults:** one stage. The upper-stage slots hold a small second and third
stage, light enough that raising the count still flies with the default
20° kick (a heavier stack lifts off more slowly and over-rotates with the
same kick). The examples use a better-matched design: the default rocket
cut in two.

### 26. Circularization compensates for gravity (2026-11-17)

**Decision:** while circularizing, the engine aims at
`Δv + (g − v_h²/r) · t_go · up` instead of along Δv alone (guidance.hpp).

**Why:** staging brought weak upper stages (thrust about equal to weight).
Steering along Δv only corrects for sinking after it happens; a three-minute
burn sank from 200 to 150 km, and payload sweeps jumped about erratically.
Adding the gravity the burn will soak up keeps the altitude, as real upper
stages do by pitching up. For the existing short, hard burns it changes
results by under a kilometre (the default orbit now ends at 200.0 km
instead of 198.8), so old scenario files fly essentially as before.

### 27. The maneuver editor: a Custom plan with its own steps (2026-11-17)

**Decision:** `FlightPlan::Custom` flies `Mission::program`, a list of
`FlightStep`s the user edits in the Flight program panel. For the built-in
plans the panel shows their steps read-only; *Customize* copies them into a
custom program. Scenario files (format 3) store the steps with readable keys
(`"until": "periapsis-altitude"`) and values in display units (km, degrees,
s). A custom step's value is a sweep target, `step-N-value`, alongside the
mission parameters (`sweepTargets()`); sweeping a coast's length is how any
burn's timing is swept. New steering (normal, anti-normal, radial in/out)
and end conditions (periapsis altitude, altitude, at periapsis); "reaches"
conditions work in both directions.

**Why:** flight programs were designed for this from the start (decision
17); the editor is a thin view over them, and it doubles as a way to see
what each plan does. Making step values sweep targets, rather than adding a
parameter per kind of burn, makes "sweep the burn timing" work for any burn.
Display units in files keep them hand-editable, like the mission section.

**Found on the way:** a step lasting 0 s made the integrator take a
zero-length step; its "next step size" was then 0 too, and the flight
stalled until the step limit. Zero-length steps are now skipped, and a step
already at its time limit ends without integrating.

### 28. Closed-loop ascent: apoapsis hold, paced by time to go (2026-11-17)

**Decision:** a new plan, *Orbit (guided ascent)*: the pitch program to
30 km, then `Steering::OrbitInsertion`, which picks the most level pitch
(−30° to +80°) that moves the apoapsis towards the target at the rate
`(target − apoapsis) / t_go`, with t_go from the rocket equation (clamped to
15–120 s). The burn ends when the periapsis reaches the target (circular),
or when the apoapsis can't be held (then the plan coasts up and
circularizes). The existing plans are unchanged.

**Why:** the open-loop plan missed the target badly whenever the rocket
wasn't the one the kick was tuned for (heavy payloads, weak upper stages).
Apoapsis hold never asks the engine for more than it has, and fails
gracefully: short of propellant it still leaves the best orbit it could.

**What didn't work first:** (1) a linear-acceleration law aiming to reach
the target altitude at burnout ("E-guidance"-like): with the default
rocket's short, hard burn it demanded a near-vertical climb and ran dry at
5.6 km/s. (2) Apoapsis hold at a fixed 20 s pace: lofted the strong rocket;
80 s: flew the strong rocket too flat at bigger kicks. Pacing by t_go
suits both. (3) Predicting the apoapsis by stepping the position forward
without gravity: overestimated every candidate and left weak stages 15 km
short. Apoapsis is conserved while coasting, so the prediction is now an
instantaneous push at the current point.

### 29. Engines lose thrust in thick air: a sea-level loss per stage (2026-11-17)

**Decision:** each stage has a *sea-level loss* L, the fraction of thrust
lost at sea level; thrust is `F_vac·(1 − L·p/p0)` at outside pressure p,
with a fixed mass flow. *Thrust* and *Isp* are now the vacuum values. The
default first stage is 1.36 MN with a 12 % loss: 1.2 MN on the pad, as
before. Scenario format 4; older files get no loss and the old 1.2 MN, so
they fly as before (all the existing examples). Results add the
back-pressure loss, and *Δv used* is the vacuum rocket equation less it.

**Why:** `F = F_vac − p·A_exit` is the real physics, and one number per
engine is enough to describe it. A loss fraction rather than a sea-level
Isp keeps the parameters independent: raising the vacuum Isp (to 450 s for
the Moon, say) would otherwise have left the sea-level Isp behind and
quietly cut liftoff thrust by a third, which first crashed half the lunar
tests.

**Side effects, all physical:** the engine now strengthens as it climbs,
so the default ascent is steeper (cutoff at 68 km instead of 79) and
max-Q higher (70 kPa). Steeper climbs let the Earth's rotation turn the
orbit's plane a little more (due north from Florida now gives 83°, not
85°), and Aim at the Moon gives 71.0° for the default rocket.

### 30. A coast between stages (2026-11-17)

**Decision:** `Rocket::separationCoast` (default 5 s): after a separation
the burn step carries on, but the next engine lights only when the coast
is over (an ignition event marks it). Scenario format 5; older files get
0 s and fly as before.

**Why:** real stages coast a few seconds apart before the next engine
lights. A single number for the rocket is enough for that; a per-stage
value would add three sliders for very little.

**Learned on the way:** the expectation that the coast must cost
propellant was wrong for our plans: the open-loop plan coasts up to its
apoapsis anyway, and a 10 s pause left 6 kg *more*. The test now says so.

### 31. Distribution: CI on three platforms, packages, an ebuild (2026-11-18)

**Decision:** GitHub Actions (`.github/workflows/ci.yml`) builds and tests
with warnings as errors on Linux (GCC, Qt 6.8 and 6.11), Windows (MSVC) and
macOS (Apple Clang, universal arm64 + x86_64), then packages the Qt 6.11
builds: an AppImage (linuxdeploy, built on Ubuntu 22.04), a ZIP with the
MSVC runtime, and a disk image. Each package is started under its own
platform and screenshotted (`tools/smoke_test.sh`). A `v*` tag drafts a
release; a person publishes it. The live Gentoo ebuild lives in
`dist/gentoo/` and uses only system dependencies.

**Why:** the rules promised MSVC, Apple Clang and Qt 6.8, but nothing
checked them; every feature made the eventual fix bigger. Packages are
only worth handing out if they start, and a missing Qt plugin or QML
module only shows when the app runs, hence the smoke test. The AppImage is
built on the oldest available Ubuntu because an AppImage runs only where
glibc is at least as new as where it was built. Releases are drafts
because publishing is public and hard to take back.

**Not yet:** native Wayland in the AppImage. It runs through XWayland,
which every Wayland desktop has. linuxdeploy's Qt plugin bundles Qt's
Wayland platform plugin but not the EGL client integration that OpenGL,
and so the 3D view, needs there; bundling half of it would risk a blank
view instead of a working one.

**Not yet:** code signing. Unsigned apps work, but Windows SmartScreen and
macOS Gatekeeper make the first start awkward (README, *Installing*).
Signing needs paid certificates (an Apple Developer account; a Windows
code-signing certificate), so it is the user's call.

### 32. App id and icon (2026-11-18)

**Decision:** the app id is `io.github.pvint.OrbitSimulator` (`ORBIT_APP_ID`
in CMakeLists.txt): it names the .desktop file, the AppStream metadata, the
installed icons and the macOS bundle. The icon (`src/app/assets/icons/`) is
an eccentric orbit, burn in orange then coast in blue, with the Earth at
one focus, on the app's dark blue.

**Why:** freedesktop.org and Flathub want reverse-DNS ids, and a GitHub
account's is `io.github.<account>`. The first icon draft centred the orbit
on the Earth and read as Saturn's rings; the Earth at a focus is what makes
it an orbit. The settings keep their old organisation and app names, so
saved layouts survive.

### 33. Natural bodies: placed at an epoch, flown both ways (2026-11-24)

**Decision:** a mission's *vehicle* is a rocket or a natural body
(`natural_body.hpp`). A body is placed at an epoch either by orbital
elements around the Earth at closest approach (q, e, i, Ω, ω, with the
epoch as the time of periapsis) or by a geocentric state vector. Both are
in the J2000 equatorial frame (ICRF) and are precessed (IAU 1976) to the
simulation's frame of date. The body coasts under the Earth, Moon and Sun
(`gravity.cpp`, shared with the rocket) in its own propagator
(`natural_body_flight.cpp`), integrated *backwards* from the epoch over the
span before it, then forwards. It stops on impact, beyond 10 million km
(`SimOptions::bodyDistanceLimit`) or at the end of the span. Scenario
format 6 adds `vehicle`, `body-given` and `epoch-utc`, and a file holds
only its vehicle's parameters.

**Why:**
- *q rather than a*: the semi-major axis is infinite for a parabola and
  negative for a hyperbola; q describes every conic and is how comets are
  catalogued (with the time of periapsis), so the heliocentric bodies of
  the solar-system milestone can be given the same way.
- *Both ways from the epoch*: what is known about a real body is its state
  at one moment (JPL gives elements or vectors at an epoch). Integrating
  only forwards from the start of the span would need that state carried
  back first, and the given values would no longer hold exactly where they
  were given.
- *J2000 input*: every catalogue (JPL Horizons, the MPC) uses it, so values
  can be pasted in as they are. Without precession Apophis would be 0.36°
  off by 2029.
- *Its own propagator*: the rocket's simulator is a flight-program
  machine (steps, staging, guidance); a body only coasts. Both return a
  `SimResult`, so the app's views, graphs and sweeps work unchanged.
- *No air*: a body that reaches the ground simply stops there. Real small
  bodies break up in the air (Chelyabinsk at 30 km); modelling ablation and
  breakup is a project of its own, and the example says so.

**Checked against JPL Horizons:** Apophis's 2029 flyby stays within 20 km
of JPL's positions two days either side. 2024 PT5's temporary capture
(energy relative to the Earth below zero, at 3.5 to 4 million km) starts and
ends within a day of JPL's dates over a 210-day span, though out there the
Sun's pull is stronger than the Earth's.

**Known approximation, found while checking:** the frame is the equator
and equinox of date, but the Earth turns by the IERS rotation angle, which
is measured from the CIO, not the equinox. The two differ by the equation
of the origins, about 0.36° by 2029 (−0.43° in 1969), so where things are
over the ground is that much off in longitude relative to the Sun, Moon and
bodies. Turning the Earth by sidereal time (GMST) instead would fix it, but
it moves every launch site in space by that much and changes every rocket
result a little; left for a deliberate change with its own tests.

### 34. The solar system: planets from JPL's approximate elements (2026-11-24)

**Decision:** a natural body can go round the Sun instead of the Earth
(`BodyCentre::Sun`). It is then placed and flown in the heliocentric
ecliptic J2000 frame (`solar_system.hpp`), pulled by the Sun, the eight
planets and the Moon, with the planets from Standish & Williams'
*approximate positions of the planets*: table 1 from 1800 to 2050, tables
2a/2b outside it. The Earth sits off the Earth–Moon barycentre opposite the
Moon (from the existing lunar series). Results carry their frame
(`SimResult::frame`), and the app shows them in a second 3D scene,
`SolarScene.qml`, in millions of km with ecliptic north up. One playback
bar drives whichever scene the result needs.

**Why:**
- *No data files* (decision 18): the tables are 100 numbers, accurate to
  20" for the inner planets and 6,000 km for the Earth between 1800 and
  2050. That is far better than the drawing needs. It is also good enough
  that real comets and asteroids come out right: Halley passes the Earth at
  0.4173 au in 1986 (JPL: 0.4172), and Apophis's orbit changes from
  a = 0.9224 to 1.1030 au in 2029, as JPL's does to the fourth decimal.
- *Ecliptic J2000*: comet and asteroid catalogues give elements in it, so
  they can be typed in as published.
- *Switching the centre keeps a position and velocity*, which is exact.
  Elements around the new centre would put the epoch at a periapsis found
  from a two-body orbit, which is badly wrong next to a planet. Apophis's
  flyby, converted that way, came out at 30,700 km instead of 38,011.
- *A second scene*, rather than one scene that switches scale: the Earth's
  scene works in km with a textured globe and a ground track; the solar
  one in millions of km with markers for the Sun and planets (to scale
  they would be invisible). The camera controls, the axis gizmo and the
  demo caption are shared QML components.
- *A one-day cap on the step size* around the Sun, so a step can never jump
  clean over a close pass by a planet. Halley's 76 years take a second.

**Found and fixed on the way:** `calendarFromJulianDate` switched to the
Julian calendar before 1582 while `julianDate` stayed Gregorian, so dates
before 1582 did not survive a round trip. Both are Gregorian now
(proleptic, as in ISO 8601 and Qt). JPL Horizons uses the Julian calendar
before 1582, so dates copied from it are some days earlier here.

**Known approximation:** the tables want TDB and get UTC, about 69 s apart
now and half an hour in 1066. In half an hour Mercury moves 270", which
the test allows for. A ΔT model could fix it if old dates come to matter.

**Not yet:** an Earth-centred close-up of a Sun-centred body's pass by
the Earth (switch the body's centre to the Earth to see one); rockets
flying between planets.

### 35. Regroup: simple first, and a game, "Lost in space" (2026-11-24)

**Context:** after N2 the user opened the app and was overwhelmed: about
25 sliders, a sweep table and a flight-program grid before doing anything.
They asked for something much easier, with advanced settings still there
but out of the way, and more like a game than a simulator while keeping
the physics real. Their example: drop a ship on a random orbit in the
solar system and find your way back to the Earth on the fuel you have.

**Decision:**
- A game of its own, *Lost in space* (Game ▸ Lost in Space, or
  `orbitsim --game`), in a separate window: the solar system, a fuel
  gauge, the goal, the date, and a burn planner. Nothing else.
- **You plan burns rather than fly by hand** (the user's choice): pick
  when, and how hard prograde / outward / north, and the predicted path
  updates as you go; then fly, and burns fire as the clock passes them.
  Months-long trips suit planning better than piloting.
- **Burns are instant** (impulsive). Burns last minutes and the trip lasts
  months, so this is how transfers are first designed; it is said so in
  the code and physics.md. Finite burns can come later.
- **The goal grows with the difficulty** (the user's choice): Easy, reach
  the Earth's sphere of influence; Normal, hit the re-entry corridor
  (−5° to −8°, under 13 km/s at 120 km); Hard, brake into orbit.
- **Every puzzle is solvable:** the game solves it first (a porkchop
  search with Lambert's problem, Sun only) and gives that cost × a
  margin (1.6 / 1.4 / 1.25) plus a reserve for corrections. A test plays
  puzzles of each difficulty with the hints in the full simulation and
  checks each reaches its goal on its own fuel.
- **Hints** do the next sensible step: the cheapest way home from now; a
  course correction aimed on the B-plane with the full simulation; on
  Hard, the braking burn at perigee. Hints used are counted at the end.
- Puzzles start today, and a seed gives the same puzzle on every platform
  (the random numbers are made from the engine's raw output, not
  std::uniform_real_distribution, which differs between libraries).

**Next (to talk over with the user):** a start screen in the main app
that offers the game, launching a rocket, the tour, and the workbench
(today's panels); dragging burn handles in 3D; a "go to a point on a
path" for the game's path.

---

## Open questions

- **Scripting for complex paths.** The user wants scripting eventually.
  Flight programs (decision 17) are the target: scripts will build or steer
  them rather than reaching into the integrator. Candidate languages: Lua (small, embeddable, common in
  simulators) or Qt's QJSEngine (JavaScript, no new dependency). Either way
  the scripting layer lives outside `src/core`, which stays Qt-free.

- **Importing a body from JPL by name** (decisions 33 and 34 cover typing
  one in). It needs network access, and a decision on how the app talks to
  JPL's API.
- **Code signing** for Windows and macOS packages (decision 31): needs
  paid certificates, so it is the user's decision.
