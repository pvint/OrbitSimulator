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
| Drag | `a = −½ρ|v_air|·Cd·A·v_air / m` | Below 1000 km; see below |
| Mass flow | `dm/dt = −F / (Isp·g0)` | g0 = 9.80665 m/s² |

The **third-body** formula has two terms. The first is the body's pull on the
rocket. The second subtracts its pull on the Earth: our frame is centred on
the Earth, which itself falls towards the Moon and Sun, and only the
*difference* moves the rocket relative to the Earth. (It's also what raises
tides.)

Thrust and mass flow are those of the **stage** that is firing (see below).

## The atmosphere (`atmosphere.hpp`)

**Up to 86 km** the air follows the **1976 US Standard Atmosphere**: seven
layers, in each of which the temperature changes at a constant rate with
height (the *lapse rate* L) or stays constant. It cools through the
troposphere (−6.5 K/km, where the weather is), stays at −56.5 °C, warms in the
stratosphere (ozone absorbs sunlight) and cools again in the mesosphere.
Pressure comes from the **hydrostatic equation**, `dP/dh = −ρg` (each layer
of air holds up the weight of the air above it), and the **ideal gas law**,
`ρ = P·M / (R·T)`:

    L ≠ 0:  P = P_b · (T_b / T)^(g0·M / (R·L))
    L = 0:  P = P_b · exp(−g0·M·(H − H_b) / (R·T_b))

H is *geopotential* height, `H = r0·z / (r0 + z)`, which allows for gravity
weakening with height. The **speed of sound** is `a = √(γ·R·T / M)`, 340 m/s
at sea level and 295 m/s in the cold air at 11–20 km.

**Above 86 km** the air is so thin that the Sun's activity changes it a lot,
so we use the classic **exponential model** densities (Vallado, table 8-4) at
90, 100, ... 1000 km. Between two of them the density falls exponentially,
`ρ = ρ_1·exp(−(z − z_1)/H_s)`. The *scale height* H_s, the climb over which
the density falls by a factor e, grows from about 6 km low down to over
50 km at 300 km. At 200 km the air is about 10⁻¹⁰ as dense as at sea level;
above 1000 km we treat it as empty.

The atmosphere **turns with the Earth**.

## Drag (`aerodynamics.hpp`)

    D = ½ ρ v² · Cd · A,    against the velocity through the air

- `v` is the speed **relative to the air**, `v_air = v − ω × r`: the air
  moves with the ground, so a rocket on the pad feels no wind.
- `A = π·d²/4` is the frontal area (the *diameter* parameter).
- `Cd`, the **drag coefficient**, says how streamlined the shape is. The
  *drag coefficient* parameter is its value below the speed of sound. Near
  **Mach 1** shock waves form and Cd nearly doubles (the *transonic drag
  rise*), then eases off as the rocket goes faster. We scale Cd by a typical
  curve for slender rockets: ×1 up to Mach 0.8, ×1.8 at Mach 1.2, back to ×1
  by Mach 5.

**Dynamic pressure** `q = ½ρv²` is the push of the oncoming air. As the
rocket climbs it speeds up while the air thins, so q rises to a peak,
**max-Q**, and falls. That comes about a minute after liftoff, 10–15 km
up, and is the hardest moment for the structure; real rockets throttle back
through it. Most real launchers see 30–35 kPa. The default rocket sees about
63 kPa, because it has far more thrust for its weight (1.9 times at liftoff,
against a typical 1.2–1.5) and so is going faster while the air is thick.

**Drag loss** is the speed taken away by drag on the way up, `∫ D/m dt`.
Real launchers lose 100–200 m/s; the default rocket loses about 130. It is
small next to the gravity loss (around a kilometre per second), but a
blunt or fat rocket can lose far more: try the drag coefficient sweep.

Some consequences you can see in the simulator:

- **The best kick angle is lower with air.** A flatter climb spends longer in
  thick air. The default kick is 20° (it was 25° in a vacuum), and kicks
  that reach orbit in a vacuum now turn over into the ground.
- **The orbit ends a few kilometres under the target.** The ascent stops
  when the apoapsis reaches the target, at about 80 km, still in the upper
  air; drag on the coast up trims a little off. Real rockets aim high to
  allow for it.
- **A pure gravity turn can't reach orbit at all.** The best it manages is
  an orbit whose low point is inside the atmosphere, so the air brings it
  down within one revolution.
- **Low orbits decay.** Each revolution of a circular orbit loses
  `Δa = −2π (Cd·A/m) ρ a²` of its semi-major axis: over a kilometre at
  150 km, about 10 cm at 600 km for a typical satellite.

