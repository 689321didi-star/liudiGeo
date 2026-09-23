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

**Status:** Accepted, 2026-09-07; SEG-Y file organization superseded by D035

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

**Status:** Accepted, 2026-09-07; SEG-Y sidecar policy superseded by D035

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

## D031 — Explicit radius-six traction-free ghost projection

**Status:** Accepted and implemented, 2026-09-09

The production top boundary is a separate projection at physical `z=0`, with
top CPML disabled and CPML retained on the other five sides.  It requires six
upper ghost layers: integer `szz` is odd and exactly zero on the surface;
z-half `sxz`/`syz` are odd about the surface; tangential stresses and the
required velocity ghosts use even continuation.  Projection occurs at the two
boundary hook locations and neither changes subsurface values nor owns another
volume.

This implementation is directly qualified for exact traction, normal-incidence
timing/polarity/velocity doubling, surface interpolation, and 600-step
stability.  Oblique mode-conversion benchmarks remain a desirable later
physics extension; they are not silently inferred from the normal-incidence
result.

## D032 — Typed, optional, propagation-independent production I/O

**Status:** Accepted and implemented, 2026-09-09; SEG-Y output superseded by D035

File formats terminate at validated domain objects and are never parsed by the
elastic propagator. YAML, HDF5, and SEG-Y each have an independent, default-off
CMake option; strict receiver CSV and shared typed I/O data remain dependency-
free. This preserves useful CPU/CUDA builds on machines without scientific I/O
libraries.

HDF5 is the canonical lossless internal representation: model axes are
`[z,y,x]`, single-source trace axes are `[source,receiver,time]`, and sparse
snapshots carry explicit storage indices. SEG-Y is an exchange format only:
VX/VY/VZ are separate big-endian Rev-1 IEEE-float files, coordinates use a
millimetre scalar, and exact binary64 sampling/source/component semantics are
recorded in a JSON sidecar without normalizing samples. External SEG-Y model
volumes are converted by a separate executable and must declare matching fixed
trace/sample dimensions.

## D033 — Accuracy-first RTX 5060 production envelope

**Status:** Accepted and measured, 2026-09-09

The supported baseline is one RTX 5060, a `200^3` physical grid with the
documented padding, five-side CPML, a traction-free top, 4000 samples, and
sparse surface receiver output. Every invocation must plan against currently
free CUDA memory and keep the 80% budget plus 512 MiB reserve rule; the
qualification measurement is evidence, not a future allocation guarantee.

Nsight shows the separate CPML stress and velocity kernels consume 99.8% of
GPU kernel time and that double-precision arithmetic, not device memory or
output, is the limiting resource. Double accumulation remains intentional
because it underpins the accepted transparent CPU/GPU comparisons. A later
optimization may change precision, register pressure, fusion, or CPML layout
only behind a new numerical gate. Increment 10 therefore qualifies the
accuracy-first reference path without silently trading accuracy for speed.

## D034 — Read-only extension seam and removable checkpoint boundary

**Status:** Accepted and implemented, 2026-09-09

Extension consumers receive non-owning const views of all nine elastic fields
plus explicit completed-step metadata. They cannot obtain mutable pointers or
control the time-step order. Observer registration closes before the first
step; the transparent CPU reference factory owns all state, preallocates
receiver-major output, and performs no dynamic allocation while stepping.
`IReceiverData` similarly separates component/sample access from any HDF5 or
SEG-Y implementation.

Checkpoint storage is not a forward dependency. `ICheckpointStore` and its
interval observer live under `optional/rtm`, reachable only through the
default-off `WAVE3D_ENABLE_RTM` interface target. Save accepts a const view;
restore requires an explicit mutable destination and exact grid identity.
Removing the entire optional tree leaves the forward build and binary
unchanged. This seam authorizes no RTM, imaging, reverse, or decomposition
implementation.

## D035 — One self-describing three-component SEG-Y output

**Status:** Accepted and implemented, 2026-09-09

At the user's request, the external seismic record is one SEG-Y file rather
than three per-component files plus JSON sidecars. The output remains the
widely interoperable big-endian SEG-Y Revision 1 core with fixed-length IEEE
`float32` traces. It contains one common-source ensemble ordered by receiver,
then VX, VY, and VZ.

