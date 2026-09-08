# Wave3D Architecture Decision Record

This is a living summary of decisions reached with the user. Changes require a
new dated entry or an explicit amendment; do not silently reverse them.

## D001 — Create a new modular implementation

**Status:** Accepted, 2026-09-07

The Zhang Wei repository supplies useful numerical intent but its 3D directory
is incomplete and tightly couples forward modeling, P/S decomposition, RTM,
multi-GPU exchange, model buffers, and output buffers. Wave3D therefore uses a
new architecture and preserves legacy material as a reference. Reuse means
verified equations, coefficients, conventions, and test cases—not copying the
legacy monolithic structure.

## D002 — Incremental development with hard verification gates

**Status:** Accepted, 2026-09-07

The solver will not be generated in one pass. Each increment has explicit
acceptance criteria and must compile and pass relevant tests before the next
increment. Unverified code is recorded as pending, not complete.

## D003 — Linux and one desktop RTX 5060

**Status:** Accepted, 2026-09-07

Linux is the target environment. The current scope is one desktop RTX 5060 and
an initial `200 x 200 x 200` physical grid. Multi-GPU domain decomposition and
halo exchange are excluded until a correct single-GPU solver exists and a real
need is demonstrated.

## D004 — Forward modeling is the current product

**Status:** Accepted, 2026-09-07

Only 3D forward modeling is implemented now. RTM requirements shape clean
wavefield-view, propagator-factory, receiver-data, and checkpoint interfaces,
but RTM code remains optional and removable. Deep learning is downstream
research and is not part of the present solver implementation.

## D005 — Isotropic viscoelastic physics and reproduction-first policy

**Status:** Superseded by D018, 2026-09-08

The medium is 3D isotropic viscoelastic. The first attenuation implementation
must reproduce the method used in the identified Zhang Wei 2020 work, after
primary-source verification. A standard independently documented SLS/GSLS
variant may be added later as a comparison. Anisotropy, poroelasticity, and
other constitutive models are outside current scope.

## D006 — Velocity–stress staggered-grid finite differences

**Status:** Provisional

The legacy work uses a first-order velocity–stress style GPU finite-difference
framework and a radius value of 6. Wave3D will initially use a high-order
staggered-grid finite-difference scheme, but exact order, coefficients,
stability bound, staggering, and time-level convention remain provisional until
derived from and checked against the authoritative method.

The current `provisional_cfl_safety_factor=0.45` helper is a configuration smoke
check, not a final proof of stability for the eventual high-order operator.

## D007 — Canonical coordinates and storage layout

**Status:** Accepted, 2026-09-07

Public coordinates are x east, y north, z positive downward, in metres, with
the surface at z=0. Volumes use logical shape `[z][y][x]`, and x is contiguous.
Physical coordinates, physical-grid indices, and padded storage indices remain
distinct types or clearly separated transformations.

## D008 — General moment-tensor source

**Status:** Accepted, 2026-09-07

The source interface supports all six unique components of a symmetric moment
tensor and a separate source-time function. An isotropic explosive source is a
special validation case. Source sign convention, normalization, units, and
staggered injection must be documented and tested before scientific use.

## D009 — Surface three-component acquisition

**Status:** Accepted, 2026-09-07

The primary geometry is surface acquisition with `vx`, `vy`, and `vz` particle
velocity. Geometry is expressed in physical coordinates. The architecture must
allow irregular receiver locations and precomputed interpolation weights;
regular generated grids are sufficient for early tests.

## D010 — Free top surface and five absorbing sides

**Status:** Accepted with staged delivery, 2026-09-07

The intended physical model has a traction-free top satisfying the appropriate
normal and shear traction conditions, with CPML on both x sides, both y sides,
and the bottom. Early propagation tests may use absorption on all six sides to
separate interior-equation defects from free-surface defects. A simple sponge
precedes CPML as a debugging boundary.

## D011 — Layered file formats

**Status:** Accepted, 2026-09-07

Generated models and raw binary/CSV/JSON support the earliest tests. YAML is the
planned human-readable configuration. HDF5 is the canonical internal scientific
format for 3D models, traces, snapshots, and future checkpoints. SEG-Y is an
external trace exchange format, initially one file for each component. External
SEG-Y models are converted by a separate tool rather than interpreted by the
propagator.

## D012 — Optional dependencies

**Status:** Accepted, 2026-09-07

Core propagation depends on C++ and CUDA. YAML, HDF5, SEG-Y, testing frameworks,
and other libraries are optional adapters controlled through CMake. The core
must retain useful tests when optional I/O libraries are disabled.

## D013 — Forward-only memory allocation

**Status:** Accepted, 2026-09-07

Production wavefields use `float32` SoA storage and RAII device buffers. The
forward executable must not allocate RTM images, reverse fields, P/S-decomposed
fields, or complete time history. A pre-run planner reserves headroom for the
CUDA runtime and desktop use and rejects unsafe allocations.

## D014 — Checkpoint seam without current RTM

**Status:** Accepted, 2026-09-07

Observers and a checkpoint abstraction may receive read-only wavefield views.
No checkpoint policy is embedded in the propagation loop. Sparse checkpoints
plus recomputation are the leading future RTM strategy for an 8 GB device, but
this remains unimplemented and subject to measurement.

## D015 — Preserve reproducibility metadata

**Status:** Accepted, 2026-09-07

Runs will record resolved configuration, code revision, compiler/CUDA/GPU
environment, dimensions, units, time convention, source/receiver definitions,
memory plan, timings, and diagnostics. Normalization must be explicit and
reversible. SEG-Y output should have a metadata sidecar when standard headers
cannot preserve exact semantics.

## D016 — Validate through multiple independent checks

**Status:** Accepted, 2026-09-07

Validation includes unit tests, CPU/GPU field comparison, analytical arrival
times, symmetry, convergence, layered interfaces, boundary reflection,
long-time stability, and measured performance/memory. A plausible wavefield
image alone is insufficient.

## D018 — Remove viscoelastic attenuation from scope

**Status:** Accepted, 2026-09-08

The active product is a 3D isotropic elastic forward solver. `Qp`, `Qs`,
relaxation mechanisms, attenuation compensation, and viscoelastic memory
variables are not implemented. This supersedes D005. The scope reduction must
be reflected in code, tests, architecture, roadmap, and I/O schemas rather than
left as a disabled mode.

## D019 — Role of the Zhang et al. 2020 paper

**Status:** Accepted, 2026-09-08

The identified paper is Wei Zhang, Jinghuai Gao, Zhaoqi Gao, and Ying Shi,
“2D and 3D amplitude-preserving elastic reverse time migration based on the
vector-decomposed P- and S-wave records,” *Geophysical Prospecting* 68(9),
2712–2737, DOI `10.1111/1365-2478.13023`.

The paper addresses elastic RTM rather than a viscoelastic constitutive model.
It is relevant to elastic test parameters and future RTM interfaces, but it is
not the sole authority for the forward finite-difference implementation. The
user cannot provide the full PDF, so inaccessible equations or parameters must
not be invented; use auditable primary sources and label any independent
derivation explicitly.

## D017 — Preserve the remote history

**Status:** Accepted, 2026-09-08

The configured GitHub remote already contains a `main` commit. The new local
history must be reconciled explicitly. Do not force-push or discard remote
content merely to publish the local skeleton.
