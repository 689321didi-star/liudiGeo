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

### `model`

Separates physical input fields from derived propagation coefficients.

```text
PhysicalModel: Vp, Vs, rho
ElasticCoefficients: inverse density, lambda, mu, staggered averages
```

Generated test models and HDF5-loaded models must produce the same validated
`PhysicalModel` interface.

### `wave`

Owns velocity and stress state. Production layout is SoA:

```text
velocity: vx, vy, vz
stress: sxx, syy, szz, sxy, sxz, syz
```

An owning object manages lifetime. `WavefieldView` provides controlled access
to operators, diagnostics, checkpoints, and optional imaging without exposing
ownership.

### `numerics`

Defines spatial finite-difference operators, staggering, update ordering, and
time integration. Difference coefficients should be compile-time constants or
small constant-memory data after validation. The first correct implementation
may use separate kernels for clarity; fusion is allowed only after reference
tests pass.

### `physics`

Contains the 3D isotropic elastic constitutive update. Attenuation and
viscoelastic state are intentionally outside the project scope.

Conceptual time step:

```text
update stress
inject stress/moment-tensor source at its defined staggered time
apply relevant stress boundary operations
update particle velocities
inject force source if configured
apply velocity boundary operations
sample receivers at a documented time level
notify diagnostics/output/checkpoint observers
```

The exact source order and half-step convention must be fixed by the numerical
derivation and encoded in tests before kernels are finalized.

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

`ReceiverSet` supports surface three-component particle-velocity sampling and
eventually arbitrary geometry. Mapping/interpolation weights are prepared once,
then reused during the time loop. Per-step host/device transfers are forbidden.

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
