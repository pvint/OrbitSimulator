# The physics in Orbit Simulator

A guided tour of `src/core`, in the order a flight uses it. Every formula here
is in the code with a comment pointing back to it.

## Frames: where things are (`earth.hpp`)

The simulation runs in an **Earth-centred inertial frame** (ECI). The origin
is Earth's centre and z points to the north pole. The axes don't turn with
the Earth; they stay fixed relative to the stars. Newton's laws hold in this
frame without extra "fictitious" forces.

The launch site, however, sits on the turning Earth. Its position is given by
latitude and longitude in an **Earth-fixed frame** (ECEF), which shares the z
axis and is turned by the **Earth rotation angle** θ:

    r_eci = Rz(θ) · r_ecef        θ(t) = θ₀ + ω·t,   ω = 7.292×10⁻⁵ rad/s

ω is one turn per *sidereal* day (23 h 56 min), the time Earth takes to turn
once relative to the stars.

Anything resting on the ground moves with it, at velocity `ω × r`. At the
equator that is 465 m/s eastward, falling to zero at the poles as
`ω·R·cos(latitude)`. A rocket on the pad already has this velocity, which is
why eastward launches are cheaper and westward ones dearer.

At any point we also use the **local frame**: east, north and up. "Up" points
away from Earth's centre; east is `z × up` (undefined at the poles, where we
pick a fixed direction).

## Time, and where the Sun and Moon are (`astro_time.hpp`, `ephemeris.hpp`)

Every mission has a **launch time**. Times are counted in seconds since
**J2000.0** (2000-01-01 12:00), the standard astronomical epoch, and
converted to and from calendar dates through the **Julian date** (a
continuous day count; Meeus, *Astronomical Algorithms*, ch. 7).

The **Earth rotation angle** says how far the Earth has turned relative to
the stars at a given moment (IERS definition):

    θ = 2π · (0.7790572732640 + 1.00273781191135448 · days since J2000)

That one number places every launch site in inertial space. It turns
slightly more than once per (solar) day: one turn per *sidereal* day of
23 h 56 min 4 s, because the Earth also moves along its orbit.

