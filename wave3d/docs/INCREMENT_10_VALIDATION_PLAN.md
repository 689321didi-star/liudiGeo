# Increment 10 Predeclared RTX 5060 Qualification Plan

Status: passed on 2026-09-09. Fixed before the first `200^3` propagation run;
no numerical or memory threshold was relaxed after testing began.

## Case

- Target device: NVIDIA GeForce RTX 5060, compute capability 12.0.
- Physical grid: `200 x 200 x 200`; spacing `10 m`; radius-six halo.
- Allocated grid: `252 x 252 x 232`, or 14,732,928 cells.
- Boundary: traction-free top and 20-cell CPML on the other five sides.
- Homogeneous material: `Vp=3200 m/s`, `Vs=2200 m/s`,
  `rho=2500 kg/m^3`.
- Time: `dt=0.0005 s`, 4000 steps (2 s), satisfying the existing exact CFL
  and 30 Hz design-band checks.
- Source: off-node isotropic explosion at `(995,995,600) m`, with 30 Hz
  Ricker moment rate and no attenuation.
- Acquisition: nine irregularly staggered surface three-component receivers;
  receiver-major samples are preallocated for all 4000 steps.

## Measurements and fixed gates

- Query currently free CUDA memory, create the CPML memory plan, and reject the
  run unless required bytes plus the 512 MiB runtime reserve are no more than
  80% of currently free memory.
- Record deterministic owned device bytes, observed free-memory reduction,
  setup time, warm/cold propagation time, milliseconds per step, allocated
  cell updates per second, trace-download time, optional HDF5 write time, and
  output bytes.
- Run all 4000 steps. At completion every wavefield value, all 18 CPML memory
  fields, and all receiver samples must be finite. Surface normal and
  interpolated shear traction must be exactly zero after the final projection.
- Run an NVIDIA Systems profile on a short target-size sample to identify
  launch/API structure, then an NVIDIA Compute profile of the target kernels.
  Any profiling limitation must be recorded verbatim. Optimization is allowed
  only for an observed bottleneck and must retain all numerical regressions.
- Existing CPU, CUDA, boundary, physics, I/O-off, ASan/UBSan, and focused
  Compute Sanitizer gates must remain green. Large generated traces and
  profiler reports are temporary and must not be committed.

The 4000-step full run, not an extrapolation from a smaller grid, is the final
qualification gate. No threshold will be relaxed after it starts.

## Observed result

The complete case ran all 4000 steps in `292785.147840 ms`, or
`73.196287 ms/step` and `201.279718` million allocated-cell steps/s. Every
downloaded value was finite and maximum final surface traction was exactly
zero. Deterministic device ownership was `2023.692627 MiB`; the observed
free-memory reduction was `2090.25 MiB`. The conservative plan, including
padded model fields not retained on the device and a 512 MiB runtime reserve,
was `2704.293579 MiB` against a contemporaneous `5885.849999 MiB` budget.

Nine receivers by 4000 samples by three components took `0.090972 ms` to
download and `0.645352 ms` to write as a `440408`-byte HDF5 file. The temporary
file was removed. Host peak resident memory during final field/state validation
was `2219816 KiB`; there were no swaps or major page faults.

Nsight Systems 2025.6.3 attributed 51.7% of kernel time to the CPML stress
kernel and 48.1% to the CPML velocity kernel; source, sampling, and free-surface
kernels together used less than 0.2%. Its report was generated successfully,
although the collection agent required an interrupt after the target exited.
Nsight Compute 2026.1.1 required root performance-counter access. It measured
the stress kernel at 36.08 ms, 87.54% compute throughput, 13.41% memory
throughput, 56 registers/thread, and 60.16% achieved occupancy. The dominant
pipeline was FP64, as expected from the deliberate double-precision derivative
accumulation that preserves the accepted CPU reference behavior. No precision
or kernel-fusion optimization was made during this gate.

A target-sized one-step Compute Sanitizer memcheck reported zero errors. The
full run's launch checks, final finite-state scan, existing four-tool focused
boundary checks, and final regression suites complete the invalid-access and
stability gate.

Final CPU Release, CUDA Release, and ASan/UBSan suites passed 15/15, 20/20,
and 15/15 tests respectively. Full free-surface memcheck and initcheck each
reported zero errors; focused projection-only racecheck reported zero hazards
and synccheck reported zero errors. The focused environment switch avoided
instrumenting the desktop GPU for the redundant 600-step physics portion.
