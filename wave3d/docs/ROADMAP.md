# Wave3D Incremental Roadmap

Every increment stops at its acceptance gate. The next increment must not begin
in the same change merely because time remains. Wave3D implements a 3D
isotropic **elastic** forward solver; viscoelastic attenuation is out of scope.

## Scientific reference gate — required before Increment 4

**Goal:** Establish an auditable elastic numerical specification before coding
the propagation equations.

**Work:**

- Record the Zhang et al. 2020 paper metadata and the limits of available text.
- Index supplied legacy definitions, call sites, missing kernels, parameters,
  dependencies, coordinate conventions, and apparent defects.
- Select primary references for the 3D first-order isotropic elastic equations
  and staggered-grid operator where the linked paper is unavailable or silent.
- Document every equation, symbol, unit, staggered location, coefficient,
  update order, time level, stability bound, and source convention.
- Define small analytical tests before writing the propagator.

**Acceptance:** Every implemented numerical equation has a primary source or an
explicitly marked derivation; legacy reuse and licensing boundaries are clear;
the CPU reference test plan is reviewable without reading implementation code.

**Status:** Passed on 2026-09-08. `ELASTIC_NUMERICAL_SPEC.md` fixes the
interior equations, units, stress/source signs, component staggering, standard
12th-order radius-six coefficients, second-order leapfrog time levels,
heterogeneous coefficient averages, exact interior CFL bound, design-band
dispersion rules, and analytical CPU test plan. `LEGACY_AUDIT.md` records the
supplied and deleted historical source, its limitations, hashes, and licensing
boundary. Propagation code was not added during this gate.

## Increment 1 — CPU-only core skeleton

**Goal:** Verify grid conventions and configuration checks independently of
CUDA.

**Work:**

- Configure with CMake and a modern C++17 compiler on the target Linux machine.
- Build with warnings enabled and run CTest.
- Verify dimensions, `[z][y][x]` indexing, checked arithmetic, time-step count,
  finite configuration values, elastic material extrema, and boundary/config
  consistency.
- Keep the CFL helper explicitly provisional until the scientific reference
  gate establishes the final staggered-grid coefficients.
- Record exact environment, commands, and results in `docs/HANDOFF.md`.

**Acceptance:** Clean configure/build; all tests pass; expected `200^3`
physical count and `252 x 252 x 232` sample allocation are confirmed; invalid
indices, non-finite or invalid material/time values, inconsistent top-boundary
settings, unstable sample `dt`, and arithmetic overflow are rejected.

**Status:** Verified on the target Linux machine on 2026-09-08.

## Increment 2 — CUDA foundation and memory planning

**Goal:** Establish safe GPU ownership without wave propagation.

**Work:**

- Enable CUDA as an optional CMake language.
- Add CUDA runtime error translation and launch checks.
- Add move-only RAII `DeviceBuffer<T>` and controlled host/device copies.
- Discover device name, compute capability, driver/runtime versions, and
  total/free VRAM through a programmatic interface.
- Implement a field-by-field elastic forward memory plan for the padded grid.
- Add a trivial allocation/copy/fill kernel test.

**Acceptance:** CPU-only build still works; CUDA tests detect the RTX 5060;
allocation and move tests pass under compute-sanitizer; deliberate oversized
plans fail before allocation; no raw allocation occurs outside the documented
ownership layer.

**Status:** Verified on the target RTX 5060 on 2026-09-08. CPU-only and CUDA
builds pass independently; Compute Sanitizer reports no memcheck, initcheck,
racecheck, or synccheck errors.

## Increment 3 — Model and acquisition domain objects

**Goal:** Prepare deterministic elastic inputs without external libraries.

**Work:**

- Add physical-coordinate and padded-grid mapping types.
- Add homogeneous and horizontal-layer model generators.
- Validate `Vp`, `Vs`, and density arrays, units, and per-cell relationships.
- Add a Ricker source-time function.
- Add a symmetric moment-tensor description and isotropic explosion preset.
- Generate a regular three-component receiver grid.

**Acceptance:** Deterministic unit tests pass; sources/receivers outside the
physical domain fail; generated models have correct layers and extrema; moment
tensor and coordinate conventions are printed in resolved metadata.

