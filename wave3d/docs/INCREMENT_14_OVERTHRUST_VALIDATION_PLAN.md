# Increment 14 SEG/EAGE Overthrust Elastic Benchmark Plan

Status: fixed before implementation on 2026-09-09.

## Goal

Create a reproducible, reduced, isotropic solid elastic benchmark derived from
the SEG/EAGE 3-D Overthrust P-wave velocity model, convert it to Wave3D's
canonical HDF5 schema, and validate one CUDA forward run that writes a single
three-component SEG-Y record.

This increment does not add attenuation, fluids, anisotropy, RTM, imaging, or
new propagation equations.

## Scientific qualification

The original Overthrust distribution is a constant-density acoustic P-wave
velocity macro model. It does not uniquely determine S-wave velocity or
density. The resulting Wave3D input must therefore be named and documented as
a **derived isotropic elastic benchmark**, not as original measured elastic
properties.

The approved deterministic derivation is:

```text
Vs = Vp / sqrt(3)                 (Poisson ratio nu = 0.25)
rho = 1000 * 0.31 * Vp^(1/4)     (Gardner; Vp in m/s, rho in kg/m^3)
```

No value may be silently clipped. Every output cell must be finite and satisfy
`Vp > 0`, `Vs > 0`, `rho > 0`, and `Vp^2 > (4/3) Vs^2`.

## Provenance and data boundary

- The authoritative citation is Aminzadeh, Brac, and Kunz (1997),
  *SEG/EAGE 3-D Salt and Overthrust Models*, SEG/EAGE 3-D Modeling Series
  No. 1.
- The SEG Open Data page and its CC BY 4.0 attribution/license statement are
  the authority for redistribution terms.
- Record the exact download URL, byte count, SHA-256, container variables,
  dimensions, ordering, units, and value extrema before conversion.
- The official SEG S3 object currently returns HTTP 403. A mirror is acceptable
  only when its volume geometry, 25 m sampling, dimensions, and velocity range
  can be cross-checked against the documented model. Record the mirror as a
  transport source, not as scientific authority.
- Downloaded and generated `.mat`, raw-volume, HDF5, SEG-Y, and run-directory
  artifacts remain ignored by Git. Commit only source, deterministic
  configuration, provenance/checksum text, and verification results.

## Reduction contract

- Inspect orthogonal slices before selecting the crop.
- Select one fixed, documented window containing dipping layers, folds, and
  thrust discontinuities.
- Preserve the original 25 m sampling and cell values. Do not interpolate or
  smooth the P-wave velocity volume.
- Keep every physical dimension at or below 200 grid points and retain at
  least 96 points per horizontal axis when the source volume permits it.
- Store the selected physical volume in Wave3D `[z][y][x]` order with x
  contiguous. Any source-axis permutation must be explicit and tested.

## Implementation gates

### 14a — Provenance and source audit

Acceptance:

- Record source URL, size, SHA-256, variable metadata, dimensions, ordering,
  sampling, units, extrema, and the official-access failure.
- Independent byte-count and extrema checks agree with the decoded source.
- No production or test code changes are included in this gate.

### 14b — Pure conversion and unit tests

Implement a dependency-light model transformation that accepts a decoded
source volume plus explicit shape/crop metadata, applies the approved elastic
derivation, and returns a validated `PhysicalModel`. File-container parsing
must remain in an adapter/CLI layer.

Acceptance:

- Synthetic uniquely indexed input proves source ordering, crop bounds, and
  canonical `[z][y][x]` output.
- Exact Vp preservation, derived Vs/rho formulas, physical validation,
  deterministic output, invalid metadata, truncation, non-finite input, and
  arithmetic overflow are tested.
- CPU Release and focused ASan/UBSan tests pass before 14c begins.

### 14c — Canonical model and configuration

Decode the audited source, choose the fixed complex crop, write canonical
`overthrust_small.h5`, reread it, and emit a matching
`wave3d.forward.v2` configuration.

Acceptance:

- HDF5 round trip preserves exact binary32 Vp/Vs/rho values, shape, spacing,
  axes, and material extrema.
