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

## Forces (`simulation.cpp`)

| Force | Formula | Notes |
|---|---|---|
| Gravity | `a = −μ r / |r|³` | Point-mass Earth, μ = 3.986×10¹⁴ m³/s² |
| Thrust | `a = F/m` along the guidance direction | Only while the engine runs |
| Mass flow | `dm/dt = −F / (Isp·g0)` | g0 = 9.80665 m/s² |

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

## Orbits (`orbit.hpp`)

While coasting, two things stay constant:

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
