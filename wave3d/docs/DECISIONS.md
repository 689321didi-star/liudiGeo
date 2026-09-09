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

**Status:** Accepted through D022, 2026-09-08

The legacy work uses a first-order velocity–stress style GPU finite-difference
framework and a radius value of 6. Wave3D uses the independently derived
standard 12th-order radius-six spatial operator and second-order leapfrog
scheme fixed in `ELASTIC_NUMERICAL_SPEC.md` and D022.

Increment 4a replaced the provisional safety hook with the accepted
coefficient-aware CFL and dispersion validation before any time stepping was
implemented. The default safety factor is `0.9`, and every run must declare a
positive design frequency.

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
special validation case. D022 fixes its sign, normalization, units, time level,
and stagger-specific interpolation requirements.

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

## D017 — Preserve the remote history

**Status:** Accepted, 2026-09-08

The configured GitHub remote already contains a `main` commit. The new local
history must be reconciled explicitly. Do not force-push or discard remote
content merely to publish the local skeleton.

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

## D020 — Optional CUDA foundation and conservative memory gate

**Status:** Accepted, 2026-09-08

CUDA is enabled only with `WAVE3D_ENABLE_CUDA=ON`; the CPU core and memory-plan
tests remain buildable without CUDA headers or libraries. The target build uses
`sm_120` for the RTX 5060. CUDA errors retain the failed operation and runtime
error code, kernel launches are checked, and device allocations are owned by a
move-only RAII buffer.

Before allocating production fields, a pure C++ planner compares every named
allocation plus a runtime reserve against a configurable fraction of current
free device memory. The initial plan deliberately retains conservative model,
coefficient, wavefield, sponge, receiver, and workspace allocations. It must be
revised explicitly as later ownership and boundary designs become concrete.

## D021 — Physical input storage and prepared acquisition coordinates

**Status:** Accepted, 2026-09-08

Elastic input models own only the unpadded physical `Vp`, `Vs`, and density
arrays in `[z][y][x]` order with contiguous x. Halo and absorbing storage are
propagator concerns; duplicating them in the input model would obscure model
semantics and commit to an unverified boundary implementation. Homogeneous and
horizontal-layer generators produce this same representation.

Public source and receiver locations are expressed in metres and must lie on
or within the physical grid's node extent. Preparation maps them once to
fractional padded-storage coordinates. Regular surface receivers use x-fastest
ordering and declare `vx`, `vy`, and `vz` as their eventual sampled components.
The six-component symmetric moment tensor uses N·m and a separately parameterized
Ricker moment-rate function. D022 resolves injection sign, normalization,
interpolation, and staggered placement for the CPU reference.

## D022 — Accepted elastic interior numerical contract

**Status:** Accepted, 2026-09-08

The authoritative implementation contract is
`docs/ELASTIC_NUMERICAL_SPEC.md`. Wave3D uses tension-positive stress, a
complete 3D first-order isotropic velocity-stress system, standard 12th-order
radius-six centered staggered derivatives, and second-order leapfrog time
integration. Velocities live at face offsets, normal stresses at integer
points, and shear stresses at the corresponding edge offsets.

The physical solid requires positive density, shear modulus, and bulk modulus,
equivalently `Vs>0` and `Vp^2>(4/3)Vs^2`. Face buoyancy uses the reciprocal of
the arithmetic density mean; edge shear modulus uses the four-point harmonic
mean. The exact operator spectral maximum is
`1187803/887040`, giving the coefficient-aware CFL formula recorded in the
specification. A declared design frequency must also pass spatial and temporal
dispersion gates.

A constant moment tensor `M` in N·m is paired with a Ricker moment-rate
function `q(t)` in `s^-1`. Its body-force convention is
`f_i=-M_ij*s(t)*partial_j(delta)`, so the tension-positive stress-rate source is
`-M_ij*q(t)*delta`. Discrete trilinear delta weights sum to one and the update
divides by cell volume. Positive equal diagonal components define an explosion
and must produce outward first motion.

