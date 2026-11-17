# Orbit Simulator

A desktop app for building intuition about orbital mechanics. Launch a rocket
from anywhere on a rotating Earth, see where it goes, and sweep a parameter
to compare a whole family of trajectories side by side.

**Status:** early development. The desktop app shows flights on a textured
3D Earth with playback, live parameter sliders, results and graphs, parameter
sweeps that compare a whole family of flights, and scenario files with
built-in examples. `orbitsim-cli` flies missions and sweeps from the command
line. Next: the Moon. See the roadmap in [CLAUDE.md](CLAUDE.md). The
original 2D browser prototype, which already works, is in
[prototype/web](prototype/web/README.md).

## Building

You need:

- a C++20 compiler: GCC 11+, Clang 14+, MSVC 2022 or Xcode 15+
- CMake 3.25+ and Ninja
- Qt 6.8 or newer with **Qt Quick 3D** and **Qt Graphs**
- Catch2 v3 for the tests (optional: CMake downloads it if it isn't installed)

| Platform | Getting the dependencies |
|---|---|
| Linux | Your distribution's packages, e.g. Gentoo `dev-qt/qtquick3d dev-qt/qtgraphs dev-cpp/catch`, Debian/Ubuntu `qt6-quick3d-dev qt6-graphs-dev catch2` |
| macOS | Qt from the [online installer](https://www.qt.io/download-qt-installer) (tick Qt Quick 3D and Qt Graphs), then `brew install cmake ninja` |
| Windows | Qt from the online installer (MSVC 2022 64-bit build, plus Qt Quick 3D and Qt Graphs). Build from a "Developer Command Prompt for VS 2022" |

If CMake can't find Qt, point it there, e.g. `-DCMAKE_PREFIX_PATH=~/Qt/6.11.0/macos`.

Then:

```sh
cmake --workflow --preset dev     # configure, build and run the tests
build/dev/src/app/orbitsim        # start the app

Drag any slider and the flight re-simulates at once. The panels are docks:
move, stack or hide them (View menu), and the layout is remembered.

In the 3D view, drag to turn the camera and use the mouse wheel to zoom. The
playback bar underneath plays the flight (Space), scrubs through it, sets the
speed and can follow the rocket. Burns are drawn orange, coasting blue, and
the ground track (the path over the ground) yellow. The orbit stays fixed in
space while the Earth turns underneath, which is why the ground track drifts
west each orbit.

**Sweeps.** In the Sweep panel pick a parameter, a range and a number of
flights, then *Run sweep*. Every flight appears in its own colour, in 3D and
faintly in the graphs; the legend lists each one's outcome and orbit, and
clicking a row selects it. The swept parameter's slider is locked; *Single
flight* goes back. In single-flight mode, the last few flights stay behind as
grey ghosts so you can see what a slider change did (*Clear ghosts* removes
them).

**Scenarios.** *File ▸ Examples* has ready-made scenarios (equatorial and
polar launches, east vs west, kick-angle and propellant sweeps, a pure
gravity turn...). *Save Scenario* writes the current setup to a JSON file
you can share or edit by hand; `orbitsim file.json` opens one directly. The
examples live in [scenarios/](scenarios/).

`orbitsim --screenshot FILE` saves an image of the window and quits.
```

### Trying the physics from the command line

```sh
build/dev/src/cli/orbitsim-cli                        # default rocket from Cape Canaveral
build/dev/src/cli/orbitsim-cli --latitude 0 --azimuth 270  # launch west from the equator
build/dev/src/cli/orbitsim-cli --sweep kick-angle:5:45:9   # a sweep, as a table
build/dev/src/cli/orbitsim-cli --scenario scenarios/vandenberg-polar.json
build/dev/src/cli/orbitsim-cli --help                 # all options
build/dev/src/cli/orbitsim-cli --csv flight.csv       # every sample, for plotting
```

How the physics works is explained in [docs/physics.md](docs/physics.md).

### Build presets

| Preset | What it is |
|---|---|
| `dev` | Debug build, warnings are errors |
| `dev-clang` | Same with Clang; also used for clang-tidy |
| `asan` | Debug with AddressSanitizer and UndefinedBehaviorSanitizer (Linux/macOS): catches memory bugs at run time |
| `release` | Optimised build |

Each preset builds into `build/<preset>/`. Use them as `cmake --preset X`,
`cmake --build --preset X` and `ctest --preset X`.

### Packaging

```sh
cmake --preset release && cmake --build --preset release
cmake --build build/release --target package
```

This produces a `.zip` on Windows, a `.dmg` on macOS and a `.tar.gz` on Linux
in `build/release/`. On Windows and macOS the Qt runtime is bundled. On Linux
it isn't by default, since a distribution's Qt can't be bundled reliably; see
[docs/decisions.md](docs/decisions.md).

## Project layout

```
src/core/      the physics, in plain C++ (no Qt)
src/cli/       orbitsim-cli: runs the physics without a GUI
src/app/       the Qt desktop app (Widgets, plus QML for the 3D view and plots)
scenarios/     example scenario files
tools/         helper scripts (rebuilding the Earth texture)
tests/         physics tests (Catch2)
docs/          design decisions and physics notes
prototype/web/ the original 2D browser prototype
```

## Credits

Earth imagery: *Blue Marble: Next Generation*, NASA Earth Observatory
(Reto Stöckli), public domain. See `tools/make_earth_texture.sh` for exactly
which file and how it was processed.

## Licence

GPL-3.0-or-later: see [LICENSE](LICENSE). You may use, change and share this
program, but versions you distribute must also be free software under the GPL.

## Development

- Code style: `.clang-format` (most editors apply it on save). Lint:
  `clang-tidy -p build/dev-clang <files>`.
- Editor support: `.clangd` points clangd at `build/dev`, so configure the
  `dev` preset once.
- The reasons behind the bigger choices are in [docs/decisions.md](docs/decisions.md).
