# Wave3D Architecture

## Architectural objective

Wave3D is a forward-modeling platform whose physics core remains usable when
all RTM, imaging, P/S decomposition, HDF5, SEG-Y, and command-line components
are removed. RTM will be an optional consumer of stable forward interfaces, not
a mode flag spread throughout the solver.

## Dependency direction

```text
apps: forward3d / wave3d_run   optional app: rtm3d
              |                         |
              v                         v
         ForwardTask                  RtmTask
        |                    /         |         \
        v                   v          v          v
Propagator3D          PropagatorFactory  Checkpoints  ImagingCondition
        |
        +---- Constitutive operator: isotropic elastic
        +---- Boundary condition: Sponge, CPML, FreeSurface composition
        +---- Source injector: MomentTensorSource
        +---- Receiver sampler: SurfaceReceiverSet
        |
        v
Numerical/CUDA backend
        |
        v
Core grid, views, checked sizes, errors, diagnostics
```

Dependencies point downward. Core and propagation layers must not include task,
imaging, or file-format headers.

## Planned modules

### `core`

Owns scalar types, `Grid3D`, coordinate conventions, checked arithmetic, error
types, non-owning views, and common metadata. It has no CUDA or file-format
dependency in its public CPU-only subset.

The concrete coordinate API keeps metres (`PhysicalPoint3D`), integer physical
grid indices, fractional physical-grid coordinates, integer padded-storage
indices, and fractional padded-storage coordinates distinct. The physical
domain consists of the grid nodes from zero through `(n-1)d`, inclusively.
Mapping validates the point before adding the halo and lower absorbing width.

### `model`

Separates physical input fields from derived propagation coefficients.

```text
PhysicalModel: Vp, Vs, rho
ElasticCoefficients: lambda, mu, K, face buoyancy, edge shear modulus
```

Generated test models and HDF5-loaded models must produce the same validated
`PhysicalModel` interface.

The current `PhysicalModel` owns unpadded physical arrays shaped `[nz, ny, nx]`
with contiguous x. Homogeneous and horizontal-layer generators fill these
arrays deterministically. Layer interfaces use the first physical z node whose
depth is greater than or equal to the declared layer top. Per-cell validation
requires a finite solid with positive `Vp`, `Vs`, density, shear modulus, and
bulk modulus: `Vp^2 > (4/3)Vs^2`. The accepted coefficient placement and
heterogeneous averages are defined in `ELASTIC_NUMERICAL_SPEC.md`.
Increment 4a prepares nine padded `float32` coefficient volumes. Physical edge
values are extended constantly through all nonphysical storage before face
buoyancies and four-point harmonic edge shear moduli are calculated in double
precision. Values that overflow or underflow their required nonzero `float32`
representation are rejected.

### `wave`

Owns velocity and stress state. Production layout is SoA:

```text
velocity: vx, vy, vz
stress: sxx, syy, szz, sxy, sxz, syz
```

An owning object manages lifetime. `WavefieldView` provides controlled access
to operators, diagnostics, checkpoints, and optional imaging without exposing
ownership.

Increment 4c implements move-only CPU `ElasticWavefield` ownership with nine
zero-initialized padded `float32` SoA arrays. Layout validation checks every
component before an update. Non-owning public views remain planned for later
task, diagnostic, checkpoint, and CUDA interfaces.

### `numerics`

Defines spatial finite-difference operators, staggering, update ordering, and
time integration. Difference coefficients should be compile-time constants or
small constant-memory data after validation. The first correct implementation
may use separate kernels for clarity; fusion is allowed only after reference
tests pass.

The selected interior scheme is a standard radius-six, 12th-order centered
staggered spatial derivative with second-order leapfrog time integration. Its
exact rational coefficients, `I->H`/`H->I` index formulas, spectral radius,
CFL limit, and design-band rules are fixed in
`ELASTIC_NUMERICAL_SPEC.md`. Increment 4 implements that document in five
separately verified sub-increments.

Increment 4a now owns the exact rational constants, their derived double
values, the exact spectral maximum, coefficient-aware CFL calculation, and
design-band reporting/validation. It does not apply a derivative or update a
wavefield; those remain separate gates.

Increment 4b adds a checked pointwise CPU derivative over padded SoA storage.
Callers must select `DerivativeAxis::{X,Y,Z}` and either `IntegerToHalf` or
`HalfToInteger`; the API exposes each mapping's distinct complete-stencil
target range and rejects boundary targets that cannot supply all radius-six
samples. It accepts `float` production fields and `double` validation fields,
accumulates in double, allocates no temporary volume, and still owns no
wavefield or time-update behavior.

