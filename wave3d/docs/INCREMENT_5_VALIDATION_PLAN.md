# Increment 5 Predeclared CUDA Validation Plan

Status: fixed before the first elastic CUDA kernel run and passed on
2026-09-09.

Increment 5 must reproduce the accepted boundary-free CPU reference without
adding a boundary algorithm or changing any physical convention.

## Comparison case

- Homogeneous `15 x 15 x 15` physical grid with six halo cells.
- `dx=dy=dz=10 m`, `Vp=3200 m/s`, `Vs=2200 m/s`,
  `rho=2500 kg/m^3`.
- `dt=0.0005 s`, eight complete steps.
- Centered `40 Hz`, positive isotropic `1e12 N*m` source with zero peak delay.
- Four interior, off-grid receivers; all source and receiver interpolation
  remains component-specific.
- Initial state is exactly zero on CPU and GPU.

The CPU and CUDA paths must execute the identical order: stress, source,
no-op stress boundary, velocity, no-op velocity boundary, receiver sampling.

Before that multi-step case, one deterministic nonzero manufactured state
exercises all nine wavefield components through the separate stress and
velocity launch interfaces.  It uses the same homogeneous coefficients and
the same fixed error metric, so shear paths that an isotropic centered source
can leave zero are compared explicitly.

## Fixed comparison metrics

For every one of the nine full padded fields and each of the three receiver
trace components, define

```text
normalized_max_error = max(abs(gpu-cpu)) / max(max(abs(cpu)), 1e-20).
```

The limit is `2e-5`. Every GPU result must be finite. Storage that the CPU
complete-stencil rules leave exactly zero must also remain exactly zero on the
GPU. Receiver sample `n` retains the implicit time label `(n+1)*dt`.

Device ownership is move-only. Coefficients, nine wavefields, source stencil,
receiver stencil, and complete trace storage are allocated and uploaded before
the timed loop. Kernel launch functions may not allocate, free, resize, or
copy a full field per step. The test warms up the CUDA runtime before measuring
the eight-step elapsed time and reports the owned device bytes.

All existing CPU physical tests, CUDA-enabled CTest targets, and Compute
Sanitizer memcheck/initcheck/racecheck/synccheck must pass before Increment 6.

## Observed result

All manufactured fields, all nine fields after eight complete source-driven
steps, and all three receiver trace arrays were bitwise equal to the CPU
reference on the target RTX 5060 (normalized maximum error `0`, versus the
fixed `2e-5` limit).  The direct Release run measured `400 us` for the eight
preallocated steps.  Wavefield, coefficient, source-stencil,
receiver-stencil, and trace ownership totaled `1,419,864 bytes`; CUDA runtime
overhead is not included in that deterministic owned-byte count.  CPU Release
passed 11/11 tests, CUDA Release passed 13/13, ASan/UBSan passed 11/11, and all
four Compute Sanitizer tools reported zero errors or hazards.