The specification and `LEGACY_AUDIT.md` were completed before propagation
code. The legacy repository has no supplied license and is not copied. Its
radius, rounded coefficients, layout, and broad launch order are corroborating
evidence only.

## D023 — Prepared elastic coefficient ownership and numerical gate

**Status:** Accepted and implemented, 2026-09-09

Increment 4a stores the six derivative weights as exact integer ratios with
compile-time derived doubles and stores the exact spectral maximum
`1187803/887040`. Runtime numerical validation uses double-precision time and
design-frequency values, applies the accepted coefficient-aware CFL formula,
and independently enforces five S-wave points per design wavelength on every
axis plus twenty samples per design period.

`PhysicalModel` remains unpadded. `ElasticCoefficients` owns nine padded
`float32` volumes: collocated `lambda`, `mu`, and `K`; three face buoyancies;
and `mu_xy`, `mu_xz`, and `mu_yz`. Preparation computes in double precision,
extends physical edge samples constantly before staggering, and rejects
required nonzero coefficients that overflow or underflow `float32`. The memory
planner enumerates these actual nine fields. No derivative, wavefield, or time
update belongs to this decision.

## D024 — Explicit CPU staggered-derivative mappings

**Status:** Accepted and implemented, 2026-09-09

The CPU reference exposes one pointwise radius-six derivative that requires an
explicit axis and an explicit `IntegerToHalf` or `HalfToInteger` mapping. For
an allocated axis of length `n`, the valid target ranges are `[5,n-6)` for
`I->H` and `[6,n-5)` for `H->I`, with the upper limit exclusive. These ranges
encode the asymmetric array indices caused by the half-cell lattice offset;
callers may not compensate with ad-hoc index shifts.

The function accepts padded `float` or `double` fields, checks the full volume
size and target support, and accumulates into double without allocating.
Boundary targets without all twelve source samples are rejected because the
Increment 4 CPU interior does not yet define a boundary algorithm. Wavefield
ownership and update equations remain outside this interface.

## D025 — Move-only wavefield and split CPU leapfrog updates

**Status:** Accepted and implemented, 2026-09-09

`ElasticWavefield` owns exactly nine zero-initialized padded `float32` SoA
arrays: three particle velocities and six symmetric stresses. Implicit copies
are disabled and moves transfer ownership. Wavefield and coefficient layouts,
exact grid identity, positive finite `dt`, and finite `float32` update results
are checked before or during the transparent CPU reference operation.

Stress and velocity advancement remain separate calls. Stress uses velocity at
integer time `n` to advance stress from `n-1/2` to `n+1/2`; velocity then uses
that half-step stress to advance from `n` to `n+1`. This split reserves the
specified source and stress-boundary hooks between them and the velocity
boundary/receiver hooks afterward. Each field updates only its own
complete-stencil target region. No source, receiver, boundary behavior, or
multi-step driver is implied by Increment 4c.

## D026 — Component-specific staggered acquisition stencils

**Status:** Accepted and implemented, 2026-09-09

Acquisition preparation converts each physical source or receiver point into
an independent fixed eight-node trilinear stencil for every required field
lattice. Normal stresses use the integer lattice; `sxy`, `sxz`, and `syz` use
their corresponding edge lattices; `vx`, `vy`, and `vz` use their distinct
face lattices. A stencil is valid only when all eight nodes exist in allocated
storage and its finite, non-negative weights sum to one within double-precision
roundoff. No clipping or silent boundary renormalization is permitted.

Moment-source injection samples `q(t_n)` from the supplied step index and adds
`-dt*Mij*q(t_n)*w/(dx*dy*dz)` once to each tensor component's stress field.
Updates are checked for finite `float32` representation before any component is
written. Receiver sampling occurs after velocity step `n`, labels every sample
`(n+1)*dt`, and writes into caller-preallocated storage without resizing it.
This establishes numerical acquisition mechanics only: multi-step propagation,
explosion first-motion polarity, arrivals, symmetry, energy behavior, and
surface/free-surface validity remain later gates.

