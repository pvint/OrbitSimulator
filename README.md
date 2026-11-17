# Orbit Simulator

A desktop app for building intuition about orbital mechanics. Launch a rocket
from anywhere on a rotating Earth, see where it goes, and sweep a parameter
to compare a whole family of trajectories side by side.

**Status:** early development. Milestone M0 (project skeleton) is done; the
3D physics core is next. See the roadmap in [CLAUDE.md](CLAUDE.md). The
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
```

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
tests/         physics tests (Catch2)
docs/          design decisions and physics notes
prototype/web/ the original 2D browser prototype
```

## Development

- Code style: `.clang-format` (most editors apply it on save). Lint:
  `clang-tidy -p build/dev-clang <files>`.
- Editor support: `.clangd` points clangd at `build/dev`, so configure the
  `dev` preset once.
- The reasons behind the bigger choices are in [docs/decisions.md](docs/decisions.md).