For this synthetic survey, x/east is declared in-line and uses trace
identification code 14; y/north is cross-line and uses code 13; z/down is the
vertical component and uses code 12. Both coordinate and elevation scalars are
`-1000`, horizontal coordinates are millimetres, receiver z becomes elevation
`-z`, and source z becomes positive depth below the z=0 surface. The 3200-byte
ASCII textual header declares ordering, axes, units, lack of normalization,
source metadata, and the `(n+1)*dt` first-sample convention.

No JSON sidecar is emitted. Consequently, a sample interval not exactly
representable as an integer number of microseconds in Revision 1 is rejected
instead of rounded. D035 supersedes only the SEG-Y organization and sidecar
clauses of D011, D015, and D032; HDF5 remains the lossless canonical internal
format and the propagator remains independent of all file adapters.

## D036 — YAML v2 declares heterogeneous model extrema

**Status:** Accepted and implemented, 2026-09-09

The original YAML contract repeated one `homogeneous_material`, even when its
`model_hdf5_path` could select a heterogeneous volume. That is unsafe for CFL
and dispersion validation. `wave3d.forward.v2` therefore removes the redundant
homogeneous triple and serializes all six `SimulationConfig::material` extrema:
minimum/maximum Vp, Vs, and density.

The YAML adapter remains independent of HDF5 and validates the declared
bounds. The production task must separately calculate extrema from the loaded
HDF5 cells and require exact binary32 agreement before propagation. Missing,
old, or unknown YAML schemas fail rather than being guessed.

## D037 — Optional HDF5-to-SEG-Y CUDA production task

**Status:** Accepted and implemented, 2026-09-09

`wave3d_run CONFIG.yaml` is the general single-source forward command and is
built only when CUDA, YAML, HDF5, and SEG-Y are all enabled. Relative paths are
resolved against the YAML location. The command accepts only canonical Wave3D
HDF5 models, requires exact grid and declared/calculated material-extrema
agreement, validates the SEG-Y sample axis and the current-memory plan before
device allocation, and writes exactly `<output_directory>/record.sgy`.

The task composes existing prepared coefficients, source and receiver
stencils, CPML, optional traction-free top, CUDA kernels, and the single-file
SEG-Y writer. It introduces no propagation equation or kernel. CPML uses the
accepted target reflection `1e-3`, power 2, and `kappa_max=1`, with actual
maximum model Vp and the source dominant frequency. A free surface disables
z-min CPML; an absorbing top enables all six sides.

## D038 — Derived isotropic elastic SEG/EAGE Overthrust benchmark

**Status:** Accepted by the user and source-audited, 2026-09-09

The next and only authorized benchmark is a reduced solid-domain model derived
from the SEG/EAGE 3-D Overthrust P-wave velocity macro model. The original is
an acoustic, constant-density model and does not uniquely supply elastic
properties. Wave3D must therefore label the result as derived rather than
claiming original or measured `Vs` and density.

The accepted deterministic mapping is `Vs=Vp/sqrt(3)`, corresponding to
Poisson ratio 0.25, and Gardner density
`rho=1000*0.31*Vp^(1/4)` for Vp in m/s. Calculations occur in binary64 and are
converted once to binary32. No input or result is clipped, smoothed, or
normalized.

The fixed zero-based crop is source `[z,y,x]` window
`[0:187,153:353,154:354]`, giving canonical Wave3D shape
`[187,200,200]` at the unchanged 25 m sampling. It was selected by the maximum
depth-integrated horizontal absolute-gradient score and confirmed on
orthogonal slices. Large source, HDF5, SEG-Y, images, and run artifacts remain
outside Git; code, checksums, configuration, attribution, and results remain
reproducible in the repository.

## D039 — Cellwise elasticity is not inferred from independent extrema

**Status:** Accepted and implemented, 2026-09-09

`MaterialExtrema` stores six independent global bounds for CFL, dispersion,
and model-identity checks. Pairing global minimum Vp with global maximum Vs
constructs a material that may not occur in a heterogeneous volume, so that
pair cannot soundly prove a non-positive bulk modulus. The structural
configuration validator therefore validates each bound and its ordering but
does not infer a cellwise Vp/Vs relation from unrelated extrema.