## The rocket (`rocket.hpp`)

Constant thrust `F` until the propellant runs out. The **exhaust velocity** is
`v_e = Isp·g0`. Isp, the specific impulse, measures engine efficiency in
seconds. The **Tsiolkovsky rocket equation** gives the total velocity change
the propellant can deliver:

    Δv = v_e · ln(m₀ / m_dry)

In flight, part of that Δv is lost to fighting gravity while climbing
("gravity losses") and some to pushing through the air ("drag losses"). That's
why the simulator shows both *ideal Δv* and *Δv used*, and the drag loss on
its own.

**Thrust and air pressure.** The engine burns propellant at a fixed rate,
but its nozzle opens onto the outside air, which presses back on the
exhaust leaving it: `F = F_vac − p·A_exit`. So an engine pushes least on
the pad and most in space. We describe each stage's engine by its vacuum
thrust and Isp and a **sea-level loss** `L = p0·A_exit / F_vac`:

    F(p) = F_vac · (1 − L·p/p0)          Isp(p) = Isp_vac · (1 − L·p/p0)

Merlin 1D (282 s at sea level, 311 s in vacuum) loses 9 %; the Space
Shuttle main engine (366 s and 452 s) 19 %. The default rocket loses 12 %:
1.36 MN in vacuum, 1.2 MN on the pad. The speed this costs, `∫ (F_vac − F)/m
dt`, is the **back-pressure loss**, about 100 m/s for the default rocket;
*Δv used* is what the engine really delivered: the vacuum rocket equation
less that loss.

**Thrust-to-weight ratio** on the pad, `F_sl / (m₀·g)` with the sea-level
thrust, must exceed 1 to lift off. It also shapes the gravity turn: a low ratio turns more, a high one less.
So a heavier rocket (more stages, more payload) needs a smaller kick.

## Stages (`rocket.hpp`)

A rocket is a stack of up to three **stages** with a **payload** on top. The
bottom stage fires first. When its propellant runs out it is **dropped**
(stage separation: its dry mass leaves the rocket, a sudden step in the mass
graph) and the stage above ignites at once.

Why bother? The rocket equation punishes dead weight: an empty tank still
has to be accelerated by the engine. Each stage gets its own rocket equation,
with everything above it (the upper stages, full, and the payload) as its
load:

    Δv_k = v_e,k · ln((m_k + m_above) / (m_dry,k + m_above))

and the rocket's ideal Δv is the sum. Cutting the default 64 t rocket into
two stages, with exactly the same structure, propellant and engines, raises
it from 9.2 to 13.2 km/s: the second stage no longer drags the first
stage's 3 t of empty tanks to orbit. That's enough to reach the Moon on
ordinary 340 s engines, which the single stage can't do.

