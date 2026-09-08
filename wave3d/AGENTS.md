# Wave3D Agent Instructions

This file is the authoritative working agreement for every agent modifying this
repository. Read it completely before inspecting or changing implementation
files. Also read `docs/HANDOFF.md`, `docs/ARCHITECTURE.md`,
`docs/DECISIONS.md`, and `docs/ROADMAP.md` before beginning a development
increment.

## Project objective

Build a scientifically verifiable, modular, single-GPU program for 3D
isotropic elastic wave forward modeling. The target development and
execution platform is Linux with a desktop NVIDIA GeForce RTX 5060. A
`200 x 200 x 200` physical grid is the initial target problem size.

The linked Zhang Wei et al. 2020 paper concerns amplitude-preserving elastic
RTM. It and the legacy repository discussed in `docs/HANDOFF.md` inform the
elastic forward design and possible future RTM work. Do not claim reproduction
until the exact equations, parameters, source convention, and validation
results have been documented and tested.

Forward modeling is the current deliverable. RTM must be supported by clean
interfaces but must remain an optional, removable component and must not be
implemented until the forward milestones have passed.

## Non-negotiable development rules

1. Work incrementally. Each increment must have one narrow purpose, explicit
   acceptance criteria, and proportionate tests.
2. Do not begin the next increment until the current increment compiles and its
   tests pass on the active target environment. Record commands and results in
   `docs/HANDOFF.md`.
3. Never report uncompiled or untested code as complete. Clearly distinguish
   inspection, compilation, numerical verification, and physical validation.
4. Preserve the legacy source unchanged. It is evidence and a numerical
   reference, not the architecture of the new program.
5. Do not invent missing legacy kernels or infer a 3D operator from incomplete
   2D code. Document every 3D velocity/stress equation and its staggering.
6. Keep physics, numerical discretization, CUDA memory, acquisition geometry,
   boundaries, file I/O, diagnostics, and task orchestration decoupled.
7. The forward propagator must not depend on RTM, imaging conditions, P/S
   decomposition, SEG-Y, HDF5, or a specific configuration parser.
8. Optional features must be controlled by CMake options and removable without
   breaking the forward executable or core tests.
9. Prefer RAII ownership. Do not spread raw `cudaMalloc`/`cudaFree` calls
   throughout scientific code. Do not create a monolithic device structure
   containing forward, RTM, I/O, and multi-GPU state.
10. Use `float32` for production wavefields unless a validation experiment
    explicitly requires another precision. Use higher precision for selected
    diagnostics when it materially improves error measurement.
11. Use physical coordinates in public input APIs and convert them once to
    grid/storage coordinates. The coordinate convention is x east, y north,
    z positive downward, surface at z=0.
12. The canonical volume layout is `[z][y][x]`, with x contiguous:
    `index = x + nx * (y + ny * z)`.
13. Validate dimensions, units, finite values, material bounds, source and
    receiver positions, CFL stability, numerical dispersion, boundary width,
    integer overflow, and planned GPU memory before propagation.
14. Do not store every full 3D time step in GPU memory. Receiver traces,
    sparse diagnostic snapshots, and optional checkpoints have separate
    policies.
15. Never optimize solely by visual appearance or intuition. Establish a CPU
    reference or analytical comparison first, then profile with NVIDIA tools.
16. Keep original physical quantities and units in outputs. Any normalization
    for visualization or machine learning must be explicit and reversible.
17. Update handoff and decision documents whenever an increment changes an
    interface, equation, convention, dependency, or accepted limitation.

## Fixed scope and current choices

- Platform: Linux.
- Hardware target: one desktop RTX 5060; no multi-GPU work now.
- Medium: 3D isotropic elastic. Attenuation and viscoelastic rheologies are out
  of scope.
- Reference: Zhang Wei et al. 2020 and the supplied legacy source, with standard
  primary references used where the paper or legacy implementation is incomplete.
- Source: general symmetric moment tensor; an isotropic explosion is an allowed
  validation case.
- Acquisition: surface, three-component particle-velocity receivers.
- Boundaries: use simple absorbing boundaries for early propagation debugging;
  the intended physical configuration is a traction-free top and CPML on the
  other five sides.
- Internal scientific volume and trace format: HDF5 when the I/O increment is
  reached.
- External seismic trace exchange format: SEG-Y.
- Early debugging I/O: generated models plus small raw binary/CSV/JSON outputs.
- RTM: interfaces and checkpoint seams only; no current RTM implementation.
- Multi-GPU, anisotropy, poroelasticity, inversion, and deep learning are out of
  the current implementation scope.

## Required validation ladder

For every scientific feature, validate in this order where applicable:

1. Unit tests for indexing, coefficients, interpolation, configuration, and
   ownership.
2. CPU/GPU comparison on a small grid for a small number of time steps.
3. Homogeneous-model symmetry and theoretical P/S arrival times.
4. Grid-refinement or convergence behavior.
5. Layered-medium reflection/transmission behavior.
6. Boundary reflection and long-time stability tests.
7. Performance and peak-memory measurement on the RTX 5060.

Images that merely look plausible are not acceptance evidence.

## Git and change discipline

- Keep commits limited to a completed increment or a clearly identified
  documentation/setup change.
- Do not combine numerical changes with unrelated formatting or refactoring.
- Do not force-push or rewrite shared history.
- Do not commit build directories, wavefield volumes, SEG-Y files, HDF5 data,
  checkpoints, or generated run directories.
- Before committing, inspect `git diff`, run the relevant tests, and record any
  test that could not be run and why.