The physical-model boundary remains strict: every HDF5 cell is validated and
must satisfy `Vp^2 > (4/3)Vs^2` before coefficients or propagation are
created. The production runner also requires exact agreement between the YAML
extrema and those validated HDF5 cells. This preserves material safety while
allowing valid broad-range heterogeneous models such as derived Overthrust.

## D040 — Optional audited MATLAB v5 Overthrust adapter

**Status:** Accepted and implemented, 2026-09-09

The benchmark converter uses MatIO only behind the default-off
`WAVE3D_ENABLE_OVERTHRUST` option. Enabling it requires the already optional
HDF5 and YAML adapters; ordinary CPU, CUDA, and production builds do not gain
a MATLAB dependency.

The adapter accepts only the audited real-double MATLAB v5 contract:
`d=[dz,dy,dx]`, `n=[nz,ny,nx]`, and `data[nz,ny,nx]`. It reads only the fixed
crop hyperslab, converts MATLAB column-major data explicitly to Wave3D
`[z][y][x]` x-fastest order, and rejects non-positive, non-finite, or
non-binary32-exact Vp. Container parsing remains separate from the pure
elastic derivation.

## D041 — Fixed Overthrust forward-acquisition configuration

**Status:** Accepted and implemented, 2026-09-09

The preparation command emits one deterministic validation configuration, not
a family of tunable benchmarks. Its 200 x 200 x 187 physical grid retains 25 m
sampling, uses radius-six halo storage, 20-point CPML on x/y and z-max, and a
traction-free z-min surface. The time axis is 3000 samples at exactly 1 ms;
the accepted 9 Hz design band has at least 6.2758 points per minimum-S-wave
wavelength and the step occupies about 65.49% of the accepted CFL limit.

An `Mxy=1e12 N*m` double-couple at `(2500,2500,1100) m` with a 3 Hz Ricker
moment-rate and one-period peak delay excites both P and S motion. A 121-point
surface array spans 500-4500 m on x and y at 400 m spacing. The 3 s record
window exceeds the conservative farthest-receiver distance/minimum-Vs time
plus source delay. Increment 14d must still qualify the actual CUDA record and
time refinement; this decision does not claim propagation success in advance.

## D042 — Fixed Overthrust CUDA smoke profile

**Status:** Accepted and implemented, 2026-09-09

`wave3d_prepare_overthrust --smoke` exists only as the first Increment 14d
execution gate. It takes the fixed 64-cubed source window beginning at original
`[z,y,x]=[0,221,222]`, retains 25 m sampling and the free surface, uses
10-point CPML on the other five sides, and runs eight 1 ms steps with a 3 x 3
surface receiver grid. Its source is an `Mxy` double couple at
`(800,800,600) m`.

This profile is deliberately not a substitute for the accepted 200 x 200 x
187 model or 3 s record. It exists to exercise real Overthrust MAT decoding,
HDF5/YAML I/O, coefficient preparation, CPML, the free surface, source,
receivers, CUDA kernels, and the single-file SEG-Y writer cheaply under
Compute Sanitizer before larger runs.

## D043 — Full-model Overthrust time-refinement gate

**Status:** Accepted and verified, 2026-09-09

The fixed refinement profiles use the accepted full model and acquisition for
a nominal 0.8 s window. The coarse record has 800 samples at 1 ms and the fine
record has 1600 samples at 0.5 ms. Because `TimeConfig` defines step count by
ceiling division, the serialized end-time request is the immediately lower
binary64 neighbor of 0.8; both resulting `(n+1)dt` records end exactly at
0.8 s without an unintended extra step.

Comparison pairs coarse sample `n` with fine sample `2n+1`, which represents
the same physical `(n+1)dt` time. The independent standard-library verifier
checks IEEE SEG-Y, finite data, receiver/component order, and unchanged trace
headers before computing binary64 normalized L2 error over all traces. The
accepted threshold remains 5%. The observed all-trace error was
`0.000433639742266` (0.0434%); VX, VY, and VZ errors were respectively
`0.000454674475987`, `0.000432879948454`, and `0.000387158120674`.

