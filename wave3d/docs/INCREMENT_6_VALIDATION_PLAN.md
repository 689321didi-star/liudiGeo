# Increment 6 Predeclared Sponge Validation Plan

Status: fixed before the first sponge propagation run and passed on
2026-09-09.

Increment 6 adds only a replaceable multiplicative debug sponge.  It does not
change the elastic equations, add auxiliary constitutive state, or claim CPML
or free-surface behavior.

## Definition

For each absorbing side, normalized depth `r` increases from `1/width` in the
cell next to the physical model to `1` in the outermost updated absorbing
cell.  Its one-application factor is

```text
d_axis(r) = exp(log(d_outer) * r^2),  0 < d_outer <= 1.
```

An absent side contributes one.  The three axis factors are multiplied, so
edges and corners receive all relevant damping.  Halo-only storage beyond an
absorbing layer uses the outer factor but is not interpreted as physical
medium.  The prepared `float32` volume is applied independently after the
stress/source hook to six stresses and after the velocity update to three
velocities.  Tests use `d_outer=0.75`.

## Fixed tests and thresholds

- Unit checks require factor one throughout the physical model, monotonic
  damping on every enabled side, the analytical face/edge/corner products,
  exact no-op behavior for `d_outer=1`, and rejection of invalid parameters.
- CPU and CUDA application to deterministic nonzero fields must agree with
  direct `float32` multiplication with normalized maximum error at most
  `2e-6`.
- A centered isotropic source in a homogeneous `31^3` physical model uses
  `20` absorbing cells and six halo cells on all sides, `10 m` spacing,
  `Vp=3200 m/s`, `Vs=2200 m/s`, `rho=2500 kg/m^3`, `dt=0.5 ms`, and a delayed
  `30 Hz` Ricker pulse.  The damped and undamped center-line records must be
  agree through `35 ms`, before the fastest wave can reach the
  physical/absorbing interface, with normalized maximum error at most `1e-6`.
- For samples from `175 ms` through `260 ms`, covering the first return from
  the outer complete-stencil edge, the damped peak must be below `12%` of the
  undamped peak.  This deliberately loose debug threshold is not a CPML claim.
- A `600`-step (`300 ms`) damped run must leave all nine fields and all traces
  finite.  Coefficients and buffers are prepared before its time loop.

CPU-only and CUDA-enabled Release suites, ASan/UBSan, and Compute Sanitizer
memcheck/initcheck/racecheck/synccheck must pass before Increment 7 starts.

## Observed result

CPU and CUDA application agreed bitwise.  The early record error was `0`.
The late damped/undamped peak ratio was `0.01085103272`, well below `0.12`;
the corresponding peaks were `2.794222837e-6` and `2.575075487e-4 m/s`.
The 600-step run left every field and trace finite.  A representative direct
Release run took `1095.53 ms` undamped and `1143.00 ms` damped.  CPU Release
passed 12/12 tests, CUDA Release passed 15/15, and ASan/UBSan passed 12/12.
Compute Sanitizer memcheck and initcheck exercised the full 600-step case;
racecheck and synccheck exercised the focused deterministic CUDA sponge path,
all with zero reported errors, hazards, or warnings.
