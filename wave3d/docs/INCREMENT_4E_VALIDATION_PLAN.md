# Increment 4e Predeclared Validation Plan

Status: initial thresholds fixed before the first multi-step propagation run;
geometry Revision A recorded before the second run, 2026-09-09.

This plan qualifies only the boundary-free CPU interior path. Thresholds below
must not be widened after observing a result. A failed criterion requires a
documented diagnosis or a new increment, not post-hoc acceptance.

## Homogeneous case

- Physical grid: `41 x 41 x 41` nodes.
- Spacing: `dx=dy=dz=10 m`.
- Halo: six cells; no absorbing cells and no free-surface operation.
- Coordinate convention: x east, y north, z down.
- Material: `Vp=3200 m/s`, `Vs=2200 m/s`, `rho=2500 kg/m^3`.
- Time step: `dt=0.0005 s`.
- Ricker dominant frequency: `40 Hz`; peak rate `1 s^-1`.
- Moment scale: `1e12 N*m`.
- Source point: `(200,200,200) m`, the center integer lattice node.
- Axis receiver radius: `90 m`.

The design band has eight P points and 5.5 S points per wavelength and 50 time
samples per period. The accepted radius-six CFL limit and design-band checks
must pass before propagation.

## Arrival estimator and fixed tolerances

Arrival runs use `peak_delay=1.5/f0=0.0375 s`, whose omitted pre-zero Ricker
tail is `9.85e-9` of the peak. A particle-velocity far-field pulse is compared
with the analytical time derivative of the Ricker moment-rate function. The
observed feature time is the lag with maximum absolute matched-filter response;
the search interval is fixed at one dominant period on either side of the
theoretical feature. The normalized absolute correlation must be at least
`0.5` before its lag can qualify an arrival.

For speed `c`, define

```text
ppw = c/(f0*dx)
Rspace = 2*S(2*pi/ppw)/(2*pi/ppw)
Rcombined = asin(pi*f0*dt*Rspace)/(pi*f0*dt)
tolerance = 2*dt + (r/c)*abs(1/Rcombined - 1).
```

The two time samples cover leapfrog labeling and discrete lag selection; the
second term is the precomputed 12th-order spatial plus second-order temporal
phase prediction. Fixed values are:

| Feature | Theoretical time | Combined ratio | Tolerance |
| --- | ---: | ---: | ---: |
| P at 90 m | `0.065625 s` | computed from `ppw=8` | at most `0.00102 s` |
| S at 90 m | `0.0784090909 s` | computed from `ppw=5.5` | at most `0.00103 s` |

The test recomputes the exact ratios from the committed coefficients and also
asserts the displayed tolerance ceilings.

## Polarity, symmetry, and leakage

- A separate causal polarity run uses `peak_delay=0`. The first radial sample
  exceeding 1% of that trace's peak absolute amplitude must point away from
  the source at both positive and negative x receivers.
- For a delayed isotropic explosion, opposite-axis radial traces must match
  after sign reversal with relative L2 error no greater than `2e-5`.
- Positive x, y, and z radial traces must agree with relative L2 error no
  greater than `2e-5`.
- The summed transverse trace energy of the six axis receivers must be no more
  than `1e-8` of their summed radial trace energy.
- For a delayed `Mxy` source observed on the x axis, the matched-filter S
  feature is evaluated on `vy`; opposite x receivers must reverse sign with
  relative L2 error no greater than `2e-5`.

## Energy and boundary-safe windows

The causal Ricker source is treated as ended at `1.5/f0=0.0375 s`, where its
remaining magnitude is below `1e-8` of peak. The source-center distance to the
nearest physical edge is `200 m`, so the fastest theoretical boundary contact
is `200/3200=0.0625 s`. Energy is evaluated from `0.038 s` through `0.0615 s`,
strictly before that contact.

The diagnostic sums face kinetic energy and normal/edge strain energy using
the accepted `K`, `mu`, buoyancy, and cell volume. Every value must be finite
and positive. Over the fixed post-source window, `max(E)/min(E)` must not exceed
`1.05`; this is a bounded-energy gate, not a claim of exact same-time energy
conservation for leapfrog-staggered fields.

The shortest possible reflected P path from the center through the nearest
axis boundary to a 90 m axis receiver is `310 m`, or `0.096875 s`. Arrival runs
stop at `0.09 s`, before this theoretical reflected arrival.

## Geometry Revision A

The first run used a `31^3` grid and receivers at `60 m`, while retaining the
same material, sampling, source, estimator, and tolerance formulas. P passed.
The `Mxy` S trace had `0.981` normalized correlation with the analytical
far-field template, but its matched feature was `0.066027 s` rather than
`0.064773 s`, missing the fixed tolerance by about `0.00024 s`. The predicted
finite-difference phase shift is only about `0.000018 s`; inspection showed the
expected near-field Ricker contribution shifting the zero-phase landmark at a
range of only 1.09 S wavelengths.

Revision A increases the range to 1.64 S wavelengths and expands the symmetric
grid to keep both boundary-contact and reflected-arrival exclusions valid. It
does not change the material, `dt`, frequency, matched-filter definition,
correlation minimum, symmetry/leakage/energy limits, or arrival-tolerance
formula and ceilings.

## Determinism and allocation

Two identical short multi-step runs must produce bitwise-identical wavefields
and receiver samples. Wavefield, receiver frame, trace, and energy buffers are
allocated before stepping; their addresses and capacities must remain fixed,
and an allocation counter must report zero dynamic allocations inside every
time loop.

## Accepted results

Revision A passed without changing any threshold:

| Measurement | Observed | Acceptance |
| --- | ---: | ---: |
| P matched feature | `0.0658258512 s` | `0.065625 +/- 0.0010185209 s` |
| P normalized correlation | `0.9939220` | at least `0.5` |
| S matched feature | `0.0792410475 s` | `0.0784090909 +/- 0.0010263976 s` |
| S normalized correlation | `0.9714117` | at least `0.5` |
| Maximum symmetry relative L2 error | `1.21975e-7` | at most `2e-5` |
| Explosion transverse/radial energy | `2.05475e-16` | at most `1e-8` |
| Post-source energy max/min | `1.00094514` | at most `1.05` |
| Time-loop dynamic allocations | `0` | exactly `0` |

The causal positive explosion's first significant x samples were
`+2.6643791e-5 m/s` at the positive receiver and `-2.6643791e-5 m/s` at the
negative receiver, so both point away from the source. Repeated short runs
matched bitwise in all nine wavefields and all three receiver components.