## D044 — Qualified reduced Overthrust production record

**Status:** Accepted and verified, 2026-09-09

The final production case is the unchanged 200 x 200 x 187 canonical model,
121-receiver acquisition, and 3000-sample 1 ms configuration from D038 and
D041. It completed on the target RTX 5060 in 207.773 s of propagation. The
live preflight allowed 6,078,123,212 bytes and the conservative plan required
2,710,794,920 bytes.

The output directory contains exactly one 4,446,720-byte `record.sgy`. An
independent standard-library parser validates all Revision 1, IEEE float,
sampling, trace-order, component, coordinate, and source headers; all
1,089,000 samples are finite, 1,015,337 are nonzero, and VX/VY/VZ each have
positive energy. All 121 first-significant arrivals pass the frozen
straight-line travel window. Full Release, focused sanitizer, CPU-off-option,
and adapter-combination regressions pass. This closes only the derived
Overthrust elastic-forward increment.

## D045 — Local scientific-data directory contract

**Status:** Accepted and implemented, 2026-09-14

Every local dataset under `data/<dataset>/` separates immutable downloaded
volumes in `source/`, prepared HDF5 volumes in `models/`, dataset-level model
plots in `figures/model/`, and run artifacts in `runs/<run_id>/`. Every run
uses the same `config.yaml`, `output/`, `figures/`, `logs/`, and `reports/`
layout. Checksums and inventories stay in `manifests/`.

Run configurations resolve model and output paths relative to their own
directory, so the complete data tree can move with the repository. Build
trees, helper source, and executables belong under the repository-level
`build/` directory. Large scientific data remains ignored by Git, while
`data/README.md` is versioned as the directory contract.

## D046 — Stateful CUDA forward ownership for interactive consumers

**Status:** Accepted and verified, 2026-09-15

The production CUDA state is now owned by a file-format-independent
`CudaForwardSession`. A caller chooses a positive whole-step batch size; the
session synchronizes at the batch boundary and advances its completed-step
metadata only after successful synchronization. The CLI uses one full batch,
while a future desktop controller may use short batches for cooperative
control. The session exposes only const device wavefield views and permits
final trace download only after completion. It owns no thread, GUI state,
snapshot policy, or RTM behavior.

Direct CUDA composition and uneven session batches matched bitwise in all nine
fields and all traces. The dense Overthrust SEG-Y also remained byte-for-byte
identical to the accepted output.

## D047 — One aligned physical scalar is the solver-render boundary

**Status:** Accepted and verified, 2026-09-15

The solver-side visualization boundary is one reusable full-resolution
`float32` physical volume in canonical `[z][y][x]` order. It excludes halo and
absorbing cells. Supported initial fields are centred Vx, Vy, Vz, speed,
divergence, and curl magnitude. Face velocities and natural edge-lattice curl
components are interpolated to one integer lattice before scalar formation.

Filtered downsampling, buffering, CUDA/OpenGL interop, render scheduling,
colour/opacity mapping, screenshots, videos, and scientific snapshots remain
outside this boundary. This prevents transient display policy from entering
the wave solver and prevents an unfiltered decimation path from becoming a
scientific output convention.

The future desktop controller must account for all persistent display volumes
through the existing forward memory plan's `workspace_bytes`. Three target-grid
volumes consume 89,760,000 bytes (about 85.6 MiB); this cost is therefore part
of the same preflight rejection decision as the solver state and traces.

## D048 — Accepted first-release desktop product boundary

**Status:** Accepted, 2026-09-15

The desktop target is a default-off Qt 6 Widgets/OpenGL application in the
Wave3D repository and is qualified for native Linux with NVIDIA CUDA. Its
default layout is one large 3-D view plus stacked XY/XZ/YZ views. All views use
one synchronized scalar selection, initially velocity magnitude, switchable to
Vx, Vy, Vz, divergence, or curl magnitude. Model and wavefield are separate
render layers. The first release exports screenshots but no video and exposes
a disabled wavefield-snapshot command with a future storage seam.

