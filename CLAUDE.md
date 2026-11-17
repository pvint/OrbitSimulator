# Orbit Simulator

A cross-platform desktop app for building intuition about orbital mechanics. A
rocket launches from anywhere on a rotating 3D Earth. You see its trajectory,
orbit and graphs, and you can sweep one parameter to compare a family of
trajectories side by side. Later it flies to the Moon.

It started as a 2D browser prototype (`prototype/web/`, see its README). That
prototype settled the physics model and the UI ideas; this is the full version.

## Hard rules

- **`src/core` never includes Qt.** Plain C++20 and the standard library only.
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
build/dev/src/cli/orbitsim-cli        # CLI
clang-tidy -p build/dev-clang <files> # lint (needs the dev-clang build)
cmake --build build/release --target package   # archive in build/release/
```

## Layout

```
src/core/      physics library (namespace orbit). Public headers in include/orbit/core/
src/cli/       orbitsim-cli
src/app/       Qt app. orbitsim_ui (static lib): panels + SimulationController, no QML.
               orbitsim (exe): main window, PlotPanel, QML scenes in qml/
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
| M3 | 3D view: textured rotating Earth, trajectory and ground track, camera controls (orbit / zoom / follow) | next |
| M4 | Sweeps + ghost paths in 3D, legend, scenario files (save/load, versioned). Design the scenario format so a demo can reuse it (M6) | |
| M5 | Moon: ephemeris, third-body gravity, Moon-relative orbit stats and sphere of influence, Moon in the 3D view. Generalise the hard-coded ascent → coast → circularize sequence into a **flight program** (a list of steps: burn/coast with a direction and an end condition) and use it for trans-lunar injection. Design it so scripting can drive it later | |
| M6 | Demo mode: a guided tour that plays scripted scenarios with captions and camera moves, e.g. east vs west launch, a kick-angle sweep, a polar orbit, a trip to the Moon. Each demo step is a scenario file plus caption, camera and timing | |
| Later | Atmosphere + drag, staging, full maneuver editor, other bodies | |
| Later | Scripting for complex trajectories: scripts that build or steer flight programs (language open: Lua or Qt's JavaScript engine; see docs/decisions.md) | |
| Later | Distribution: GitHub + CI builds for Windows/macOS/Linux (AppImage); a Gentoo ebuild (live `-9999` first, then versioned ebuilds once there are tagged releases); .desktop file + app icon | |

### App structure in brief (M2)

`ParameterPanel` emits `missionChanged` → `SimulationController::setMission`
runs `simulate()` on a worker thread (latest wins) → `resultReady` feeds
`FlightStatsPanel` and `PlotPanel`. `PlotPanel` pushes data into Qt Graphs
series it finds through properties on `qml/Plots.qml`.

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
- Defaults that worked: dry 4 t, propellant 60 t, thrust 1200 kN, Isp 340 s,
  lat 28.5°, azimuth 90°, pitch start 10 s, kick 25° held 10 s, target 200 km.