- Orthogonal model slices and per-property summaries show no axis reversal,
  transposition, interpolation, NaN, or Inf.
- The generated YAML extrema exactly match the HDF5 cells and the existing
  numerical validator accepts its grid, time step, design frequency, source,
  receivers, and five-side-CPML/free-surface geometry.

Prerequisite passed on 2026-09-09: structural validation no longer pairs
independent extrema into a nonexistent material. Positive bulk modulus remains
enforced cell by cell when the canonical HDF5 model is read.

### 14d — Incremental CUDA propagation

Run a small crop/small-step smoke case first, then the accepted reduced model.
The smoke case is checked with Compute Sanitizer memcheck. A time-refinement
check compares receiver samples from `dt` and `dt/2` at common times over a
short window; normalized L2 difference must not exceed 5%. If it fails, reduce
the production `dt`; do not relax the threshold.

Acceptance:

- The live 80%-of-free-memory plan plus 512 MiB reserve accepts the full run.
- The full run completes without CUDA errors or non-finite SEG-Y samples.
- The output directory contains exactly `record.sgy`; it has three traces per
  receiver in 14/13/12 component order, the configured sample interval, and
  finite nonzero samples.
- A conservative straight-line bound places the first significant recorded
  energy between the earliest `distance/max(Vp)` and latest relevant
  `distance/min(Vs)` travel times, with source delay and one-sample tolerance.
- All-options Release, focused I/O sanitizer, optional-I/O-off CPU, and
  supported adapter build combinations remain green.

## Stop condition

Increment 14 ends after this one derived Overthrust benchmark and its single
SEG-Y validation record are reproducible and documented. Do not continue to
Salt, SEAM, fluid-solid coupling, anisotropy, or RTM without a new user request
and a new acceptance plan.

## Gate results

### 14a

Passed on 2026-09-09. Full evidence and the precomputed conversion oracles are
recorded in `OVERTHRUST_SOURCE_AUDIT.md`.

### 14b

Passed on 2026-09-09. `derived_overthrust.hpp` keeps container parsing outside
the model layer and implements the direct crop plus approved elastic mapping.
The focused Release and ASan/UBSan tests passed. The complete dependency-free
CPU Release suite passed 17/17. No MAT or HDF5 adapter was added in this gate.

### 14c adapter sub-gate

Passed on 2026-09-09. The optional MatIO adapter validates the audited MATLAB
v5 variable contract, reads only the requested crop hyperslab, and performs an
explicit tested MATLAB-column-major to Wave3D-x-fastest reorder. Synthetic
Release and ASan/UBSan tests passed for valid unique-index data and malformed
spacing, dimensions, precision, and missing-variable cases. Canonical HDF5
and YAML generation was completed by the following sub-gate.

### 14c canonical artifacts

Passed on 2026-09-09. The audited MAT source produced an ignored canonical
`[187,200,200]` HDF5 model and a matching `wave3d.forward.v2` YAML. Immediate
HDF5 and YAML rereads were exact. Independent little-endian dataset extraction
matched all three frozen SHA-256 oracles, found 7,480,000 finite cells per
property, reproduced every derived Vs/rho float, and confirmed positive bulk
modulus everywhere. Orthogonal slices showed consistent structures and axes.
The 1 ms configuration passed CFL/dispersion validation at a 0.65487 CFL
fraction and 6.27582 minimum S-wave points per design wavelength. The complete
real-source preparation path also passed ASan/UBSan. Increment 14d propagation
remains pending.

### 14d smoke sub-gate

Passed on 2026-09-09. The fixed 64-cubed Overthrust profile exercised eight
steps through the production HDF5/YAML-to-CUDA-to-single-SEG-Y command.
Compute Sanitizer memcheck reported zero errors. The 10,944-byte record had 27
receiver-major traces with repeated 14/13/12 component codes, 8 samples at
exactly 1 ms, and 216/216 finite samples, 55 of them nonzero. Time refinement
and full-model propagation remain pending.