The first-release visual language is a restrained modern dark scientific
workstation. Theme colors, typography, spacing, and widget treatment are
centralized so later visual changes do not couple to experiment modules.

HDF5 and explicitly described regular-volume IEEE-float SEG-Y Vp/Vs/rho inputs
are supported model paths. Source editing includes explosion, manual symmetric
moment tensors, and strike/dip/rake double-couple conversion. Acquisition tools
cover rectangular arrays, lines, explicit coordinates, CSV, templates, and
geometry translation. A sequential single-GPU run queue is optional.

## D049 — Multi-shot schema and immutable source models

**Status:** Accepted, 2026-09-15

A forward experiment owns one or more shots. Every shot owns a source,
references or overrides acquisition, and produces separate status, traces,
logs, and checksums. The first UI defaults to one shot but includes a basic
shot table so future RTM does not require replacing the experiment schema.
Multiple experiments and shots execute sequentially on the single GPU.

Imported models are immutable. Cropping, property derivation, smoothing,
resampling, and later region edits produce derived models with ordered
operation history and new checksums. The first release implements inspection,
crop, and reproducible Vp-to-Vs/density derivation; direct voxel painting is
deferred.

## D050 — Versioned desktop project and immutable run preparation

**Status:** Accepted, 2026-09-15

Desktop project metadata uses `wave3d.desktop.project.v1` JSON and lives at
`project.wave3d.json` in the scientific workspace root. It stores identity and
workflow metadata only; forward solver parameters remain in the existing
versioned YAML format. Project references are safe relative paths so moving a
complete workspace does not invalidate it.

The desktop creates the same source/model/figure/run/manifest directory
contract used by organized datasets. A prepared run retains the exact resolved
YAML bytes and an immutable `wave3d.desktop.run.v1` manifest containing the
project ID, shot ID, run ID, creation time, and configuration SHA-256. Existing
project directories and run IDs are rejected instead of merged or overwritten.

## D051 — Validated CPU model scene precedes GPU volume rendering

**Status:** Accepted and verified, 2026-09-15

Static desktop model inspection consumes `PhysicalModel` through the existing
`wave3d.model.v1` HDF5 adapter. The scene owns the validated CPU arrays in
canonical `[z][y][x]` order and produces central XY, XZ, and YZ images. XY
places increasing y upward; XZ and YZ place positive z downward. Vp, Vs, and
density each use their own full-volume range with one sequential three-stop
map from `(8,29,55)` through `(23,132,160)` to `(250,221,90)`.

An external model is copied byte-for-byte into `models/` using a temporary
file and atomic publication. An existing destination is never overwritten,
and the project stores only the safe relative reference after the copied model
has loaded successfully. The 3-D viewport temporarily presents the same
center-XY image with an explicit static-preview label. This establishes the
model/indexing contract without implying that 3-D texture upload, ray casting,
interactive slices, or crop editing are implemented.

## D052 — Crops use half-open source indices and a reversible local origin

**Status:** Accepted and verified, 2026-09-15

The scientific crop contract uses zero-based half-open x/y/z ranges. The UI
shows inclusive endpoints and converts once at its boundary. A derived model
keeps the source spacing, halo, and absorbing-boundary metadata while its local
physical coordinates restart at `(0,0,0)`. The derivation manifest records the
source begin index and its meter offset on every axis, making the mapping back
to the source volume explicit.

Each crop creates a new HDF5 under `models/` and a
`wave3d.desktop.model_derivation.v1` manifest under `manifests/models/`. Both
source and output SHA-256 values are recorded. The HDF5 is reread and compared
before atomic rename; the project reference changes only after the HDF5 and
manifest exist. Imported and earlier derived models are never overwritten.

## D053 — Static volume rendering uses a normalized presentation copy

**Status:** Accepted and verified, 2026-09-15

The static 3-D renderer consumes one independent `float32` copy of the selected
Vp, Vs, or density array in canonical `[z][y][x]` order. It normalizes against
that property's full-volume extrema, maps a constant property to `0.5`, and
uploads one `GL_R32F` texture. Normalization, colour, opacity, threshold, camera,
and crop clipping are presentation state; the validated SI-valued
`PhysicalModel` remains unchanged.