**Status:** Verified on the target Linux machine on 2026-09-08. CPU-only,
CUDA-enabled, and sanitizer builds pass. The later scientific reference gate
resolved source injection sign, normalization, and staggered placement before
propagation work began.

## Increment 4 — CPU elastic reference

**Goal:** Create a slow, transparent reference implementation.

**Work:**

- **4a:** implement and test exact FD constants, elastic coefficient
  preparation, CFL, and design-band validation.
- **4b:** implement the CPU staggered derivative with polynomial,
  convergence, and lattice-offset tests.
- **4c:** implement wavefield ownership and manufactured one-step stress and
  velocity updates.
- **4d:** implement normalized moment-source injection and component-specific
  receiver interpolation.
- **4e:** run homogeneous symmetry, P/S arrival, polarity, energy, determinism,
  and sanitizer tests in a pre-boundary time window.

Each sub-increment is a separate compile/test/commit gate. Use a small grid and
the deliberately unqualified boundary treatment documented in the numerical
specification.

**4a status:** Verified on 2026-09-09. Exact rational constants and moments,
the spectral maximum, coefficient preparation and constant halo extension,
exact CFL/design thresholds, resolved numerical metadata, `float32` conversion
failures, memory planning, CPU/CUDA builds, ASan/UBSan, and all four Compute
Sanitizer tools pass. No derivative or time update was added; 4b is next.

**4b status:** Verified on 2026-09-09. The checked pointwise CPU derivative
implements both stagger mappings on x, y, and z. Constants, affine fields,
degrees 0–12, every positive/negative impulse offset, complete-stencil ranges,
`float32` input, invalid sizes/boundaries, and smooth-wave 12th-order
convergence pass. No wavefield or time update was added; 4c is next.

**4c status:** Verified on 2026-09-09. Move-only nine-component `float32`
wavefield ownership and separate source-free stress/velocity updates implement
all accepted normal and cross derivatives. Manufactured affine fields verify
six stress and three velocity equations, additive updates, time-step/layout
rejection, unchanged incomplete-stencil storage, and `float32` overflow
handling. No source, receiver, boundary hook, or propagation driver was added;
4d is next.

**4d status:** Verified on 2026-09-09. Fixed eight-node trilinear stencils are
prepared separately for all six stress components and all three particle-
velocity components. Source injection uses the accepted tension-positive sign,
cell-volume normalization, one deposition per off-diagonal component, and
`q(n*dt)`. Receiver sampling restores affine fields on each face lattice,
labels output `(n+1)*dt`, and writes to preallocated storage. Weight sums,
volume integrals, signs, unavailable support, invalid state, CPU/CUDA builds,
ASan/UBSan, and all four Compute Sanitizer regressions pass. No multi-step
driver or physical propagation claim was added; 4e is next.

**4e status:** Verified on 2026-09-09. The minimal CPU step now composes the
accepted stress update, `q(n*dt)` source injection, explicit no-op boundary
positions, velocity update, and `(n+1)*dt` three-component sampling. A
predeclared homogeneous test passes combined-dispersion P/S arrival tolerances,
outward explosion polarity, opposite-axis and cubic symmetry, isotropic
transverse leakage, post-source pre-boundary energy, bitwise determinism, and
zero time-loop allocations. CPU Release, CUDA-enabled Release, ASan/UBSan, and
all four Compute Sanitizer gates pass. Increment 4 is complete; Increment 5
CUDA propagation is next.

**Acceptance:** Derivative tests converge at the documented order; homogeneous
wavefront symmetry and theoretical P/S arrival times meet stated tolerances;
energy remains stable after the source in a controlled test; update time levels
and staggering are documented.

## Increment 5 — CUDA elastic propagator

**Goal:** Match the CPU elastic reference on one GPU.

**Work:**

- Implement clear, initially separate CUDA stress and velocity kernels.
- Reuse the CPU coefficients, update order, source mapping, and sampling.
- Add small-grid full-field CPU/GPU comparisons.
- Avoid per-step allocations and unnecessary synchronizations.

