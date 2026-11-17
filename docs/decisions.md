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

---

## Open questions

- **Scripting for complex paths.** The user wants scripting eventually.
  Plan: in M5 the hard-coded flight sequence becomes a data-driven *flight
  program* (steps such as "burn prograde until apoapsis = X", "coast until
  …"). Scripts then build or steer flight programs rather than reaching
  into the integrator. Candidate languages: Lua (small, embeddable, common in
  simulators) or Qt's QJSEngine (JavaScript, no new dependency). Either way
  the scripting layer lives outside `src/core`, which stays Qt-free.

- **App icon and identifier.** The macOS bundle id is a placeholder
  (`io.github.orbitsim`).