The **Sun and Moon** come from the Astronomical Almanac's low-precision
series (via Vallado): mean longitudes plus the largest periodic terms, then
rotated from the ecliptic into the equatorial frame by the **obliquity**
(23.44°, the tilt of Earth's axis). Accuracy: about 0.01° for the Sun and
0.3° (≈ 2000 km) for the Moon. The tests check them against the 2024
equinox, solstice, perihelion and aphelion, and the January 2024 new and
full moons.

## Forces (`simulation.cpp`)

| Force | Formula | Notes |
|---|---|---|
| Earth | `a = −μ r / |r|³` | Point mass, μ = 3.986×10¹⁴ m³/s² |
| Moon, Sun | `a = μ_b ((r_b − r)/|r_b − r|³ − r_b/|r_b|³)` | See below |
| Thrust | `a = F/m` along the steering direction | Only while the engine runs |
| Mass flow | `dm/dt = −F / (Isp·g0)` | g0 = 9.80665 m/s² |

The **third-body** formula has two terms. The first is the body's pull on the
rocket. The second subtracts its pull on the Earth: our frame is centred on
the Earth, which itself falls towards the Moon and Sun, and only the
*difference* moves the rocket relative to the Earth. (It's also what raises
tides.)

No drag and no staging yet.

## The rocket (`rocket.hpp`)

Constant thrust `F` until the propellant runs out. The **exhaust velocity** is
`v_e = Isp·g0`. Isp, the specific impulse, measures engine efficiency in
seconds. The **Tsiolkovsky rocket equation** gives the total velocity change
the propellant can deliver:

    Δv = v_e · ln(m₀ / m_dry)

In flight, part of that Δv is lost to fighting gravity while climbing
("gravity losses"). That's why the simulator shows both *ideal Δv* and *Δv
used*.

**Thrust-to-weight ratio** on the pad, `F / (m₀·g)`, must exceed 1 to lift
off. It also shapes the gravity turn: a low ratio turns more, a high one less.

## Guidance: which way the engine points (`guidance.hpp`)

**Pitch program:**
1. **Vertical**: straight up until *pitch start*.
2. **Kick**: tilt *kick angle* away from vertical, downrange, and hold it for
   *kick hold* seconds.
3. **Gravity turn**: point along the velocity *relative to the ground*.
   Gravity bends the path over and the thrust follows it.

"Downrange" lies in the **launch plane**: the plane through Earth's centre
containing the launch site and the launch direction, fixed in inertial space
at liftoff. A fixed plane keeps "downrange" meaningful everywhere, even from
a pole, where "north" flips as soon as you move.

The gravity turn follows velocity relative to the *ground*, not inertial
velocity. Just after liftoff, the inertial velocity is mostly Earth's
rotation. Following it would tip the rocket over at once.

**Auto-circularize.** A single burn until the tank is empty almost never ends in a
clean orbit. The gravity turn would have to level off at exactly the moment
orbital speed is reached. (The prototype tried 40,000 combinations; see
`docs/decisions.md`.) So by default:

1. Cut the engine when the **apoapsis** of the current trajectory reaches the
   target altitude.
2. Coast up to apoapsis.
3. Burn along `Δv = v_circular − v`, where `v_circular` is speed `√(μ/r)`
   horizontally in the current orbit plane. This is exactly the velocity change
   needed to be in a circular orbit right now. At apoapsis that means "burn
   horizontally", and if the rocket starts to sink, Δv tilts up to correct
   it. The burn ends when Δv reaches zero.

## Flight programs (`flight_program.hpp`)

A flight is a list of **steps**, each burning or coasting, with a steering
mode (pitch program, prograde, retrograde, circularize, retrograde relative to
the Moon) and an end condition (apoapsis reached, apoapsis high enough,
orbit circular, Moon in position, closest approach to the Moon, captured by
the Moon, N revolutions, a duration). The mission's **flight plan** builds
the list:

| Plan | Steps |
|---|---|
| Orbit | Ascent → Coast to apoapsis → Circularization → Coast (1 revolution) |
| Burn until empty | Ascent (until the propellant runs out) → Coast |
| Lunar flyby | as Orbit up to the parking orbit → wait for the Moon → Trans-lunar injection → Coast to the Moon → Coast |
| Lunar orbit | as Lunar flyby, then Lunar orbit insertion (brake) → Coast round the Moon |

Running out of propellant ends any burn and skips later burns. A step whose
condition already holds is skipped, and a step can time out (a transfer that
misses the Moon never reaches "closest approach").

## Going to the Moon (`flight_program.cpp`, `lunar_targeting.hpp`)

**Trans-lunar injection (TLI)** is a prograde burn in the parking orbit that
raises the apoapsis a little beyond the Moon. The rocket then travels a
transfer ellipse for about three and a half days.

**When to burn.** Kepler's equation gives how far round the ellipse the
rocket travels before reaching the Moon's distance (ν, about 170°) and how
long that takes. The burn starts when the point where the Moon *will be* at
that time is ν ahead of the rocket. The classic **lead angle** (how far
ahead the Moon is at the moment of the burn) works out at about 110–130°.

**Which way to launch.** The burn doesn't change the orbit's plane, so the
Moon must be in that plane when the rocket arrives. The plane is fixed when
the rocket leaves the pad, by the launch site's position at that moment
and the launch direction. *Aim at the Moon* picks the azimuth whose plane
contains the Moon's arrival position, refined with quick ascent simulations
because Earth's rotation tilts the real plane slightly. For Apollo 11's
launch time it gives 72.5°; Apollo flew 72.0°. Because the Earth turns,
the plane swings round during the day: only at certain times can the rocket
fly east (the cheapest direction) and still reach the Moon. That is a
**launch window** (*Best launch time*).

**Near the Moon.** Within about 66,000 km of it (the **sphere of
influence**, `a·(m_moon/m_earth)^(2/5)`), the Moon's gravity dominates the
shape of the path. **Lunar orbit insertion** brakes against the velocity
*relative to the Moon* until the craft is bound to it. A flyby on the right
side can instead take energy from the Moon's motion: a **gravity assist**,
which can fling the rocket out of the Earth's grip altogether.

## Orbits (`orbit.hpp`)

While coasting under the Earth's gravity alone, two things stay constant:

- **specific energy** `ε = v²/2 − μ/r`
- **specific angular momentum** `h = r × v`, a vector perpendicular to the
  orbit

From them:

| Quantity | Formula |
|---|---|
| semi-major axis | `a = −μ / 2ε` |
| eccentricity vector (points to periapsis) | `e = ((v² − μ/r)·r − (r·v)·v) / μ` |
| periapsis / apoapsis | `r_p = (h²/μ) / (1 + e)`, `r_a = 2a − r_p` |
| period | `T = 2π √(a³/μ)` (Kepler's third law) |
| inclination | angle between `h` and the z axis |
| RAAN Ω | direction of the node vector `z × h` (where the orbit crosses the equator going north) |
| argument of periapsis ω, true anomaly ν | angles in the orbit plane to periapsis and to the craft |

Energy alone decides the fate: ε < 0 means the craft comes back (bound),
ε ≥ 0 means it escapes.

A due-east launch puts the orbit's highest latitude at the launch site, so
the **inclination equals the launch latitude**. This is why launches from
Florida (28.5°N) reach the equatorial plane only with an expensive plane
change, while launches near the equator don't need one.

## Integration (`integrator.hpp`)

The equations of motion are integrated with **Dormand–Prince 5(4)**, an
adaptive Runge–Kutta method:

- Each step evaluates the forces at seven points and forms a 5th-order and a
  4th-order estimate. Their difference measures the error.
- If the error is within tolerance the step is accepted. Either way, the next
  step size is scaled by `0.9 · error^(−1/5)`.
- So steps are short during hard burns and long while coasting far out. It's
  automatic, and accurate to about a metre after 10 low orbits.

**Dense output** gives the state at any time *inside* an accepted step at
no extra cost. The simulation uses it to:
- produce evenly spaced samples for drawing and graphs, and
- find **events** exactly. When something like "apoapsis reached the target",
  "started falling" or "hit the ground" happens during a step, bisection on the dense
  output finds the moment to about a microsecond, and the step is cut there.

Steps never cross points where the forces change abruptly (pitch program
changes, burnout), because that would spoil the error estimate.

## Outcomes

| Outcome | Meaning |
|---|---|
| Crashed | Hit the ground without reaching 100 km |
| Suborbital | Reached space, but the orbit dips underground |
| Orbit | The whole orbit stays above the surface |
| Escape | ε ≥ 0: never comes back |
| Lunar impact | Hit the Moon |
| Lunar orbit | Ended bound to the Moon, with its whole orbit above the surface |