A burn that empties a stage carries on with the next one, after a short
**coast between stages** (5 s by default; Saturn V took about 4 s, Falcon 9
about 7) so the two drift apart before the next engine lights. The coast
costs surprisingly little: gravity losses build up while the engine pushes
against gravity, and a rocket that coasts up to its apoapsis anyway only
does some of that coasting earlier. Only when the
last stage that can fire runs dry does the burn end (and then the empty
stage is kept: there's nothing to gain by dropping it).

**Dropped stages** fly on by themselves, unpowered, under gravity and drag,
until they hit the ground or the flight ends. A first stage dropped at
4 km/s and 75 km lands about 1500 km downrange. That's why launch sites
face an ocean, and why Florida launches eastward over the Atlantic.

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

**Gravity compensation.** Correcting only once the rocket sinks comes late
for a weak engine. An upper stage pushing about its own weight takes minutes
to circularize, and would sink tens of kilometres meanwhile. So the engine
aims at `Δv + (g − v_h²/r) · t_go · up`, where `t_go = |Δv| / (F/m)` is
roughly how long the burn still needs, and `g − v_h²/r` is the part of
gravity the sideways speed doesn't yet hold up. That is the speed gravity
will take away before the burn ends, supplied in advance by tilting the
nose up, as real upper stages do. For a short, hard burn it changes almost
nothing.

**Guided ascent** (the *Orbit (guided ascent)* plan). Everything above is
*open-loop*: the pitch program settles the whole climb in the first
seconds, and the rocket lives with the result. A heavy rocket flies flat
and circularizes low; a light one lofts. Real launchers steer by
*feedback* once they're out of the thick air. Ours, above 30 km, holds the
**apoapsis**: each moment it picks the most nearly level direction that
moves the apoapsis towards the target,

    d(apoapsis)/dt = (target − apoapsis) / t_go,    t_go = (m/ṁ)(1 − e^(−Δv/v_e))

(t_go: about how long the burn needs to reach orbital speed, from the rocket
equation), and puts all the rest of the thrust into sideways speed, which
raises the periapsis. When the periapsis reaches the target too, the orbit
is circular and the burn ends. A gravity-free "kick" doesn't change the
apoapsis, so the right pitch is found by bisection on the apoapsis that a
one-second push would give. A rocket that gains speed faster than it can
climb can't hold the apoapsis at all; it then stops, coasts up and
circularizes like the open-loop plan. Results: the two-stage rocket reaches
exactly 200 km with any payload up to 3 t and any kick from 12° to 24°; the
single stage pays about 110 m/s for the steeper climb.

## Flight programs (`flight_program.hpp`)

A flight is a list of **steps**, each burning or coasting, with a steering
mode and an end condition. The mission's **flight plan** builds the list:

| Plan | Steps |
|---|---|
| Orbit | Ascent → Coast to apoapsis → Circularization → Coast (1 revolution) |
| Orbit (guided ascent) | Ascent through the air (to 30 km) → Guided ascent → (Coast to apoapsis → Circularization, if needed) → Coast |
| Burn until empty | Ascent (until the propellant runs out) → Coast |
| Lunar flyby | as Orbit up to the parking orbit → wait for the Moon → Trans-lunar injection → Coast to the Moon → Coast |
| Lunar orbit | as Lunar flyby, then Lunar orbit insertion (brake) → Coast round the Moon |
| Custom program | your own list, edited in the Flight program panel |

**Steering** (which way the engine points):

| Mode | Direction | What it's for |
|---|---|---|
| Pitch program | vertical, kick, gravity turn (above) | the ascent |
| Prograde / retrograde | along / against the velocity | raising / lowering the orbit, the cheapest way |
| Towards circular orbit | `Δv = v_circular − v`, with gravity compensation | circularizing |
| Retrograde, relative to the Moon | against the velocity relative to the Moon | being captured by it |
| Normal / anti-normal | along / against `r × v` | turning the orbit's plane |
| Radial out / in | straight up / down | changing the orbit's shape (a radial burn raises one side and lowers the other) |
| Guided into orbit | apoapsis hold (above) | closed-loop ascent; pair with "In orbit at" |

**End conditions**: the propellant runs out; the apoapsis, periapsis or
altitude *reaches* a value (in whichever direction is needed: a retrograde
burn "until the periapsis reaches 50 km" lowers it); at apoapsis; at
periapsis; the orbit is circular; the Moon's arrival point is a given angle
ahead; closest to the Moon; captured by the Moon; a number of revolutions; a
time.

**Custom programs** start as a copy of a plan's steps (*Customize* in the
Flight program panel) and can then be edited freely. Each step's value can
be swept: sweeping the length of a coast sweeps when the next burn starts.
The examples include a **Hohmann transfer** (two prograde burns half an
orbit apart, the cheapest way between two circular orbits: from 200 to
1000 km it costs `Δv₁ + Δv₂ = 434 m/s`, with
`Δv₁ = √(μ/r₁)(√(2r₂/(r₁+r₂)) − 1)` and `Δv₂ = √(μ/r₂)(1 − √(2r₁/(r₁+r₂)))`)
and a **deorbit burn**, swept over the wait before it: each minute moves the
landing point about 450 km round the Earth.

Running out of propellant (in the last stage that can fire) ends any burn
and skips later burns. A step whose condition already holds is skipped, as
is one that takes no time (a 0 s wait), and a step can time out (a transfer
that misses the Moon never reaches "closest approach").

## Going to the Moon (`flight_program.cpp`, `lunar_targeting.hpp`)

**Trans-lunar injection (TLI)** is a prograde burn in the parking orbit that
raises the apoapsis a little beyond the Moon. The rocket then travels a
transfer ellipse for about three and a half days.

**When to burn.** Kepler's equation gives how far round the ellipse the
rocket travels before reaching the Moon's distance (ν, about 170°) and how
long that takes. The burn starts when the point where the Moon *will be* at
that time is ν ahead of the rocket. The classic **lead angle** (how far
ahead the Moon is at the moment of the burn) works out at about 110–130°.

*TLI start delay* shifts the burn by seconds. A later start adds a timed
coast; an earlier one ends the wait early, by the angle the rocket gains on
the Moon's arrival point in that time (its angular speed, 2π/88 min, less
the Moon's, 2π/27.3 days: about 4° a minute). Ten seconds moves the
arrival point by about 2300 km, more than the Moon's radius.

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