The OpenGL 3.3 fragment shader ray marches front to back through a box scaled
by node-to-node physical extents. Its texture mapping places `z=0` at the top
and positive model depth downward. Inclusive UI crop endpoints are converted
to normalized texture limits without creating a cropped volume, while explicit
derived-model creation retains the accepted half-open scientific contract.

Static model upload is deliberately CPU-to-OpenGL and occurs only when the
selected material property or model changes. It does not define the future
live wavefield transport: that path will consume the existing CUDA physical
visualization volume and requires a separately measured buffering and
CUDA/OpenGL interoperability decision.

## D054 — Experiment drafts bind to immutable model geometry

**Status:** Accepted and verified, 2026-09-15

Desktop experiment editing uses one mutable, versioned JSON draft per shot at
`source/<shot-id>.experiment.json`. It stores the active model reference plus
time, numerical, physical source, Ricker, and moment parameters. Saving uses
atomic replacement and cannot change the project document, imported model, or
an immutable prepared run.

The active HDF5 model remains authoritative for physical dimensions, spacing,
halo, absorbing widths, and the free/absorbing top boundary. Those values are
shown read-only because the production runner requires exact grid equality
between configuration and model. Changing them requires an explicit derived
model or preparation operation rather than an inconsistent run draft.

A draft is saveable only after its active source mode resolves through
`prepare_moment_tensor_source` and its `SimulationConfig` passes the existing
radius-six CFL and design-band validation. Isotropic explosion and explicit
six-component symmetric tensors are supported. Strike/dip/rake conversion
remains a visible disabled option until its convention and formula receive a
separate scientific test. Receiver geometry and complete forward YAML are not
invented by the draft layer.

## D055 — Surface acquisition resolves before immutable run preparation

**Status:** Accepted and verified, 2026-09-15

The version-2 per-shot experiment draft adds one rectangular surface receiver
grid in physical metres. It records x/y counts, inclusive physical endpoints,
and the fixed `z=0 m` depth. Receiver generation is y outer and x fastest, uses
checked products, rejects more than 1,100,000 desktop receiver points, and
validates every generated coordinate through the shared physical-grid mapping.
Version-1 drafts remain readable but carry no invented geometry; the desktop
adds model-derived `101 x 101` defaults before the draft can resolve or save.

The current production trace contract is receiver-major Vx/Vy/Vz float32 with
SEG-Y Revision 1 output. Desktop estimates therefore report both raw
three-component sample bytes and exact uncompressed SEG-Y bytes. A default
time step is rounded downward to an integer microsecond so it remains CFL-safe
and SEG-Y representable. Explicit non-integer-microsecond intervals and more
than 65,535 samples are rejected by the existing SEG-Y validator.

Preflight is available only when HDF5, YAML, and SEG-Y adapters are all built.
It creates a new immutable run directory, writes the model-relative path and
run-local output path into the established `wave3d.forward.v2` configuration,
then reloads and compares its canonical YAML. Preflight does not start CUDA;
worker ownership, progress, cancellation, and live frames require a separate
increment.

## D056 — Desktop forward execution owns CUDA on one cooperative worker

**Status:** Accepted and verified, 2026-09-16

The complete CUDA/HDF5/YAML/SEG-Y desktop build creates one incremental
production job on a `QThread`. That thread exclusively owns model loading,
CUDA allocation, bounded propagation, trace download, SEG-Y writing, and CUDA
teardown. The main thread reads copied lifecycle snapshots and controls the
worker with mutex-protected pause, resume, and stop requests. Requests are
consumed between synchronized one-step batches; no thread or kernel is forcibly
terminated.

The prepared YAML and `wave3d.desktop.run.v1` manifest stay immutable. Success
writes `record.sgy.tmp`, verifies its exact dimensions and essential Revision 1
headers, then atomically publishes `record.sgy`. A separate, atomically written
`wave3d.desktop.run_result.v1` records the terminal state. Only completed runs
may claim an output, and that claim includes the run-relative path, byte count,
SHA-256, receiver/sample counts, device, and timing fields. Cancelled or failed
runs retain a diagnostic and cannot expose a partial file as a completed
product.

