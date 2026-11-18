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

---

## Open questions

- **Scripting for complex paths.** The user wants scripting eventually.
  Flight programs (decision 17) are the target: scripts will build or steer
  them rather than reaching into the integrator. Candidate languages: Lua (small, embeddable, common in
  simulators) or Qt's QJSEngine (JavaScript, no new dependency). Either way
  the scripting layer lives outside `src/core`, which stays Qt-free.

- **Natural bodies as the "vehicle"** (asteroids, comets, ...). The core can
  already start from any state (`SimOptions::initialState`) and coast with no
  engine. Open: how the user specifies one: orbital elements, position and
  velocity, or an imported real object, and how to place it in the UI.
- **App icon and identifier.** The macOS bundle id is a placeholder
  (`io.github.orbitsim`).