## D027 — Qualified boundary-free CPU elastic reference

**Status:** Accepted and implemented, 2026-09-09

The Increment 4 CPU path composes each step in the fixed order: stress update,
moment-rate injection at `q(n*dt)`, explicit no-op stress-boundary position,
velocity update, explicit no-op velocity-boundary position, and three-component
sampling labeled `(n+1)*dt`. The caller retains the time loop and owns all
receiver-frame and trace storage. Valid stepping performs no dynamic allocation.

Physical acceptance uses the predeclared homogeneous case and thresholds in
`INCREMENT_4E_VALIDATION_PLAN.md`. Arrival lags use a normalized matched filter
against the time derivative of the Ricker moment-rate pulse and tolerances
derived from time sampling plus the combined radius-six spatial and leapfrog
temporal phase prediction. The first `60 m` S trial was rejected because its
near-field contribution shifted the far-field landmark outside the unchanged
tolerance; Revision A increased the range and domain without relaxing any
acceptance threshold.

The accepted `90 m` P and S results, explosion polarity, cubic symmetry,
transverse leakage, bounded post-source energy, bitwise repeatability, and
zero-allocation time loops complete Increment 4. This is not a boundary
qualification, CUDA implementation, file-output feature, or surface-record
claim.

## D028 — Separate preallocated CUDA elastic reference path

**Status:** Accepted and implemented, 2026-09-09

The first GPU implementation deliberately keeps stress, moment-source,
velocity, and receiver sampling in separate CUDA kernels.  It owns nine
wavefield and nine coefficient arrays through move-only `DeviceBuffer`
members, uploads fixed source/receiver interpolation tables once, and stores
all three receiver components in preallocated receiver-major arrays.  A step
does not allocate, free, resize, copy a full field, or synchronize.

The same grid identity, positive time-step, source time `q(n*dt)`, stress sign,
cell-volume normalization, complete-stencil target ranges, velocity sampling,
and implicit `(n+1)*dt` trace labels as the CPU reference are retained.  The
target RTX 5060 produced bitwise-equal fields and traces in the predeclared
Increment 5 tests.  This decision does not add a boundary or authorize kernel
fusion; later optimization must continue to compare against this clear path.

## D029 — Replaceable multiplicative debug sponge

**Status:** Accepted and implemented, 2026-09-09

The debug absorber is a separately prepared full-volume `float32` factor and
two explicit application hooks.  On an enabled side, normalized absorbing
depth `r` uses `exp(log(d_outer)*r^2)`; three axis factors multiply at edges
and corners.  Physical cells retain an exact factor of one, and halo storage
beyond an absorbing layer retains its outer factor.  Tests use
`d_outer=0.75` per stress or velocity application.

The sponge is intentionally stateless and replaceable.  It provides an early
finite-domain baseline but is not called CPML, does not change the interior
operator, and does not define a traction-free surface.  The predeclared
reflection experiment reduced the first-return peak to `1.0851%` of the
undamped result while preserving the pre-boundary record exactly.

## D030 — Unsplit CPML with explicit derivative state

**Status:** Accepted and implemented, 2026-09-09

Wave3D adopts the Komatitsch–Martin unsplit convolutional recurrence described
in `CPML_NUMERICAL_SPEC.md`.  Integer and half positions have independent
one-dimensional `a`, `b`, and inverse-`kappa` arrays.  The first implementation
uses `m=2`, `kappa_max=1`, `alpha_max=pi*f0`, and a declared target reflection
to calculate each side's `sigma_max`.  Disabled sides are exact identity.

All 18 elastic derivative histories are explicit full-volume `float32` state.
This costs about `1011.647 MiB` for the initial target allocation but keeps
ownership, validation, CPU comparison, and removal simple.  Compaction to PML
slabs is a future measured optimization, not an implicit layout change.  CPML
has its own stress/velocity calls, while the accepted boundary-free calls stay
unchanged.  Increment 8 may disable only `z_min` and compose the CPML with a
traction-free top without changing the CPML recurrence.
