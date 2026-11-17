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

### 5. Integrator: Dormand–Prince 5(4) with event location (planned, M1)

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

---

## Open questions

- **Licence.** MIT, GPL-3.0 or something else? Qt is LGPL, so any of them work.
  Decide before the code goes public.
- **App icon and identifier.** The macOS bundle id is a placeholder
  (`io.github.orbitsim`).