**Acceptance:** CPU/GPU fields agree within documented `float32` tolerances for
multiple time steps; compute-sanitizer reports no memory errors; homogeneous
physics tests pass; timings and peak memory are recorded.

**Status:** Verified on the target RTX 5060 on 2026-09-09. Separate CUDA
stress, source, velocity, and receiver kernels reproduce the CPU reference
bitwise for a nonzero nine-component manufactured state and an eight-step
source/trace case (fixed normalized maximum error limit `2e-5`). All device
state is move-only and preallocated; the direct eight-step measurement was
`400 us` with `1,419,864 bytes` of deterministic owned device storage. CPU and
CUDA Release, ASan/UBSan, and all four Compute Sanitizer gates pass. Increment
6 sponge preparation and application are next.

## Increment 6 — Sponge boundary

**Goal:** Obtain a stable finite-domain elastic baseline.

**Work:** Implement a replaceable multiplicative sponge, including corners,
with separate preparation and application interfaces.

**Acceptance:** Measured boundary reflections fall below a documented early
debug threshold; the interior solution matches the reference window before
boundary arrivals; long runs remain finite.

**Status:** Verified on the target RTX 5060 on 2026-09-09. A quadratic
separable multiplicative sponge is prepared once and applied at separate
stress and velocity hooks; face factors multiply across edges and corners.
CPU/CUDA application was bitwise equal, the pre-boundary trace error was zero,
the first-return peak was `1.0851%` of the undamped result versus the fixed
`12%` debug limit, and all fields/traces remained finite for 600 steps. CPU and
CUDA Release, ASan/UBSan, and all Compute Sanitizer gates pass. Increment 7
six-sided CPML is next.

## Increment 7 — CPML on six sides

**Goal:** Replace the debug sponge with an optional production absorber.

**Work:** Implement coefficient preparation and auxiliary state separately from
the interior propagator; cover faces, edges, and corners; include CPML memory in
the planner.

**Acceptance:** Normal and grazing-incidence reflection tests meet documented
tolerances; long elastic runs remain stable; turning CPML off does not alter
core propagation interfaces.

**Status:** Verified on the target RTX 5060 on 2026-09-09. The unsplit
convolutional recurrence has separately prepared integer/half-axis
coefficients and 18 explicit derivative-memory fields. CPU/GPU fields, state,
and traces matched bitwise; normal reflected amplitude was `0.02238%` of the
undamped return and the near-grazing translated-reference residual was
`2.27e-6`, both below fixed limits. The 800-step case remained finite and the
unchanged boundary-free interface remains available. CPU/CUDA Release,
ASan/UBSan, and all four Compute Sanitizer gates pass. Increment 8 replaces
top CPML with a traction-free surface.

## Increment 8 — Traction-free top

**Goal:** Support the intended surface-acquisition physics.

**Work:** Replace top CPML with a free surface while retaining CPML on five
sides; handle all staggered stress/velocity locations; position receivers
consistently relative to the surface.

**Acceptance:** Surface traction conditions hold numerically; reflection
polarities and arrival times agree with reference cases; no surface instability
appears in long runs.

**Status:** Verified on the target RTX 5060 on 2026-09-09. Radius-six
integer/half-z ghost projection enforces exact normal and interpolated shear
traction while five-side CPML remains active. CPU/CUDA projection matched
bitwise. The normal-incidence surface velocity retained upward polarity,
arrived within one sample of the full-space reference, correlated `0.98922`,
and had amplitude ratio `2.05877`; 600 steps remained finite. CPU/CUDA
Release, ASan/UBSan, and all focused Compute Sanitizer gates pass. Increment 9
production I/O is next.

## Increment 9 — Production input/output

**Goal:** Add file formats without coupling them to propagation.

**Work:**

- YAML configuration with resolved-configuration output.
- HDF5 `Vp`, `Vs`, density, trace, and sparse-snapshot adapters.
- CSV irregular receiver geometry.
- SEG-Y VX/VY/VZ writers with coordinate and sampling headers plus sidecar
  metadata.
- Optional converter from external SEG-Y models to canonical HDF5.