## D057 — Live views use immutable double-buffered pinned frames

**Status:** Accepted and verified, 2026-09-21

Live wavefield display uses one reusable device scalar volume and two
page-locked host scalar volumes. CUDA extraction and transfer run on the
forward worker after a completed solver step. The worker normalizes into the
same host buffer, publishes it through immutable shared ownership, and never
waits for the GUI to release an older frame. When both buffers are occupied,
the display frame is dropped while propagation and receiver sampling continue.

One sequence identifies the 3-D volume and all three orthogonal sections.
Vx/Vy/Vz and divergence use a symmetric zero-centred scale; speed and curl
magnitude use a nonnegative scale. The frame retains the physical range and SI
unit so presentation normalization is reversible. The static material texture
and live wavefield texture remain separate, and terminal run states remove the
live layer before restoring the static scene.

The compatibility baseline is pinned host staging followed by
`glTexSubImage3D`, not CUDA/OpenGL resource registration. Three five-step real
`200 x 200 x 187` Overthrust runs on the RTX 5060 Laptop GPU measured
`67.146–67.886 ms` mean propagation per step and `60.032–82.135 ms` for the
complete display path. The calculated 10%-overhead intervals were 9, 10, and
13 steps. The UI rounds this to a conservative 15-step default while allowing
users to select 1 through 100 steps.

## D058 — Three-component records use three interoperable SEG-Y files

**Status:** Accepted and verified, 2026-09-21

Production output uses three SEG-Y Revision 1 files named `record_vx.sgy`,
`record_vy.sgy`, and `record_vz.sgy`. Each file is a self-contained big-endian
IEEE float32 common-source ensemble with one fixed-length trace per receiver,
no extended textual headers, SI geometry, and trace identification code 13,
14, or 12 respectively. This broadly supported Revision 1 subset is preferred
over newer optional extensions because the exchange contract must open in
mainstream seismic viewers and processing packages.

The solver still samples all components together. Splitting happens only in
the SEG-Y adapter after trace download, so numerical propagation and sample
values are unchanged. Production writes and validates all temporary members
before publication and removes the complete new set if any member fails. The
desktop result schema is `wave3d.desktop.run_result.v2` and records a
component-labelled path, byte count, and SHA-256 for every member.

## D059 — Results inspection is read-only and range-bounded

**Status:** Accepted and verified, 2026-09-21

The first desktop results workspace reads only completed
`wave3d.desktop.run_result.v2` products inside the active project. It validates
the declared three-file Revision 1 layout and shared sample axis at open, then
validates component identity and cross-file receiver geometry for every trace
in the selected range before presentation. At most 512 contiguous
receiver traces from one selected component are loaded for a view, preventing
a dense acquisition from becoming an unbounded GUI allocation.

Gather colours use a display-only 99.5th-percentile symmetric clip and retain
particle velocity units and the applied clip in the figure. The only export is
PNG. SEG-Y files, checksums, manifests, and terminal result records remain
immutable. Processing, picks, resampling, and run-to-run amplitude arithmetic
require separate scientific contracts.

## D060 — Complete the single-shot desktop path before multi-shot workflow

**Status:** Accepted, 2026-09-21

The active desktop milestone is narrowed to a complete single-shot forward
workflow. Multi-shot table operations, shot CSV import, and sequential queue
execution are postponed. Stable shot identities, per-shot experiment paths,
SEG-Y shot metadata, and queue-preference fields remain compatible so the
postponed workflow can be added without replacing persisted projects.

Remaining delivery is source mechanisms, acquisition geometry, SEG-Y property
model conversion, and single-shot release qualification. Each increment uses
the risk-based validation matrix in `INCREMENT_EXECUTION_POLICY.md`: focused
checks are sufficient for narrow changes, while scientific, cross-boundary,
and release changes widen validation. This changes scheduling and test
repetition only; it does not relax the scientific validation ladder or permit
untested code to be reported as complete.

## D061 — Double-couple input uses Aki–Richards angles in END storage order

**Status:** Accepted and verified, 2026-09-21

