# Increment 8 Predeclared Free-Surface Validation Plan

Status: fixed before the first traction-free propagation run and passed on
2026-09-09.

## Discrete boundary

The horizontal surface is the physical `z=0` integer plane at padded index
`k0=halo`; a free-surface grid therefore requires zero lower-z absorbing
width.  After the stress/source operation:

- `szz(k0)=0` exactly and `szz` is odd into the upper ghost cells;
- `sxz` and `syz`, which lie at z-half positions, are odd across `z=0`, so
  the average of their samples at `k0-1/2` and `k0+1/2` is exactly zero;
- tangential `sxx`, `syy`, and `sxy` ghost values are mirrored evenly.

After the velocity update, `vx`/`vy` integer-z ghosts and `vz` z-half ghosts
are mirrored evenly.  Six ghost layers are filled for the radius-six
operator.  Top CPML is disabled; x-min/x-max/y-min/y-max/z-max CPML remains.

## Fixed validation

- Unit tests use arbitrary nonzero fields and require exact zero normal and
  interpolated shear traction, every declared odd/even ghost relation, no
  change below the boundary, and rejection of incompatible grids or states.
- CPU and CUDA projection must agree bitwise for all nine fields.
- A homogeneous model uses a source `100 m` below the surface and a collocated
  surface receiver.  `Vp=3200 m/s`, `Vs=2200 m/s`, density `2500 kg/m3`,
  spacing `10 m`, `dt=0.5 ms`, and a delayed `30 Hz` isotropic source are
  fixed.  A translated six-CPML full-space run supplies the incident reference.
- Within `55–90 ms`, the free-surface and reference vertical arrivals must
  select peak samples no more than one `dt` apart, have the same upward
  (negative-z) polarity, normalized waveform correlation at least `0.98`, and
  a peak-amplitude ratio in `[1.6, 2.4]`, bracketing the normal-incidence
  velocity doubling limit at this discrete sampling position.
- Surface traction must still be exact after the final step and a 600-step
  five-side-CPML/free-surface run must leave all fields, CPML memories, and
  traces finite.

CPU-only and CUDA-enabled Release, ASan/UBSan, and focused Compute Sanitizer
memcheck/initcheck/racecheck/synccheck must pass before Increment 9 starts.

## Observed result

CPU and CUDA ghost projection agreed bitwise, and final normal/interpolated
shear traction was exactly zero.  The surface and full-space vertical peaks
occurred at samples `132` and `131`; both were upward (`-0.003621267155` and
`-0.00175894564 m/s`).  Their amplitude ratio was `2.0587715` and waveform
correlation was `0.9892208823`, passing every fixed threshold.  The 600-step
surface/CPML state remained finite.  CPU Release passed 14/14, CUDA Release
19/19, ASan/UBSan 14/14, and all four focused Compute Sanitizer tools reported
zero errors or hazards.