**Acceptance:** Round-trip tests preserve shapes, values, units, axes, exact
`dt`, coordinates, source metadata, and component orientations; propagator tests
run without enabling HDF5 or SEG-Y.

**Status:** Verified on 2026-09-09. Independent CMake options now expose typed
YAML configuration, strict receiver CSV, canonical HDF5 model/trace/sparse-
snapshot storage, three IEEE-float SEG-Y component writers with exact JSON
sidecars, and an optional fixed-layout SEG-Y-model-to-HDF5 converter. Enabled
Release round trips passed 18/18 tests and focused ASan/UBSan passed 4/4; with
all optional I/O disabled, CPU Release passed 15/15 and CUDA Release passed
20/20. Increment 10 target qualification is next. Increment 12 later
superseded the three-file SEG-Y output contract without changing the other
Increment 9 adapters.

## Increment 10 — RTX 5060 `200^3` qualification

**Goal:** Establish the supported elastic production envelope.

**Work:** Run representative cases, measure peak VRAM, time per step,
throughput, output overhead, and numerical stability; profile with NVIDIA tools;
optimize only measured bottlenecks.

**Acceptance:** The full case completes inside the configured VRAM safety
margin with no invalid accesses or NaNs; physics regressions still pass after
optimization; a reproducible performance report is committed without large
generated data.

**Status:** Verified on the target RTX 5060 on 2026-09-09. The actual
`200^3`, 4000-step five-side-CPML/free-surface case completed in `292.785 s`
at `73.196 ms/step`, used `2023.693 MiB` of owned device memory, stayed inside
the conservative 80% plan by over 3 GiB, retained exact final surface
traction, and contained no non-finite values. Target-size memcheck reported
zero errors. Nsight identified deliberate FP64 work in the two CPML main
kernels as the bottleneck; no accuracy-changing optimization was made.
Increment 11 interface finalization is next.

## Increment 11 — RTM-ready interfaces only

**Goal:** Prove RTM can be added and removed cleanly without implementing it.

**Work:** Finalize read-only wavefield views, propagator factory,
receiver-data reader, observer hooks, checkpoint-store interface, and optional
CMake boundaries. Add a small mock checkpoint observer.

**Acceptance:** The forward executable is unchanged when RTM support is off;
removing optional checkpoint/imaging directories does not break core builds;
mock save/restore metadata is deterministic. Stop unless the user explicitly
starts a separate RTM phase.

**Status:** Verified on 2026-09-09. Nine-field const views, deterministic
completed-step observer metadata, an allocation-free-step CPU reference
factory, and format-independent receiver reads are now core interfaces. The
checkpoint store and interval observer exist only under the default-off RTM
tree. Mock save/restore is bitwise deterministic; removing that whole tree did
not affect the RTM-off build, and the forward executable hash was unchanged.
RTM-on Release passed 17/17, interface ASan/UBSan 2/2, final CPU Release 16/16,
CUDA Release 21/21, and an all-options-on Release build 25/25. The elastic
forward roadmap is complete; stop here unless the user explicitly authorizes a
separate research phase.

## Increment 12 — Single-file standard SEG-Y output

**Goal:** Make one standards-based SEG-Y file the external output for a
complete three-component synthetic receiver record.

**Work:** Replace the separate component writers and JSON sidecars with one
big-endian Revision 1 IEEE-float file; interleave VX, VY, and VZ per receiver;
correct the component, coordinate-scalar, elevation, depth, ensemble, and
sampling headers; embed the interpretation in the textual header; and expose
`.sgy` output from the qualified GPU executable.

**Acceptance:** Independently parsed raw bytes prove the standard header
layout, trace order and component codes, coordinates, sampling, and sample
values; invalid fractional-microsecond sampling and truncation fail; the
focused sanitizer and enabled/disabled-I/O regression suites pass.

**Status:** Verified on 2026-09-09. The raw-byte focused test passed; a one-step
target GPU run wrote exactly one 10,188-byte, 27-trace file for nine receivers.
The all-options Release suite passed 25/25, the focused ASan/UBSan I/O suite
passed 4/4, and the optional-I/O-off CPU Release suite passed 16/16. Builds of
the qualified executable also passed with no trace output, HDF5 only, SEG-Y
only, and both adapters enabled.