### `cuda`

Owns CUDA runtime interaction: error translation, launch checks, device
discovery, and move-only RAII `DeviceBuffer<T>`. Raw `cudaMalloc` and
`cudaFree` calls are confined to this ownership layer. CUDA is an optional
build feature, so CPU core and memory-plan tests do not include CUDA headers.

Increment 5 adds move-only device owners for the nine elastic wavefields, nine
prepared coefficients, fixed moment-source and receiver stencils, and complete
receiver trace storage.  Separate CUDA launches preserve the CPU ordering and
perform no allocation, resize, full-field copy, or synchronization inside a
step.  Traces use receiver-major `[receiver][sample]` storage and retain `dt`
as explicit metadata.

### `physics`

Contains the 3D isotropic elastic constitutive update. Attenuation and
viscoelastic state are intentionally outside the project scope.

Conceptual time step:

```text
update stress from v at integer time n
inject -M*q(t_n)*delta into stress at half time n+1/2
apply relevant stress boundary operations
update particle velocities to integer time n+1
apply velocity boundary operations
sample receivers at time (n+1)*dt
notify diagnostics/output/checkpoint observers
```

Initial state is `v^0=0`, `sigma^(-1/2)=0`. This order and the tension-positive
stress convention are fixed by the numerical specification and must be encoded
in CPU tests before CUDA kernels are written.

Increment 4c implements the source-free interior portions as two separate CPU
calls. The stress call maps `sigma^(n-1/2)` and `v^n` to
`sigma^(n+1/2)`; the velocity call maps `v^n` and `sigma^(n+1/2)` to
`v^(n+1)`. Keeping them separate preserves the future source and boundary hook
positions. Each component is updated only where all of its derivative stencils
are complete; other storage values remain unchanged because no boundary rule
is yet accepted.

Increment 4d implements the operation at the source hook and the sampling
operation after the velocity hook. The source API accepts step index `n` and
therefore samples `q(n*dt)`; the receiver API accepts the same `n` and labels
the post-velocity sample `(n+1)*dt`. It does not yet compose these calls into a
multi-step driver.

Increment 4e adds `cpu_advance_elastic_interior_step`, the minimal composition
of the two updates, source injection, deliberate no-op boundary positions, and
receiver sampling. The caller owns the time loop and all trace/frame storage.
The verified path produces deterministic in-memory three-component records
without allocating inside a step, but it remains a boundary-free CPU reference
rather than a production propagator.

### `boundary`

Provides replaceable boundary components with separate preparation and update
hooks. Early tests may use a multiplicative sponge. The intended production
composition is:

```text
z-min: traction-free surface
x-min, x-max, y-min, y-max, z-max: CPML
```

Free-surface logic must not be hidden inside general interior kernels unless a
later, measured optimization preserves a separately tested reference path.

Increment 6 implements the early debug boundary as one prepared `float32`
damping volume.  Its independently callable stress and velocity hooks leave
the physical model exactly undamped and multiply separable axis factors at
edges and corners.  The CUDA owner uploads the profile once; neither hook
allocates or transfers a volume.  This component remains replaceable and has
no auxiliary state, so it does not stand in for CPML.

Increment 7 adds an optional unsplit CPML path.  Six one-dimensional
integer/half coefficient groups hold `a`, `b`, and `1/kappa`; 18 independently
owned `float32` volumes retain one recurrence memory for every Cartesian
derivative in the elastic equations.  CPML-specific stress and velocity
kernels consume the same wavefield/coefficient objects as the interior path,
so turning CPML off selects the unchanged Increment 5 calls.  Six-side, edge,
and corner behavior follows from correcting each directional derivative
independently rather than special corner code.

Increment 8 composes five-side CPML with a separate horizontal traction-free
surface.  The surface object fixes `z=0` at padded integer plane `halo`,
requires no top absorbing cells, and fills six ghost layers after each stress
or velocity operation.  `szz` is zero on the plane; z-half `sxz`/`syz` are odd
across it; the remaining required ghosts use the documented parity.  Surface
receivers continue through the ordinary component-specific interpolation API.

### `acquisition`

`SourceSet` accepts physical coordinates, origin times, source-time functions,
and symmetric moment tensors `(Mxx, Myy, Mzz, Mxy, Mxz, Myz)`. An isotropic
explosion is represented by equal diagonal terms and zero off-diagonal terms.
The tensor is a scale in N·m; its Ricker function is a moment rate in `s^-1`.
With tension-positive stress, the accepted distribution is
`partial_t(sigma_source)=-M*q(t)*delta`.

