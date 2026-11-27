# Orbit Simulator

A cross-platform desktop app for building intuition about orbital mechanics. A
rocket launches from anywhere on a rotating 3D Earth. You see its trajectory,
orbit and graphs, and you can sweep one parameter to compare a family of
trajectories side by side. Later it flies to the Moon.

It started as a 2D browser prototype (`prototype/web/`, see its README). That
prototype settled the physics model and the UI ideas; this is the full version.

## Where we are (end of the first stretch, 2026-11-17)

- Milestones M0–M6 are done and merged to `main`, and the Moon texture is in.
- **Atmosphere + drag is done** and merged to `main`: 1976 US Standard
  Atmosphere + exponential table, Mach-dependent drag, max-Q and drag loss
  in the stats, a dynamic pressure graph, scenario format 2.
- **Staging is done**: up to three stages plus payload, dropped stages
  flown and drawn, gravity compensation while circularizing.
- **Second stretch (user's list, in order) is done:** burn timing
  (`tli-delay`), night-side lighting (a mid-task request), the maneuver
  editor, guided ascent, thrust that depends on air pressure, a coast
  between stages. All merged to `main`.
- **Polish (2026-11-18):** graph time units under the axis, camera views
  (edge-on / above the orbit / north, also as demo views), release warning.
- **Distribution (2026-11-18) is done:** public repo
  https://github.com/pvint/OrbitSimulator (remote `origin`). App icon and id
  (`io.github.pvint.OrbitSimulator`), Linux desktop file + AppStream,
  GitHub Actions CI (Linux Qt 6.8 + 6.11, Windows MSVC Qt 6.10, macOS
  universal Qt 6.11) with packages (AppImage, ZIP, DMG) that are started
  and screenshotted on each platform, a live Gentoo ebuild (`dist/gentoo/`).
  No release tagged yet: pushing `v0.1.0` drafts one. Work on a branch and
  open a PR so CI runs before merging.
- **Natural bodies, near the Earth (N1, 2026-11-24)** is done and merged
  to `main` (PR #2): an asteroid/comet/moon as the vehicle, placed by
  elements at closest approach (q, e, i, Ω, ω; any conic) or a state vector
  at an epoch (J2000, precessed), flown backwards and forwards from the
  epoch. Scenario format 6. Examples (Apophis 2029, its aim sweep, the 2024
  PT5 mini-moon, Chelyabinsk) are checked against JPL Horizons by tests.
- **Solar system (N2, 2026-11-24)** on branch `solar-system`: natural
  bodies around the Sun (heliocentric ecliptic J2000, planets from JPL's
  approximate elements, every planet and the Moon pulling), a second 3D
  scene for the solar system, scenario format 7. Examples (Halley,
  ʻOumuamua, Ceres, Apophis's orbit change, Kepler's third law) checked
  against JPL by tests. Decision 34.
- **Regroup (2026-11-24, decision 35):** the user found the app
  overwhelming. Priority now: simple first, advanced out of the way, more
  game than simulator, physics still real. Check in after each playable
  piece instead of running down the roadmap.
- **Lost in space (G1, branch `lost-in-space`):** the first game: a ship
  on a random orbit around the Sun gets home on its fuel (Game ▸ Lost in
  Space, `orbitsim --game`). Plan instant burns, fly, hints. Easy / Normal
  / Hard = reach the Earth / re-entry corridor / Earth orbit.
- **Lost in space grew (decisions 36, 37, 2026-11-25):** start anywhere
  (between the planets, near or in orbit around any planet, the asteroid
  belt; around the Earth always in orbit) and go home or to any planet
  (reach / close pass / orbit). Flyby tools for gravity assists ("Aim at
  a planet…", flyby markers), "Don't show this again" cards, arrow keys
  step time. `--puzzle normal:earth:saturn`, `hard:mars:orbit`.
- **One app (G2, decision 38, branch `one-app`, not merged or pushed):**
  the game window is the app. The old workbench UI (panels, sweeps,
  plots, globe view, demo tour) was deleted; it's in git history (before
  commit df88df1). The core still flies rockets of one to three stages
  from any site through the air to orbit or the Moon, and follows natural
  bodies, from `orbitsim-cli` and the tests; the launch is to come back
  as a mode in the game later (the user: "keep this in mind").
- **Done on `one-app` since (2026-11-25/26):** the asteroid belt drawn
  (G6); hints from a set orbit price departures from its best point;
  each planet's sphere of influence drawn (ring + faint sphere, brighter
  for the destination, size on its card); the wheel zooms towards the
  pointer (down to 2,000 km); double-click centres on a planet, the Sun,
  the ship or a flyby; "Centre" lists Sun / You / Earth / destination /
  any planet double-clicked.
- **G3, moons as bodies, is done** (on `one-app`): `Planet` renamed `Body`,
  18 major moons (real sizes, masses, orbits; phases illustrative), their
  spheres of influence, parking orbits (Titan's above its haze),
  `canPark`; moons in the flight (they pull inside their planet's sphere,
  bend paths, can be hit or orbited); moons drawn (orbits, spheres,
  labels, hover cards); **moons as destinations** (decision 39,
  2026-11-26): `moon_transfer.hpp` (`moonApproachSpeed`,
  `cheapestMoonTransfer`, `planToMoon`: the last leg, homed in with the
  full simulation), puzzles to any moon, hints in two steps (the planet,
  then a correction a month out), a planet's own moon starts in orbit
  around it, `Voyage::horizon` (weeks, among moons), the path drawn as
  seen from the planet on such trips, "Or one of its moons:" on the start
  card, `--puzzle normal:earth:moon`. **Moons as starts** (decision 40,
  2026-11-27): in orbit around any moon, going anywhere (nested escapes,
  `escapeFromMoon`; `planFromMoon` homes the departure in on the planned
  v∞, then a correction ten days on), moon to moon, home from the Moon (a
  re-entry, aimed on the perigee); "Or in orbit around one of its moons:"
  on the start card; `--puzzle normal:moon:earth`, `hard:ganymede:europa`.
- **Then, in order:** G4 free roam (pick a body, start in orbit, no goal,
  unlimited fuel); G5 close-ups (textures already found: Mercury, Mars,
  Titan, Io, Europa, Ganymede, Callisto from the USGS map server
  (planetarymaps.usgs.gov WMS, simple cylindrical); Jupiter PIA07782
  (assets.science.nasa.gov); Saturn, Neptune, Venus and Saturn's and
  Uranus's moons from github.com/nasa/NASA-3D-Resources; Uranus, Venus's
  clouds and Saturn's rings to be painted from real data; record each in
  tools/make_planet_textures.sh; close-ups need a floating origin, as
  floats at 1 au resolve only ~10 km); G7 landing where there's air
  (Titan: Huygens came in at ~6 km/s). The user also wants famous
  missions later, and asked whether landing on Titan after leaving the
  Earth is too hard (no: it's an entry, like the Earth's).
- **Open questions:** see the end of `docs/decisions.md` (scripting
  language, importing a body from JPL by name, code signing). Decision 33
  also notes a known 0.36° approximation: the Earth turns by the IERS
  rotation angle in an equinox-of-date frame (GMST would fix it).

## Hard rules

- **`src/core` never includes Qt.** Plain C++20, the standard library, and
  nlohmann/json (only inside `scenario.cpp`).
  The CLI, the tests and the GUI are all clients of the core.
- **Physics uses `double` and SI units** (m, kg, s; angles in radians inside
  the core, degrees only at the UI boundary). Qt's float types (`QVector3D`)
  are for rendering only, using coordinates relative to the camera.
- **Physics changes come with tests** in `tests/`. The user doesn't write
  tests; Claude writes and maintains them.
- **Zero warnings** with `dev` (GCC) and `dev-clang`, where warnings are
  errors. Also keep clang-tidy clean and run clang-format on changed files.
- **Physics code is commented for a reader learning the subject**: say which
  equation is used and why, as the prototype did.
- **Cross-platform:** no OS-specific APIs outside Qt. The code must build with
  MSVC, Apple Clang and GCC (so C++20, not C++23).

## Commands

```sh
cmake --preset dev            # configure (also: dev-clang, asan, release)
cmake --build --preset dev    # build
ctest --preset dev            # run tests
cmake --workflow --preset dev # all three in one go

build/dev/src/app/orbitsim            # the app (the game)
build/dev/src/app/orbitsim --screenshot out.png   # render the window to a file and quit
build/dev/src/app/orbitsim --puzzle hard:4 --hint --no-cards --screenshot out.png
build/dev/src/app/orbitsim --puzzle normal:earth:saturn   # from low Earth orbit to Saturn
build/dev/src/app/orbitsim --puzzle hard:earth:moon        # from low Earth orbit into lunar orbit
build/dev/src/app/orbitsim --puzzle normal:moon:earth      # home from lunar orbit (a moon first = start there)
build/dev/src/cli/orbitsim-cli --sweep kick-angle:5:45:9     # sweep as a table
build/dev/src/cli/orbitsim-cli --vehicle natural-body        # Apophis 2029 flyby
build/dev/src/cli/orbitsim-cli        # CLI
clang-tidy -p build/dev-clang <files> # lint (needs the dev-clang build)
cmake --build build/release --target package   # archive in build/release/
QMAKE=<official Qt>/bin/qmake tools/make_appimage.sh build/release   # AppImage
tools/install_local.sh                # release build + tests + sudo install into /usr/local
```

## Layout

```
src/core/      physics library (namespace orbit). Public headers in include/orbit/core/
src/cli/       orbitsim-cli
src/app/       Qt app, the game. orbitsim_ui (static lib): GameSession, BurnPlanner,
               TimelineBar, no QML. orbitsim (exe): GameWindow, custom Quick 3D
               geometries, QML scenes in qml/, textures in assets/
tools/         textures and icons (make_*.sh), make_appimage.sh, smoke_test.sh (CI)
scenarios/     example scenario files for orbitsim-cli (each is loaded and flown by a test)
tests/         Catch2 tests for the core; tests/app: Qt Test GUI tests of orbitsim_ui
cmake/         CompilerWarnings.cmake (orbit_set_warnings), Packaging.cmake (CPack)
docs/          decisions.md (why things are the way they are), physics.md (the physics, explained)
dist/          linux/ (.desktop, AppStream metainfo), gentoo/ (live ebuild)
.github/       workflows/ci.yml: build, test, package, smoke-test; v* tags draft a release
prototype/web/ the original JavaScript prototype, kept for reference
```

## Conventions

- Files: `snake_case.hpp` / `.cpp`. Types `PascalCase`, functions and
  variables `camelCase`, constants `kPascalCase`, Qt class members `m_name`.
- One class or concept per file. Core headers are included as
  `"orbit/core/<name>.hpp"`.
- Include order (clang-format enforces it): our own headers, then Qt, then std.
- Every new target calls `orbit_set_warnings(<target>)`.
- Mission parameters are defined once, in `orbit/core/mission_parameters.hpp`.
  The CLI options are generated from it.
- Widgets that tests need to find get an `objectName` (e.g. `button-hint`).
  New game behaviour gets a Qt Test in `tests/app/test_game.cpp` (runs offscreen).
- Simulations never run on the GUI thread: go through `GameSession`.
- Visual checks: use `orbitsim --screenshot FILE` (captures only the app's own
  window). Never grab the desktop or "active window" with screen tools: that
  can capture the user's other applications.
- Scenario files: JSON, keys and display units from the parameter table. If the
  format changes, bump `kScenarioFormatVersion` and upgrade older files in
  `scenarioFromJson()`; never break existing files. New example scenarios go
  in `scenarios/` with a name and description, and must load without warnings
  (a test checks).
- Texts on cards are for a curious newcomer: plain words, what to look at.
- 3D scene: 1 unit = 1 million km, heliocentric ecliptic, scene y = ecliptic
  north; always convert with `scene::fromHeliocentric()`
  (src/app/scene_coordinates.hpp).
- Git: one branch per milestone (`m1-physics`, ...), small focused commits,
  merged to `main` when the milestone runs. Commit messages: a short summary
  line, then why.
- Big or hard-to-reverse decisions get an entry in `docs/decisions.md`.
- Licence GPL-3.0-or-later. Every new source file (C++, QML, CMake) starts with
  `// SPDX-License-Identifier: GPL-3.0-or-later` (`#` in CMake).
- Install paths use GNUInstallDirs (`${CMAKE_INSTALL_BINDIR}` etc.), so distro
  packaging works.

## Tech stack

C++20, CMake 3.25+ with presets, Ninja. Qt 6.8+ (developed against 6.11):
- Widgets for the window and the burn planner.
- Qt Quick 3D for the 3D view, a QML scene embedded with `QQuickWidget`.

Tests use Catch2 v3 (installed system package first, FetchContent fallback).
See `docs/decisions.md` for the reasons.

## Roadmap

Each milestone ends with something that runs.

| # | Milestone | Status |
|---|---|---|
| M0 | Skeleton: CMake + presets, core lib, CLI, tests, Qt window with an embedded Quick 3D view, packaging, tooling | done |
| M1 | 3D physics core: port the prototype to 3D state vectors. Launch site lat/lon + azimuth (exact), rotating Earth, inclination. Dormand–Prince 5(4) adaptive integrator with event location. Orbital elements (a, e, i, Ω, ω, ν). Pitch program + auto-circularize. Port and extend the prototype's tests. CLI runs a scenario and prints stats / CSV | done |
| M2 | Qt app: parameter panel, flight stats, Qt Graphs plots, live re-simulation on a worker thread | done |
| M3 | 3D view: textured rotating Earth, trajectory and ground track, camera controls (orbit / zoom / follow), playback | done |
| M4 | Sweeps + ghost paths in 3D, legend, scenario files (save/load, versioned). Design the scenario format so a demo can reuse it (M6) | done |
| M5 | Moon: ephemeris, third-body gravity, Moon-relative orbit stats and sphere of influence, Moon in the 3D view. Generalise the hard-coded ascent → coast → circularize sequence into a **flight program** (a list of steps: burn/coast with a direction and an end condition) and use it for trans-lunar injection. Design it so scripting can drive it later | done |
| M6 | Demo mode: a guided tour that plays scripted scenarios with captions and camera moves, e.g. east vs west launch, a kick-angle sweep, a polar orbit, a trip to the Moon. Each demo step is a scenario file plus caption, camera and timing | done |
| A1 | Atmosphere + drag: 1976 US Standard Atmosphere, Mach-dependent Cd, max-Q, drag loss, dynamic pressure graph, scenario format 2 | done |
| A2 | Staging: up to three stages + payload, separation mid-burn, dropped stages flown and drawn, gravity compensation for weak upper stages | done |
| A4 | Maneuver editor: Custom plan with an editable step list (Flight program panel), new steering and end conditions, step values as sweep targets, scenario format 3 | done |
| A5 | Guided (closed-loop) ascent: apoapsis hold paced by time to go, as the *Orbit (guided ascent)* plan and as a steering mode | done |
| A6 | Thrust depends on air pressure: sea-level loss per stage, vacuum thrust/Isp, back-pressure loss, scenario format 4 | done |
| A7 | Coast between stage separation and ignition (`stage-coast`, default 5 s), scenario format 5 | done |
| Later | Other bodies | |
| A3 | Sweepable burn timing: `tli-delay`, the injection burn's start in seconds (any burn's timing comes with the maneuver editor) | done |
| N1 | Natural bodies near the Earth: an asteroid, comet or moon with no engine as the vehicle, placed by elements at closest approach (q, e, i, Ω, ω) or a state vector at an epoch (J2000), flown both ways from the epoch; impact parameter and capture; scenario format 6; real examples checked against JPL Horizons | done |
| N2 | Solar system: heliocentric ecliptic frame (scene "up" = ecliptic north), planets from JPL's approximate Keplerian elements, natural bodies placed by heliocentric elements (comets, 'Oumuamua, Ceres, Apophis changing class), a solar-system view, scenario format 7 | done |
| Later | Interplanetary rocket transfers and gravity assists; importing a body from JPL by name | |
| Later | Go to a point on a path (user's idea, 2026-11-24): click or right-click a point on any trajectory in 3D (e.g. one flight of a burn-time sweep) for "go there": select that flight, jump playback to that moment, and centre the camera on that point in space | |
| Later | Scripting for complex trajectories: scripts that build or steer flight programs (language open: Lua or Qt's JavaScript engine; see docs/decisions.md) | |
| D1 | Distribution: GitHub + CI builds for Windows/macOS/Linux (AppImage); a Gentoo ebuild (live `-9999` first, then versioned ebuilds once there are tagged releases); .desktop file + app icon | done (no release tagged yet) |
| G1 | Lost in space: the game (puzzles from anywhere to anywhere, hints, flyby tools); decisions 35–37 | done |
| G2 | One app (decision 38): the game window is the app; the old workbench UI is deleted (core physics, CLI and tests kept; the rocket launch to come back later as a mode) | done |
| G3 | Moons as bodies: all 18 major moons (the Moon, the Galilean moons, Saturn's seven, Uranus's five, Triton): ephemerides, gravity, start/destination | done |
| G4 | Free roam: pick a body, start in orbit, no goal, unlimited fuel | |
| G5 | Close-ups: textured bodies at true size, lit by the Sun, Saturn's rings, seen when the camera is near them | |
| G6 | The asteroid belt, drawn | done |
| G7 | Landing where there's air: an entry goal at Titan, Mars, Venus (the Earth's re-entry generalised) | |
| Later | Famous missions to replay and change (Voyager 2, Cassini, New Horizons, Juno, Apollo); landing on airless moons (powered descent); the rocket launch from the ground as a mode | |

### App structure in brief (the game)

`GameWindow` (exe) holds `GameSession` (orbitsim_ui: puzzle, burns, clock,
predictions and hints on worker threads), `BurnPlanner` (the side panel),
`TimelineBar` and `qml/VoyageScene.qml` (the solar system, the path, markers,
spheres of influence, the asteroid belt, cards; `CameraControls.qml`:
drag, pan, zoom towards the pointer, double-click). It fills
`PolylineGeometry`s (lines, or points for the belt) and sets scene
properties on each clock tick. Cards with a key can be hidden for good
(QSettings `game/hiddenCards`). Arrow keys step the clock (GameWindow's
keyPressEvent).

### Core API in brief (Lost in space)

`two_body.hpp`: `propagateKepler`, `solveLambert` (universal variables).
`voyage.hpp`: `Voyage` (start state, `Ship`, `Goal`, `Burn`s in
prograde/radial/normal relative to the Earth inside its SOI, else the Sun,
`lookAhead`), `flyVoyage` → `VoyageResult` (samples, burn reports,
`VoyageEnd`, `closest` Earth approach, entry speed/angle, Δv left,
`closestPasses` per planet, `flybys`), `voyageStateAt`, `passAt`.
`flyby.hpp`: `bPlane`, `impactParameter`, `FlybySide`, `aimFlyby`,
`passAfter`. `moon_transfer.hpp`: `moonApproachSpeed`, `moonDistance`,
`moonOrbitNormal`, `cheapestMoonTransfer` (planet-only porkchop),
`planToMoon` → `MoonLeg` (burn homed in with the full simulation);
from a moon: `escapeFromMoon` (nested escapes), `departureFromMoon`,
`planFromMoon` (`MoonLeg::correction` out of a planet's system).
`refineAim` aims a bound return (from the Moon) on the perigee
(`aimPerigee`).
`lost_in_space.hpp`: `Region`, `makePuzzle(difficulty, seed, epoch, region)`,
`cheapestWayHome` (porkchop), `searchSpan` (window and longest trip by
distance), hints `burnTowardsEarth`, `refineAim` (B-plane Newton with the
full simulation), `captureBurn`, `approachBrake` (shed v∞ before re-entry). App:
`GameSession` (puzzle, burns, clock, background work) and `BurnPlanner`
in orbitsim_ui; `GameWindow` + `qml/VoyageScene.qml` in the exe.

### Core API in brief (natural bodies)

`Mission::vehicle` (`Vehicle::Rocket` / `NaturalBody`) and `Mission::body`
(`NaturalBody`: `given` = Elements / StateVector, q/e/i/Ω/ω or
position/velocity in J2000, `epoch`, `timeBefore`/`timeAfter`).
`simulate()` sends bodies to `simulateNaturalBody()`
(natural_body_flight.cpp). `SimResult::startEpoch` is the epoch at t = 0
(use it, not `mission.launchEpoch`); `SimResult::encounter` (closest
approach, impact parameter, v∞ in/out, time bound, first/last orbit).
`orbit.hpp`: `stateFromPeriapsis`, `timeSincePeriapsis` (Kepler).
`natural_body.hpp`: `setBodyGiven` (converts), `precessionFromJ2000/ToJ2000`,
`apophisFlyby()`. `gravity.hpp`: `gravityAcceleration` (Earth + Moon + Sun).
Parameters carry their `vehicle`; `parameterInUse(p, mission)` filters
panels, sweeps and saved files.

### Core API in brief (solar system)

`solar_system.hpp`: `Body` (planets, then 18 moons), `kAllPlanets`,
`kMoons`, `kAllBodies`, `kBodyCount`, `isMoon`, `planetOf`,
`bodyPosition/Velocity` (heliocentric ecliptic J2000; the Earth, not the
barycentre; a moon = its planet + `moonOrbit`), `planetOrbit` (elements;
a moon's planet's), `bodyMu/Radius/Name`, `sphereOfInfluence` (a moon's
against its planet), `earthAndMoonHeliocentric`, `eclipticToEquatorial`.
`asteroid_belt.hpp`: `makeAsteroidBelt`, `asteroidPosition`,
`kirkwoodGaps`. `orbit.hpp`: `eccentricAnomaly` (Kepler).
`NaturalBody::centre` (`BodyCentre::Earth`/`Sun`), `setBodyCentre`
(converts, leaves a state vector), `centreMu`. `gravity.hpp`:
`heliocentricGravity`. `SimResult::frame` (`ResultFrame`), `SimResult::solar`
(`SolarSummary`: perihelion, first/last orbit, approaches per planet,
`impacted`).

### Core API in brief (maneuver editor)

`FlightPlan::Custom` flies `Mission::program`. `flight_program.hpp` has key/label
functions for plans, `Steering` and `StepEnd` (`kAll...` arrays), and
`stepValueToDisplay/FromDisplay` (km, °, rev, s). `sweep.hpp`: `SweepTarget`,
`sweepTargets(mission)` (parameters + `step-N-value` for custom programs),
`findSweepTarget`. CLI: `--show-program`, and impact points.

### Core API in brief (staging)

`Rocket` = `std::array<Stage, kMaxStages> stages` (bottom first) + `stageCount`
+ `payloadMass` + drag shape. `Stage` = dry mass, propellant, thrust, Isp.
`rocket.hpp`: `massAbove`, `liftoffMass`, `stageDeltaV`, `idealDeltaV`,
`thrustToWeightAtIgnition`, `canFire`. The simulator tracks the firing stage;
`EventType::StageSeparation`; `SimResult::spentStages` (samples, impact time
and distance downrange). Parameters: `stages`, `payload`, stage 1 on the old
keys, `stage-2-*`, `stage-3-*`; `parameterTitle()` gives unique names
("Stage 2 thrust") for lists.

### Core API in brief (atmosphere)

`atmosphereAt(altitude)` → `Air` (T, P, ρ, speed of sound). `aerodynamicsAt(rocket,
r, v)` → `Aerodynamics` (air velocity, Mach, q, Cd, drag force); also
`aerodynamicsAt(result, sample)` for graphs. `Rocket` has `dragCoefficient`
(subsonic, 0 = vacuum) and `diameter`. `SimResult` adds `dragLoss` and
`maxDynamicPressure` (both counted while climbing only). Scenario files at
format 1 get format 1's defaults (`defaultsForVersion()` in scenario.cpp).

### Core API in brief (M5)

`simulate(Mission)` runs `flightProgramFor(mission)` through
`simulateProgram(mission, program, options)`. A `FlightProgram` is a list of
`FlightStep`s (burn/coast, `Steering`, `StepEnd`, value, timeout). Gravity
includes the Moon and Sun (`SimOptions::thirdBodyGravity`). Results add
`LunarSummary` (SOI, closest approach, final Moon-relative orbit).
`lunar_targeting.hpp`: `azimuthToMoon`, `bestLaunchHourForMoon`.
Times are epochs (s since J2000, `astro_time.hpp`); `ephemeris.hpp` gives
Sun and Moon positions.

### Core API in brief (M1)

`simulate(Mission, SimOptions) → SimResult` in `simulation.hpp` is the entry
point. `Mission` = `Rocket` + `LaunchSite` + `AscentGuidance` + auto-circularize
settings; `defaultMission()` gives the tuned defaults. Results hold
evenly spaced `Sample`s (dense output), `FlightEvent`s, the final orbit's
`OrbitalElements`, outcome and summary numbers. `docs/physics.md` explains it all.

### Lessons from the prototype (still apply)

- A pure gravity turn with one burn to depletion almost never reaches a clean
  orbit: it's a knife edge. Auto-circularize (cut off at the target apoapsis,
  coast, burn towards circular velocity) fixes this.
- The gravity turn must follow velocity relative to the *ground*, otherwise
  Earth's 465 m/s rotation tips the rocket over at liftoff.
- Adding propellant at fixed thrust lowers thrust-to-weight, so the same kick
  over-rotates and crashes. Sweeps show this; it is physics, not a bug.
- Defaults that worked: dry 4 t, propellant 60 t, thrust 1360 kN in vacuum with a
  12 % sea-level loss (1200 kN on the pad; 1200 kN and no loss before format 4), Isp 340 s,
  lat 28.5°, azimuth 90°, pitch start 10 s, kick 20° held 10 s, target 200 km,
  Cd 0.3, diameter 2.4 m. (In a vacuum the kick was 25°; with air, 25° flies
  flat through thick air and 27° turns over: see decision 24.)
- A heavier stack (more stages, payload) lifts off more slowly and the same
  kick over-rotates: two- and three-stage rockets want a smaller kick. The
  "default rocket cut in two" (3 t + 48 t, then 1 t + 12 t at 150 kN, all
  340 s) flies well at 18–20° and reaches the Moon (scenarios/two-stages*).
  The default launch time is Apollo 11's (1969-07-16 13:32 UTC); with Isp
  450 s the same rocket reaches lunar orbit (see scenarios/).