## Increment 13 — HDF5-to-SEG-Y production pipeline

**Goal:** Run an arbitrary validated canonical HDF5 elastic model from a typed
YAML configuration and write one combined standard SEG-Y receiver record.

**13a status:** Verified on 2026-09-09. YAML schema
`wave3d.forward.v2` replaces the misleading homogeneous-material triple with
six declared heterogeneous model extrema. The focused round trip and old-
schema rejection test pass; Increment 13b production orchestration is now
complete as recorded below.

**13b status:** Verified on 2026-09-09. `wave3d_run CONFIG.yaml` now resolves
configuration-relative paths, validates a canonical heterogeneous HDF5 model
against the YAML grid/extrema, applies the live GPU memory gate, executes the
existing CUDA CPML/free-surface solver, and writes only
`<output_directory>/record.sgy`. A small heterogeneous end-to-end case produced
finite nonzero traces with the correct 14/13/12 component sequence; grid and
extrema mismatches failed before output. All-options Release passed 26/26 and
pipeline memcheck reported zero errors. Focused I/O ASan/UBSan passed 4/4 and
optional-I/O-off CPU Release passed 16/16.

## Increment 14 — Reduced SEG/EAGE Overthrust elastic benchmark

**Goal:** Reproducibly derive one reduced isotropic solid elastic model from
the classic SEG/EAGE 3-D Overthrust P-wave velocity volume and qualify the
existing HDF5-to-CUDA-to-single-SEG-Y production path on it.

**14a status:** Verified on 2026-09-09. The source audit records the official
CC BY 4.0 authority, the advertised S3 object's current HTTP 403 response, the
transport mirror, its 149,938,918-byte count and SHA-256, MATLAB variables,
`187 x 801 x 801` shape, 25 m spacing, 119,979,387 finite values, and
`2178.8345-6000 m/s` extrema. These match independent published model
descriptions. A fixed maximum-horizontal-gradient window selects source
`[z,y,x]=[0:187,153:353,154:354]`, producing a direct, unsmoothed
`187 x 200 x 200` crop. Expected Vp/Vs/density extrema and binary32 hashes are
frozen before converter implementation. Increment 14b conversion is next.

**14b status:** Verified on 2026-09-09. A dependency-free pure model
transformation now accepts canonical source Vp, an explicit crop, and output
storage geometry. It preserves Vp exactly, derives Vs and density using the
frozen binary64 formulas followed by one binary32 conversion, and returns a
validated `PhysicalModel`. Unique-index axis/crop checks, exact formula and
determinism checks, and rejection of truncated, non-finite, non-positive,
underflowing, out-of-range, and overflowing inputs pass. Focused Release and
ASan/UBSan passed, and the optional-I/O-off CPU Release suite passed 17/17.

**14c status:** Verified on 2026-09-09. The prerequisite extrema-semantics gate
now
avoids pairing independent global minimum Vp and maximum Vs as if they were
one cell; actual HDF5 materials remain strictly validated cell by cell. The
default-off MatIO adapter now validates the audited v5 variables, reads only
the selected hyperslab, and explicitly changes MATLAB column-major order to
Wave3D x-fastest order. The real source generated a canonical
`187 x 200 x 200` HDF5 model and matching validated YAML. Independent raw
dataset hashes exactly matched all three frozen preimplementation oracles;
orthogonal slices and summaries passed. Increment 14d CUDA propagation is
next.

**14d status:** Verified on 2026-09-09. The fixed 64-cubed/eight-step
Overthrust smoke
profile completed normally and under Compute Sanitizer memcheck with zero
errors. Its one SEG-Y file had 27 receiver-major 14/13/12 traces, 8 samples at
1 ms, and 216/216 finite samples. Full-model 1 ms versus 0.5 ms refinement
then passed at 0.0434% normalized L2 against the fixed 5% threshold. The full
3000-step run completed in 207.773 s propagation within the live memory gate.
Its only output was a standard 363-trace SEG-Y with 1,089,000/1,089,000 finite
samples, positive three-component energy, and 121/121 first arrivals inside
the conservative travel window. Final regressions passed. Increment 14 is
complete; stop at this one derived Overthrust benchmark.