`ReceiverSet` supports surface three-component particle-velocity sampling and
eventually arbitrary geometry. Mapping/interpolation weights are prepared once,
then reused during the time loop. Per-step host/device transfers are forbidden.

Increment 3 implements exact fractional coordinate preparation. A regular
surface grid is emitted with x changing
fastest, then y, at `z=0`; all locations are validated before the set is
returned. Source metadata records SI coordinates, tensor order and units,
origin time, moment-rate units, body-force convention, and stress-source sign.

Increment 4d prepares fixed eight-node trilinear stencils separately for the
integer normal-stress lattice, the three shear-stress edge lattices, and the
three velocity face lattices. Complete support is required; preparation never
clips or renormalizes a stencil. Source injection deposits each tensor entry
once as `-dt*Mij*q(n*dt)*w/(dx*dy*dz)` into its own stress field. Receiver
sampling writes `vx`, `vy`, and `vz` into caller-preallocated output and does
not allocate in the time step. Surface receiver physics remains unqualified
until the traction-free boundary in Increment 8.

### `io`

I/O adapters convert external representations into validated domain objects.
Propagation code never opens YAML, HDF5, SEG-Y, CSV, or raw binary files.

Implemented roles:

- YAML: versioned human-readable run configuration, declared model extrema,
  and source/receiver definitions.
- Generated models: initial deterministic debugging.
- CSV: receiver geometry during early development.
- HDF5: canonical internal 3D model, trace, snapshot, and future checkpoint
  format.
- SEG-Y: external three-component seismic trace exchange. Write one file that
  contains all VX, VY, and VZ traces and carries its required interpretation in
  standard textual, binary, and trace headers.

Canonical HDF5 model datasets are shaped `[nz, ny, nx]` and contain
`/vp`, `/vs`, and `/rho` with units and coordinate attributes.
Canonical trace datasets are shaped `[1, nreceiver, nt]` for each component in
the single-source forward format. Sparse snapshots store explicit padded-grid
indices rather than silently implying a dense volume. SEG-Y is one big-endian
Revision 1 IEEE-float common-source file. Its traces are receiver-major and
component-interleaved as VX/in-line code 14, VY/cross-line code 13, and
VZ/vertical code 12. Standard headers declare SI units, axes, ordering,
coordinates, source metadata, and the first-sample convention. Sampling must
be exactly representable in Revision 1's integer-microsecond field. The
separate model converter accepts only fixed-length IEEE-float volumes and
reorders trace-major input into canonical `[z][y][x]` HDF5.

The optional production task is built only when CUDA, YAML, HDF5, and SEG-Y
are all enabled. `wave3d_run CONFIG.yaml` resolves relative paths against the
configuration directory, reads a canonical HDF5 model, requires exact grid and
declared/calculated extrema agreement, applies the existing memory gate, runs
the accepted CUDA CPML/free-surface composition, and writes
`<output_directory>/record.sgy`. This orchestration owns no numerical kernel
and does not add file-format knowledge to propagation.

### `diagnostics`

Provides resolved configuration, environment and Git metadata, NaN/Inf checks,
amplitude extrema, energy histories, phase/timing measurements, memory plans,
and sparse snapshots/slices. Diagnostic observers may read views but may not
control propagation physics.

Increment 4e implements a read-only elastic-energy diagnostic. It sums face
kinetic energy from buoyancy and particle velocity, normal-stress deviatoric
and volumetric energy from `mu` and `K`, and edge shear energy from each
staggered shear modulus. It is used as a bounded pre-boundary stability metric;
because velocity and stress occupy different leapfrog time levels, it is not
claimed as an exact same-time invariant.

Increment 11 adds `IForwardObserver`. It receives validated completed-step
metadata and nine non-owning const field views after receiver sampling.
Registration is allowed only before propagation, so the propagator's time loop
does not resize observer storage.

### `checkpoint`

The `ICheckpointStore` and fixed-interval checkpoint observer are compiled only
when `WAVE3D_ENABLE_RTM=ON`; persistent implementations remain deferred.
Candidate future strategies are full disk snapshots, sparse checkpoints plus
recomputation, boundary reconstruction, and tiered GPU/host/NVMe storage. For an
8 GB RTX 5060, sparse checkpoints plus recomputation are the initial RTM design
candidate.

### Optional `imaging` and `rtm`

The finalized extension boundary provides:

```cpp
IForwardPropagator
make_forward_propagator
IReceiverData
ICheckpointStore
```

The current factory wraps only the transparent CPU interior reference; it does
not misrepresent the CUDA CPML path as a host-view implementation. A future
imaging condition may consume read-only source and receiver views and
accumulate an image. P/S decomposition and illumination compensation remain
future optional strategies. The forward propagator contains no `if (rtm)`
behavior.

## Memory architecture

Production arrays use `float32` SoA storage and RAII device ownership. No array
is allocated merely because a broad feature structure contains a pointer for
it. Forward mode allocates only elastic state; RTM allocates checkpoint and
image state outside the forward object.

Before allocation, a memory planner reports:

```text
physical and padded dimensions
bytes per field
model and coefficient bytes
wavefield bytes
boundary bytes
receiver/output bytes
temporary workspace
estimated CUDA/runtime reserve
total and safety margin
```

The program must reject a plan that exceeds a configurable fraction of
currently available VRAM.

The conservative elastic plan enumerates three physical model fields (`Vp`,
`Vs`, density), nine derived coefficient fields (`lambda`, `mu`, `K`, three
face buoyancies, and three edge shear moduli), nine wavefields, one full-volume
sponge field, three optional receiver-trace arrays, optional workspace, and a
runtime reserve. Later increments must update the plan when actual ownership or
the boundary implementation changes; unimplemented CPML state is not silently
hidden in the current estimate.

The planner now selects `None`, `Sponge`, or `Cpml`.  CPML explicitly adds 18
full padded state fields and six compact groups spanning the three allocated
axis lengths.  For the target `252 x 252 x 232` allocation this boundary state
is `1,060,788,480 bytes`; it is never included when another boundary is
selected.

## Build boundaries

The intended CMake options include:

```text
WAVE3D_ENABLE_CUDA
WAVE3D_BUILD_TESTS
WAVE3D_BUILD_CPU_REFERENCE
WAVE3D_ENABLE_YAML
WAVE3D_ENABLE_HDF5
WAVE3D_ENABLE_SEGY
WAVE3D_ENABLE_RTM
```

`WAVE3D_ENABLE_RTM=OFF` must remove RTM and imaging targets without changing the
forward core. HDF5 and SEG-Y are optional adapters. Early core tests must remain
buildable without them.

The `wave3d_forward_run` task library and `wave3d_run` executable require all
of CUDA, YAML, HDF5, and SEG-Y. Omitting any one removes only those production
targets and leaves the independently useful core, adapter, and qualification
targets intact.

Increment 11 verifies that behavior by configuring and building with the whole
`optional/rtm` tree temporarily absent. RTM-off exposes only the forward views,
observer, factory, and receiver reader; RTM-on adds the header-only checkpoint
interface target and its mock test. There is no RTM executable or imaging
target.

## Numerical verification architecture

A deliberately slow CPU reference will use the same grid convention,
coefficients, source description, and update ordering as CUDA. Small-grid
regression tests compare full fields after selected time steps. Analytical and
physical tests live above implementation-specific unit tests.

Performance optimization begins only after reference comparisons pass. Likely
later optimizations include constant-memory coefficients, shared-memory stencil
tiles where beneficial, kernel fusion, asynchronous output staging, and reduced
temporary storage. Each optimization must preserve a reference path and error
tolerance.

Increment 10 profiling narrows that list: on the target `200^3` case, the two
CPML main kernels consume 99.8% of kernel time and the stress kernel reaches
87.54% compute versus 13.41% memory throughput, dominated by deliberate FP64
accumulation. Output, source, receiver, and free-surface optimization would not
materially improve the current run. Any precision/register/fusion change is a
future measured numerical increment, not part of interface finalization.

## Proposed final source tree

```text
wave3d/
├── AGENTS.md
├── CMakeLists.txt
├── apps/
│   ├── forward3d.cpp
│   ├── run_forward.cpp
│   └── rtm3d.cpp                 # optional, future
├── include/wave3d/
│   ├── core/
│   ├── cuda/
│   ├── model/
│   ├── wave/
│   ├── physics/
│   ├── boundary/
│   ├── acquisition/
│   ├── checkpoint/
│   ├── imaging/                  # optional, future
│   ├── io/
│   └── task/
├── src/
├── cuda/
├── configs/
├── docs/
├── tests/
│   ├── unit/
│   ├── cpu_gpu/
│   ├── physics/
│   ├── regression/
│   └── performance/
└── legacy/                       # reference index or separately obtained source
```
