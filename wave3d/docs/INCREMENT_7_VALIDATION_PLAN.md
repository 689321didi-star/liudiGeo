# Increment 7 Predeclared CPML Validation Plan

Status: fixed before the first CPML propagation run and passed on
2026-09-09.

## Unit and CPU/GPU gates

- Prepared integer/half profiles must be exact identity outside enabled PML,
  follow the specified `sigma`, `alpha`, `a`, `b`, and `1/kappa` formulas at
  selected nodes, distinguish half-grid depths, and reject invalid widths,
  time steps, frequencies, velocities, powers, kappa, and reflection targets.
- State owns exactly 18 zero-initialized padded `float32` arrays and is
  move-only.  The forward memory plan must enumerate those arrays plus the 18
  compact one-dimensional coefficient arrays instead of the sponge field.
- A deterministic nonzero small-grid state and a general six-component moment
  source run for eight steps on CPU and CUDA.  All nine fields, 18 memories,
  and three trace arrays must have normalized maximum error at most `3e-5`.
  No launch-time allocation or full-field copy is allowed.

## Reflection and stability gates

All physical cases use a homogeneous solid (`Vp=3200 m/s`, `Vs=2200 m/s`,
`rho=2500 kg/m^3`), `10 m` cubic spacing, `dt=0.5 ms`, 20-cell PML, quadratic
profiles, `kappa_max=1`, target `R=1e-3`, and a delayed `30 Hz` Ricker source.

- Normal incidence reuses the centered Increment 6 geometry.  From `175 ms`
  through `260 ms`, the CPML/undamped late peak ratio must be below `0.005`.
- The grazing proxy places source and receiver `40 m` from x-min and separates
  them by `200 m` parallel to that face, giving a specular incidence angle of
  about `68.2 degrees` from the normal.  A translated, expanded homogeneous
  grid supplies the no-near-boundary reference.  The maximum CPML-minus-
  reference residual from `90 ms` through `180 ms` must be below `0.02` of the
  reference direct-wave peak.
- A six-sided 800-step (`400 ms`) run must leave all fields, memories, and
  traces finite. Turning CPML off uses the unchanged interior step interface.

CPU-only and CUDA-enabled Release, ASan/UBSan, and focused/full Compute
Sanitizer checks must pass before Increment 8 begins.

## Observed result

The eight-step CPU/CUDA comparison produced normalized maximum error `0` for
all nine wavefields, all 18 memory fields, and all three traces.  The focused
case owned `1,758,096 bytes` of CPML state and coefficient storage.  Normal
first-return amplitude was `0.0002237981069` of the undamped return, below the
fixed `0.005` limit.  The near-grazing translated-reference residual ratio was
`2.265134082e-6`, below `0.02`.  Every field, memory, and trace remained finite
through 800 steps.

The `200^3` target plan now reports `1,060,788,480 bytes` (`1011.647 MiB`) for
the 18 full state fields plus six compact coefficient groups.  CPU Release
passed 13/13 tests, CUDA Release passed 17/17, ASan/UBSan passed 13/13, and all
four focused Compute Sanitizer tools reported zero errors or hazards.