## Desktop solver preparation phase

## Increment 15 — Stateful CUDA forward session for desktop control

**Goal:** Make the accepted production CUDA solver controllable in whole-step
batches and observable through a const device view without changing numerical
behavior.

**Work:** Add a file-format-independent `CudaForwardSession`, completed-step
metadata, const device-wavefield views, explicit validation downloads, and
production-task composition through the session. Keep Qt, rendering,
snapshots, RTM, and precision/performance kernel changes outside the increment.

**Acceptance:** The fixed checks in
`INCREMENT_15_CUDA_FORWARD_SESSION_PLAN.md` pass, including bitwise direct-vs-
session fields/traces, all repository tests, and byte-identical reproduction
of the accepted dense Overthrust SEG-Y.

**Status:** Verified on 2026-09-15. Uneven whole-step batches matched the
direct CUDA composition bitwise in all nine final fields and all three trace
components. The production runner now uses the session and reproduced the
accepted dense Overthrust SEG-Y byte for byte. CUDA RTM-off passed 28/28,
all-options RTM-on passed 29/29, and CPU-only passed 17/17.

## Increment 16 — CUDA physical-volume visualization extraction

**Goal:** Produce scientifically aligned physical-domain display scalars from
the accepted staggered CUDA wavefield without coupling the solver to a GUI or
renderer.

**Work:** Add one reusable scalar device-volume owner and GPU extraction for
centred Vx/Vy/Vz, speed, divergence, and curl magnitude. Exclude halo and CPML
from the output and retain full physical resolution.

**Acceptance:** The analytic staggered-field, non-mutation, layout, invalid-
input, and build-boundary checks in
`INCREMENT_16_CUDA_VISUALIZATION_EXTRACTION_PLAN.md` all pass.

**Status:** Verified on 2026-09-15. Analytic affine fields on the staggered
lattices produced the expected six physical-domain quantities and extraction
left the nine source fields bitwise unchanged. On the 200 x 200 x 187 target,
one scalar volume is 29,920,000 bytes; measured extraction ranged from about
1.1 ms for a centred component to 30.8 ms for centred curl magnitude.

## Desktop application phase

## Increment 17 — Optional Qt/OpenGL desktop shell

**Goal:** Establish a removable desktop build boundary and the accepted
four-view shell without connecting model data or the CUDA runner.

**Work:** Add default-off `WAVE3D_BUILD_DESKTOP`, a Qt 6 Widgets/OpenGL
application target, named 3-D/XY/XZ/YZ viewports, module navigation, shared
display-field control, run/status placeholders, and focused contract tests.

**Acceptance:** The fixed build-boundary, shell-contract, WSLg development
smoke, and existing regression checks in
`INCREMENT_17_DESKTOP_SHELL_PLAN.md` pass.

**Status:** Verified on 2026-09-15. Qt is consulted only when the default-off
desktop option is enabled. The application now provides the accepted four-view
shell, centralized modern dark theme, Chinese labels, six-field selector,
module navigation, run placeholders, disabled wavefield-snapshot seam, and
deterministic structure/screenshot diagnostics. The desktop/CPU suite passed
18/18, the CUDA/YAML/HDF5/SEG-Y suite passed 28/28, and WSLg created all four
OpenGL contexts. The review screenshot was inspected after installing the
required CJK font. No model data or solver execution is connected yet.

## Increment 18 — Desktop project workspace

**Goal:** Persist a versioned desktop project and create reproducible,
non-overwriting scientific workspace/run layouts without connecting model data
or the solver.

**Work:** Add a Qt Core project store, validated JSON schema, standardized
workspace creation, immutable run preparation, project summary UI, and desktop
settings persistence.

**Acceptance:** The focused persistence, validation, run-manifest, UI-state,
build-boundary, WSLg smoke, and existing regression checks in
`INCREMENT_18_DESKTOP_PROJECT_WORKSPACE_PLAN.md` pass.