The desktop accepts a positive scalar moment in N·m, strike clockwise from
north in `[0,360)` degrees, dip downward from horizontal in `[0,90]` degrees,
and rake from strike toward down-dip in `[-180,180]` degrees. The standard
north-east-down double-couple equations are evaluated first and then permuted
to Wave3D's `x=east, y=north, z=down` tensor order. No additional sign change is
applied; the existing tension-positive stress-injection contract remains
authoritative.

The experiment schema becomes `wave3d.desktop.experiment.v3` and persists the
four defining double-couple values. Versions 1 and 2 remain readable with safe
inactive-mode defaults. The resolved source still contains only the general
symmetric tensor, so propagation and output adapters do not depend on focal
mechanism parameterization.

## D062 — Acquisition modes resolve to one immutable ordered receiver list

**Status:** Accepted and verified, 2026-09-21

The active shot may define a surface rectangular array, a surface line, or
ordered explicit coordinates. CSV input must use `x_m,y_m,z_m`; imported rows
are stored in the experiment document rather than referenced by path. A finite
X/Y translation is applied to every generated/imported point before the common
surface, model-boundary, safety-count, and exact-duplicate validation. The
resolved row/generation order is the receiver and SEG-Y trace order.

Experiment schema v4 owns the acquisition variant and keeps versions 1–3
readable. Standalone acquisition-template v1 files store geometry and
translation without shot, source, model, or run identity. Templates are
atomically written and may be reused across projects; applying one still
requires validation against the active model before save or preflight.

## D063 — SEG-Y property volumes require declared geometry and provenance

**Status:** Accepted and verified, 2026-09-21

Desktop model conversion accepts three independent fixed-length SEG-Y files for
Vp, Vs, and density. The supported subset is big-endian IEEE float format code
5 with exactly `nx*ny` traces and `nz` samples per trace. Trace order is x fast
then y; samples increase with z. Values are already SI (`m/s`, `m/s`, and
`kg/m3`); the converter performs no unit conversion or resampling.

The user declares dimensions, metre spacing, halo, and all six absorbing
widths. Wave3D does not infer a regular 3-D grid from optional trace headers.
Conversion validates the physical model, verifies a temporary canonical HDF5
round trip, refuses overwrite, and publishes a v1 JSON manifest with source
paths, byte counts, source/output SHA-256 values, geometry, units, axis order,
and coordinate convention. Failure removes partial project artifacts; success
activates the HDF5 model through the existing loading boundary.

## D064 — RTM release seam is an optional immutable task/result contract

**Status:** Accepted and verified, 2026-09-21

The first single-shot release contains no RTM execution. When
`WAVE3D_ENABLE_RTM=ON`, the optional tree exposes a task lifecycle interface,
cooperative stop request, immutable result ownership, and named unit-bearing
physical-grid image const views. It also retains the checkpoint store seam.
The interfaces do not select a reverse propagator, imaging condition,
checkpoint policy, P/S decomposition, image persistence, or desktop workflow.

With `WAVE3D_ENABLE_RTM=OFF`, neither header is reachable through a build
target and forward targets remain unchanged. The disabled desktop snapshot
action explicitly states that the current version writes no snapshots.

The D060 single-shot schedule also defers the generic desktop Vp-to-Vs/density
derivation editor. The established Overthrust conversion and immutable crop
workflow remain available; broader derived-model operations retain the D049
provenance requirement and require a later increment.

## D065 — V2 selection is one typed, UI-independent state contract

**Status:** Accepted and verified, 2026-09-22

`SelectionController` is the sole owner of the V2 current selection. Its value
uses `SelectionKind` rather than labels or string type tags and contains only
stable identities plus optional project, run, and model-property scope. It
contains no widget, solver, renderer, or raw object pointer. Equal values do
not produce duplicate notifications.

`MainWindowShell` owns the controller's QObject lifetime and routes typed
Navigator page contexts to it. The shell does not derive selection meaning
from page titles. The legacy `MainWindow` supplies placeholder page identities
and replaces their project scope when a real project opens. This bridge does
not move project, experiment, run, result, or visualization ownership.
`ForwardRunState` remains the only desktop run-lifecycle enum.
