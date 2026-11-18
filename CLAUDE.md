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
- **Second stretch (user's list, in order):** burn timing (`tli-delay`),
  night-side lighting (a mid-task request), the maneuver editor, guided
  ascent, thrust that depends on air pressure: done and merged. Still to
  do: a coast between stage separation and ignition.
  Then STOP: natural bodies are next on the roadmap but the user said not
  yet; ask what's next.
- The app flies a rocket of one to three stages from any site and date,
  through the air, to low orbit or to the Moon, with built-in plans or a
  custom flight program, sweeps (of any parameter or program step),
  ghosts, scenario files, 21 example scenarios, a textured Earth and Moon
  lit by the real Sun, and a 14-step demo tour. 97 test suites pass (GCC,
  Clang, ASan/UBSan); clang-tidy is clean.
- **Open questions:** see the end of `docs/decisions.md` (scripting
  language, how to specify natural bodies, app icon).

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

build/dev/src/app/orbitsim            # GUI
build/dev/src/app/orbitsim --screenshot out.png   # render the window to a file and quit
build/dev/src/app/orbitsim scenarios/kick-angle-sweep.json   # open a scenario
build/dev/src/app/orbitsim --demo-step 8 --screenshot out.png  # check a demo step
build/dev/src/cli/orbitsim-cli --sweep kick-angle:5:45:9     # sweep as a table
build/dev/src/cli/orbitsim-cli        # CLI
clang-tidy -p build/dev-clang <files> # lint (needs the dev-clang build)
cmake --build build/release --target package   # archive in build/release/
```

## Layout

```
src/core/      physics library (namespace orbit). Public headers in include/orbit/core/
src/cli/       orbitsim-cli
src/app/       Qt app. orbitsim_ui (static lib): panels + SimulationController, no QML.
               orbitsim (exe): main window, GlobeView (3D + playback), PlotPanel,
               custom Quick 3D geometries, QML scenes in qml/, textures in assets/
tools/         make_earth_texture.sh: rebuilds the Earth texture from NASA's original
scenarios/     example scenario files (built into the app's File ▸ Examples; each is
               loaded and flown by a test)
demos/         tour.json: the guided demo tour (Help ▸ Demo Tour); a test checks every
               step's scenario exists and loads
tests/         Catch2 tests for the core; tests/app: Qt Test GUI tests of orbitsim_ui
cmake/         CompilerWarnings.cmake (orbit_set_warnings), Packaging.cmake (CPack)
docs/          decisions.md (why things are the way they are), physics.md (the physics, explained)
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
  The GUI sliders and CLI options are generated from it; add new numeric
  parameters there, not in the UI.
- Widgets that tests need to find get an `objectName` (e.g. `spin-kick-angle`).
  New panel behaviour gets a Qt Test in `tests/app/test_ui.cpp` (runs offscreen).
- Simulations never run on the GUI thread: go through `SimulationController`.
- Visual checks: use `orbitsim --screenshot FILE` (captures only the app's own
  window). Never grab the desktop or "active window" with screen tools: that
  can capture the user's other applications.
- Scenario files: JSON, keys and display units from the parameter table. If the
  format changes, bump `kScenarioFormatVersion` and upgrade older files in
  `scenarioFromJson()`; never break existing files. New example scenarios go
  in `scenarios/` with a name and description, and must load without warnings
  (a test checks).
- Demo tours (`demos/`, `orbit/core/demo.hpp`) refer to scenarios by file
  name. Captions are for a curious newcomer: plain words, what to look at.
- Saved window layout is versioned (`kLayoutVersion` in main_window.cpp): bump
  it when panels are added or removed.
- 3D scene: 1 unit = 1 km, axes (x, z, −y) of the physics frame; always convert
  with `scene::fromPhysics()` (src/app/scene_coordinates.hpp).
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
- Widgets for the window, docks and forms.
- Qt Quick 3D for the 3D view, and Qt Graphs for the plots. Both are QML scenes
  embedded with `QQuickWidget`.

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
| Later | Other bodies. A coast between stage separation and ignition | |
| A3 | Sweepable burn timing: `tli-delay`, the injection burn's start in seconds (any burn's timing comes with the maneuver editor) | done |
| Later | Natural bodies as the "vehicle": an asteroid, comet or moon with no engine, placed by orbital elements or position/velocity at an epoch (later perhaps imported from JPL small-body data). How to specify and place it in the UI is still open | |
| Later | Solar system: heliocentric ecliptic frame (scene "up" = ecliptic north), planets from JPL's approximate Keplerian elements, interplanetary transfers and gravity assists | |
| Later | Scripting for complex trajectories: scripts that build or steer flight programs (language open: Lua or Qt's JavaScript engine; see docs/decisions.md) | |
| Later | Distribution: GitHub + CI builds for Windows/macOS/Linux (AppImage); a Gentoo ebuild (live `-9999` first, then versioned ebuilds once there are tagged releases); .desktop file + app icon | |

### App structure in brief (M2–M4)

`MainWindow` owns the state: mission, optional sweep, latest `RunSet`, the
selected flight, the scenario file. `ParameterPanel` / `SweepPanel` report
user changes → `SimulationController::run(mission, sweep)` simulates on worker
threads (latest wins; sweep flights in parallel) → `resultReady(RunSet)` →
`MainWindow::showSelection()` feeds `FlightStatsPanel`, `PlotPanel` (selected
bold, other sweep flights faint), `GlobeView` (ditto in 3D, or fading ghosts
of recent single flights) and the sweep legend. `GlobeView` fills the
trajectory (inertial) and ground track (Earth-fixed) `PolylineGeometry`s in
`qml/GlobeScene.qml` and drives playback (rocket position via `stateAt()`,
Earth rotation angle). `PlotPanel` pushes data into Qt Graphs
series it finds through properties on `qml/Plots.qml`.

### Core API in brief (maneuver editor)

`FlightPlan::Custom` flies `Mission::program`. `flight_program.hpp` has key/label
functions for plans, `Steering` and `StepEnd` (`kAll...` arrays), and
`stepValueToDisplay/FromDisplay` (km, °, rev, s). `sweep.hpp`: `SweepTarget`,
`sweepTargets(mission)` (parameters + `step-N-value` for custom programs),
`findSweepTarget`. App: `ProgramPanel` (orbitsim_ui) in the "Flight program"
dock, tabbed with Graphs. CLI: `--show-program`, and impact points.

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