**Status:** Verified on 2026-09-15. The versioned Qt Core project store creates
and validates the standardized workspace, persists the initial shot and shared
display preference, atomically saves project JSON, and prepares non-overwriting
run directories with exact resolved YAML bytes and a SHA-256 manifest. The
desktop actions create/open projects, show their identity, reopen the last
project, persist window state, and keep scientific actions gated. A fresh
desktop build passed 19/19, CUDA/YAML/HDF5/SEG-Y passed 28/28, the default
build remained Qt-free, and WSLg OpenGL smoke passed.

## Increment 19 — Static HDF5 model inspection

**Goal:** Load a real validated Wave3D model and display scientifically indexed
static metadata and central orthogonal sections without launching propagation.

**Work:** Add an HDF5-conditional desktop model scene, immutable project import,
Vp/Vs/density selection, model metadata, and synchronized XY/XZ/YZ images.

**Acceptance:** The focused model-scene, project-import, HDF5-off boundary,
clean desktop, WSLg smoke, and existing regression checks in
`INCREMENT_19_STATIC_HDF5_MODEL_PLAN.md` pass.

**Status:** Verified on 2026-09-15. The existing HDF5 adapter now feeds a
validated desktop scene with model metadata, immutable project import,
Vp/Vs/density selection, and correctly oriented central XY/XZ/YZ images. Both
HDF5-on and HDF5-off desktop suites, the full CUDA regression, and the real
Overthrust WSLg capture passed their declared gates. The large viewport is
still an explicitly labelled static preview; volume ray casting is deferred.

## Increment 20 — Interactive model slices and immutable crop

**Goal:** Select linked physical-grid slices and create a reproducible cropped
HDF5 model without modifying its source.

**Work:** Add arbitrary slice indices, crop bounds and overlays, exact
three-property crop extraction, atomic derived-model writing, and a versioned
checksum provenance manifest.

**Acceptance:** The scene/crop/provenance/UI, optional-build, WSLg Overthrust,
and CUDA regression checks in `INCREMENT_20_INTERACTIVE_CROP_PLAN.md` pass.

**Status:** Verified on 2026-09-15. Linked arbitrary sections and crop outlines
now drive exact three-property extraction. Derived HDF5 files and versioned
checksum manifests publish without overwriting sources, and the project
activates the derived model only after verification. All optional-build,
desktop, CUDA, and real Overthrust visual gates passed.

## Increment 21 — Static OpenGL volume rendering

**Goal:** Render the validated static material model as an interactive 3-D
volume while preserving the accepted model, slice, and crop contracts.

**Work:** Add canonical texture preparation, OpenGL 3.3 volume ray marching,
physical aspect, crop clipping, orbit/zoom, and compact transfer controls.

**Acceptance:** The CPU texture contract, desktop state, real WSLg shader and
Overthrust capture, optional-build, and CUDA checks in
`INCREMENT_21_STATIC_VOLUME_RENDERING_PLAN.md` pass.

**Status:** Verified on 2026-09-15. The static scene now prepares an immutable
canonical display copy, and the dedicated OpenGL 3.3 viewport uploads it as a
single-channel 3-D texture for physical-aspect front-to-back ray marching.
Material changes, shared crop bounds, opacity/threshold controls, orbit/zoom,
and camera reset synchronize without changing the scientific arrays. The real
Overthrust shader/upload/frame gate and visual review passed under WSLg/XCB;
HDF5-on passed 22/22, HDF5-off 19/19, desktop-off 17/17, and the complete CUDA
configuration 28/28.

## Increment 22 — Persistent workspace and source draft

**Goal:** Configure one active shot's time controls and moment-tensor source
against the loaded model with immediate scientific validation and durable
draft state.

**Work:** Add a versioned atomic experiment draft, model-locked grid summary,
time/numerical controls, explosion/manual source controls, CFL/dispersion and
coordinate diagnostics, and synchronized source markers.

**Acceptance:** The persistence, resolution, UI-state, source-marker, WSLg,
optional-build, and CUDA checks in `INCREMENT_22_EXPERIMENT_DRAFT_PLAN.md`
pass.

