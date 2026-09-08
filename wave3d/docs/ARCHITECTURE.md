# Wave3D Architecture

## Architectural objective

Wave3D is a forward-modeling platform whose physics core remains usable when
all RTM, imaging, P/S decomposition, HDF5, SEG-Y, and command-line components
are removed. RTM will be an optional consumer of stable forward interfaces, not
a mode flag spread throughout the solver.

## Dependency direction

```text
apps: forward3d              optional app: rtm3d
        |                              |
        v                              v
ForwardTask                       RtmTask
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

Increment 3 implements exact fractional coordinate preparation, not yet an
interpolation stencil. A regular surface grid is emitted with x changing
fastest, then y, at `z=0`; all locations are validated before the set is
returned. Source metadata records SI coordinates, tensor order and units,
origin time, moment-rate units, body-force convention, and stress-source sign.
Increment 4d must prepare separate trilinear stencils for each staggered source
or receiver component; using one array index for all components is forbidden.

### `io`

I/O adapters convert external representations into validated domain objects.
Propagation code never opens YAML, HDF5, SEG-Y, CSV, or raw binary files.

Planned roles:

- YAML: human-readable run configuration and source definitions.
- Generated models: initial deterministic debugging.
- CSV: receiver geometry during early development.
- Raw binary plus JSON metadata: minimal early trace/slice output.
- HDF5: canonical internal 3D model, trace, snapshot, and future checkpoint
  format.
- SEG-Y: external three-component seismic trace exchange. Initially write one
  file per component and include a metadata sidecar where SEG-Y headers cannot
  represent exact semantics safely.

Canonical HDF5 model datasets will be shaped `[nz, ny, nx]` and contain
`/vp`, `/vs`, and `/rho` with units and coordinate attributes.
Canonical trace datasets will be shaped `[nsource, nreceiver, nt]` for each
component.

### `diagnostics`

Provides resolved configuration, environment and Git metadata, NaN/Inf checks,
amplitude extrema, energy histories, phase/timing measurements, memory plans,
and sparse snapshots/slices. Diagnostic observers may read views but may not
control propagation physics.

### `checkpoint`

Defines an optional interface from the start, with implementations deferred.
Candidate future strategies are full disk snapshots, sparse checkpoints plus
recomputation, boundary reconstruction, and tiered GPU/host/NVMe storage. For an
8 GB RTX 5060, sparse checkpoints plus recomputation are the initial RTM design
candidate.

### Optional `imaging` and `rtm`

RTM will depend on interfaces comparable to:

```cpp
IPropagator
PropagatorFactory
IReceiverData
ICheckpointStore
IImagingCondition
```

An imaging condition consumes read-only source and receiver wavefield views and
accumulates an image. P/S decomposition and illumination compensation are
optional strategies under this layer. The forward propagator must contain no
`if (rtm)` behavior.

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

## Build boundaries

The intended CMake options include:

```text
WAVE3D_ENABLE_CUDA
WAVE3D_BUILD_TESTS
WAVE3D_BUILD_CPU_REFERENCE
WAVE3D_ENABLE_HDF5
WAVE3D_ENABLE_SEGY
WAVE3D_ENABLE_RTM
```

`WAVE3D_ENABLE_RTM=OFF` must remove RTM and imaging targets without changing the
forward core. HDF5 and SEG-Y are optional adapters. Early core tests must remain
buildable without them.

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

## Proposed final source tree

```text
wave3d/
├── AGENTS.md
├── CMakeLists.txt
├── apps/
│   ├── forward3d.cpp
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