**Status:** Verified on 2026-09-15. A per-shot atomic JSON draft now persists
model binding, time/numerical settings, physical source coordinates, Ricker
parameters, and explosion or manual moment tensors. The editor locks model
grid/storage metadata, validates through existing CFL/dispersion/source
contracts, and synchronizes a source marker across the four views. Real
Overthrust numerical and visual review passed; HDF5-on passed 23/23,
HDF5-off 20/20, desktop-off 17/17, and the full CUDA configuration 28/28.

## Increment 23 — Acquisition geometry and forward preflight

**Goal:** Complete one active shot with surface receiver geometry and publish a
reviewable, immutable production configuration without starting CUDA.

**Work:** Add a version-2 acquisition draft with version-1 migration, default
`101 x 101` surface receivers, checked trace/SEG-Y estimates, acquisition
editing, receiver overlays, production YAML assembly, SEG-Y sample-axis
validation, and immutable preflight publication with YAML readback.

**Acceptance:** The geometry/order, bounds, overflow, persistence, migration,
output estimates, UI state, overlay, HDF5/YAML/SEG-Y preflight, optional-build,
WSLg Overthrust, and full CUDA gates in
`INCREMENT_23_ACQUISITION_PREFLIGHT_PLAN.md` pass.

**Status:** Verified on 2026-09-15. The active shot now owns a validated
rectangular surface acquisition with `101 x 101` defaults, exact
three-component storage estimates, synchronized receiver overlays, and a
complete immutable `wave3d.forward.v2` preflight configuration. Version-1
drafts migrate without invented persisted geometry. The HDF5/YAML/SEG-Y desktop
suite passed 25/25, all optional-build matrices passed, the complete CUDA suite
passed 28/28, and the real Overthrust WSLg capture passed visual and OpenGL
gates.

## Increment 24 — Background CUDA forward execution

**Goal:** Execute one immutable preflight configuration without blocking the
desktop and publish only a verified terminal product.

**Work:** Refactor the production task into an incremental CUDA job; add
one-step worker batches, progress, pause/resume/stop, editor locking,
failure-safe temporary SEG-Y output, and a checksummed terminal result record.

**Acceptance:** The job, worker lifecycle, workspace terminal record, desktop
state, real small CUDA run, optional builds, and complete regression gates in
`INCREMENT_24_BACKGROUND_FORWARD_EXECUTION_PLAN.md` pass.

**Status:** Verified on 2026-09-16. The command-line runner and desktop now
share one incremental production job. The worker owns CUDA entirely off the UI
thread and consumes controls at synchronized one-step boundaries. Successful
runs validate and atomically publish SEG-Y plus a checksummed
`wave3d.desktop.run_result.v1`; cancellation and failure publish terminal state
without claiming an output. The combined Qt/CUDA/HDF5/YAML/SEG-Y suite passed
34/34, including an actual desktop-driven CUDA run, and all affected optional
build boundaries passed.

## Increment 25 — Synchronized live wavefield display

**Goal:** Present completed forward wavefields in the 3-D volume and linked
XY/XZ/YZ sections without compromising solver progress or SEG-Y publication.

**Work:** Add timed CUDA visualization downloads, two persistent pinned host
buffers, immutable frame identity, signed/magnitude normalization, separate
live texture and section overlays, field/opacity/threshold/interval controls,
and a real-target display benchmark.

**Acceptance:** The ownership, extraction, worker buffering, orientation,
four-view synchronization, real CUDA desktop run, Overthrust timing, optional
builds, and regression gates in
`INCREMENT_25_LIVE_WAVEFIELD_DISPLAY_PLAN.md` pass.

**Status:** Verified on 2026-09-21. The worker publishes synchronized immutable
frames and drops only superseded display opportunities. The volume uses a
separate live `GL_R32F` texture while all three sections composite the same
frame sequence. Repeated real Overthrust benchmarks selected a configurable
15-step default; the complete suite passed 35/35 and affected optional builds
passed.

## Deferred research phases

After elastic forward qualification, separate future phases may implement
source/receiver reconstruction, checkpoint/recomputation, imaging conditions,
P/S decomposition, illumination compensation, and a deep-learning relocation
pipeline. They are not acceptance criteria for the current project.
