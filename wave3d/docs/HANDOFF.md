# Wave3D Project Handoff

Last updated: 2026-09-08

## Purpose

This document records the current scope, evidence, decisions, verified work,
and exact next action for a future agent. It distinguishes implemented and
tested behavior from planned numerical work.

## Current product scope

Wave3D is an incremental, scientifically verifiable, single-GPU program for 3D
isotropic **elastic** wave forward modeling on Linux. The initial target is a
desktop NVIDIA GeForce RTX 5060 and a `200 x 200 x 200` physical grid.

The user explicitly removed all viscoelastic work on 2026-09-08. Do not add
`Qp`, `Qs`, attenuation compensation, relaxation mechanisms, or constitutive
memory variables. Forward modeling is the current deliverable. RTM and deep
learning remain separate future research phases and must not enter the current
propagator.

Development is strictly incremental. Each increment has a narrow acceptance
gate, must compile, and must pass proportionate tests before the next increment
begins.

## Identified paper and its role

The user supplied:

```text
https://onlinelibrary.wiley.com/doi/10.1111/1365-2478.13023
```

Verified metadata:

- Wei Zhang, Jinghuai Gao, Zhaoqi Gao, and Ying Shi.
- “2D and 3D amplitude-preserving elastic reverse time migration based on the
  vector-decomposed P- and S-wave records.”
- *Geophysical Prospecting* 68(9), 2712–2737.
- First published 27 August 2020.
- DOI `10.1111/1365-2478.13023`.

The paper concerns elastic RTM, vector-decomposed P/S records, crosstalk, and
amplitude/polarity preservation. It is not a viscoelastic constitutive paper.
The user cannot provide the full PDF. The public publisher abstract and
metadata may be cited, but inaccessible equations or experiment parameters
must not be invented. Use auditable primary sources for the elastic forward
equations and explicitly label independent derivations.

## Legacy repository

The preserved reference source is outside this directory at:

```text
../2D_and_3D_elastic_reverse_time_migration-master/
```

The user requires this directory to remain unchanged. It is evidence and a
numerical reference, not the architecture of the new program.

Previously observed limitations include:

- The 3D makefile expects `3D_elastic_modeling.cu`, which is absent.
- Important called kernels such as `fwd_vx_3D`, `fwd_vy_3D`, `fwd_vz_3D`, and
  `fwd_txxzzxzpp_3D` were not found in the supplied 3D files.
- The build uses obsolete CUDA 5.5 sample paths and Seismic Unix libraries.
- One large `GPUdevice` structure mixes forward and reverse fields, P/S
  decomposition, imaging, I/O, host staging, and multi-GPU remnants.
- Suspicious CUDA initialization calls were observed and require verification.

Do not fill missing kernels by guesswork or copy the monolithic structure.

## Fixed physical and architectural choices

- Coordinates: x east, y north, z positive downward, surface at `z=0`.
- Volume layout: `[z][y][x]`, x contiguous.
- Physics: first-order velocity–stress, 3D isotropic elastic.
- Production wavefields: `float32` structure-of-arrays.
- Source: general symmetric moment tensor; isotropic explosion is a validation
  preset.
- Acquisition: surface, three-component particle velocity.
- Early boundary: replaceable sponge.
- Final boundary: traction-free top and CPML on the other five sides.
- CPU reference precedes CUDA propagation kernels.
- GPU allocations use RAII and are preceded by an explicit memory plan.
- Full wavefield history, RTM images, reverse fields, and P/S-decomposed fields
  are not allocated by the forward executable.

The preliminary CFL helper is only an architectural safety hook. It is not a
proven limit for the final high-order staggered-grid operator.

## Planned I/O

- Generated homogeneous and layered models for early deterministic tests.
- YAML run configuration and resolved configuration metadata.
- HDF5 as the canonical internal format for `Vp`, `Vs`, density, traces, and
  sparse snapshots.
- CSV for irregular receiver geometry.
- Separate VX, VY, and VZ SEG-Y files for external trace exchange.
- Raw binary/CSV/JSON only for small early diagnostics.

The propagator must not parse file formats. I/O adapters produce validated
domain objects. Do not save every full 3D time step.

## Repository state

The Git repository root is now:

```text
/home/ld/tyut/3dsrc
```

`wave3d/.git` was deliberately removed, so `wave3d` is managed by the root
repository. The root remote is:

```text
origin https://github.com/689321didi-star/liudiGeo.git
```

The local `main` branch tracks `origin/main`. The user requested a push after
Increment 3; local and remote were synchronized at `63ecc49`. Fetch uses the
HTTPS URL and push uses the authenticated SSH URL. Never force-push or rewrite
shared history. The user authorizes local commits; a later push still requires
an explicit request.

## Target environment recorded on 2026-09-08

- OS: Ubuntu 24.04.4 LTS.
- Kernel: `7.0.0-30-generic` x86_64.
- C++ compiler: GCC/G++ 13.3.0.
- CMake: 3.28.3, installed from Ubuntu packages during Increment 1.
- CUDA compiler: NVIDIA CUDA Toolkit 13.2, `nvcc` 13.2.78.
- GPU: NVIDIA GeForce RTX 5060.
- Compute capability reported by `nvidia-smi`: 12.0.
- Driver: 595.84.
- Total VRAM: 8151 MiB.
- Free VRAM at inspection: 7398 MiB; this is time-dependent.

## Increment 1 implementation

The CPU-only core contains:

- `CMakeLists.txt`: C++17 interface library, smoke executable, CTest target,
  and compiler warnings.
- `include/wave3d/core/grid.hpp`: physical/allocated dimensions, halo and
  boundary widths, `[z][y][x]` indexing, physical origins, bounds checks, and
  checked `size_t` arithmetic.
- `include/wave3d/core/simulation_config.hpp`: time configuration, elastic
  material extrema, top-boundary consistency, finite-value checks, a
  provisional CFL estimate, and validation.
- `apps/forward3d.cpp`: temporary hard-coded `200^3` smoke configuration.
- `tests/test_grid.cpp`: dimension, indexing, configuration, non-finite input,
  top-boundary consistency, time-step count, and overflow tests.

The sample uses a six-cell halo, 20 absorbing cells on both x and y sides, zero
top absorbing cells, 20 bottom absorbing cells, `10 m` spacing, `0.5 ms` time
step, and `2 s` duration. Expected allocation is `252 x 252 x 232` with 4000
time steps.

## Increment 1 verification

Initial unmodified source and the corrected source both configured, compiled,
and passed tests on the target Linux machine. Final commands:

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
./build/wave3d_forward
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build build-sanitize --parallel
ASAN_OPTIONS=detect_leaks=0 \
  ctest --test-dir build-sanitize --output-on-failure
```

Final observed results:

- Configure and build completed without compiler warnings.
- CTest: 1/1 test passed.
- AddressSanitizer and UndefinedBehaviorSanitizer: 1/1 test passed with leak
  detection disabled. LeakSanitizer itself cannot run under the active
  `ptrace`-based execution environment and exits before testing code.
- Physical cell count: 8,000,000.
- Allocated grid: `252 x 252 x 232`.
- Time steps: 4000.
- Provisional time-step limit: approximately `0.000649519 s`.

## Increment 2 implementation

Increment 2 adds CUDA infrastructure without implementing wave propagation:

- `WAVE3D_ENABLE_CUDA=OFF` by default preserves a CUDA-free CPU build.
- The target CUDA architecture defaults to `sm_120` when CUDA is enabled.
- `cuda_error.hpp` translates runtime failures into exceptions that retain the
  CUDA code and failed-operation context, and provides launch/sync checks.
- `DeviceBuffer<T>` is non-copyable, move-only, checks byte-count overflow,
  owns all explicit `cudaMalloc`/`cudaFree` calls, and provides bounded host
  copies and zeroing.
- `device_info` queries ordinal, name, compute capability, driver/runtime
  versions, and CUDA-allocatable total/free memory.
- The pure C++ elastic memory planner enumerates model, coefficient, wavefield,
  sponge, receiver, workspace, and runtime-reserve bytes before allocation.
- `fill_kernel` is a grid-stride smoke kernel used only to validate allocation,
  launch checking, synchronization, and round-trip copies.
- `wave3d_cuda_info` prints the device and complete initial memory plan.

The conservative `200^3` example currently budgets 18 padded-volume fields:
three physical model fields, five derived coefficient fields, nine wavefields,
and one sponge field. It also budgets 1000 receivers × 4000 samples × three
components, a 64 MiB workspace, and a 512 MiB runtime reserve. This is a
planning baseline, not a claim that later CPML or propagator ownership is
already designed.

## Increment 2 verification

Commands run on 2026-09-08:

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWAVE3D_ENABLE_CUDA=OFF
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure

cmake -S . -B build-cuda -DCMAKE_BUILD_TYPE=Release \
  -DWAVE3D_ENABLE_CUDA=ON
cmake --build build-cuda --config Release --parallel
ctest --test-dir build-cuda -C Release --output-on-failure
./build-cuda/wave3d_cuda_info

compute-sanitizer --tool memcheck --error-exitcode 1 \
  ./build-cuda/wave3d_cuda_tests
compute-sanitizer --tool initcheck --error-exitcode 1 \
  ./build-cuda/wave3d_cuda_tests
compute-sanitizer --tool racecheck --error-exitcode 1 \
  ./build-cuda/wave3d_cuda_tests
compute-sanitizer --tool synccheck --error-exitcode 1 \
  ./build-cuda/wave3d_cuda_tests
```

Observed results:

- CPU-only build: 2/2 tests passed.
- CUDA build: 3/3 tests passed, including the RTX 5060 allocation/copy/fill and
  move-ownership test.
- `cuobjdump --list-elf build-cuda/wave3d_cuda_tests` reports an embedded
  `sm_120` cubin, confirming the target architecture was generated.
- Compute Sanitizer: zero memcheck, initcheck, racecheck, and synccheck errors.
- CUDA Runtime reported 7699.2 MiB allocatable total and 7212.0 MiB free at the
  sampled moment. This is intentionally distinguished from `nvidia-smi`'s
  nominal board-memory report.
- Planned field allocations: approximately 1121.4 MiB.
- Required including runtime reserve: approximately 1633.4 MiB.
- Allowed budget at 80% of sampled free memory: approximately 5769.6 MiB.
- Plan result: fits with approximately 4136.2 MiB budget headroom. Available
  memory is time-dependent and must be queried for every production run.

## Increment 3 implementation

Increment 3 adds deterministic physical inputs without propagation code:

- `coordinates.hpp` defines separate physical-point, physical-grid, and
  padded-storage coordinate/index types, validates the inclusive grid-node
  domain, and performs checked mappings.
- `physical_model.hpp` owns unpadded `[z][y][x]` `Vp`, `Vs`, and density arrays,
  validates their sizes and cells, reports extrema, and generates homogeneous
  or horizontal-layer models.
- `source.hpp` validates a six-component symmetric moment tensor in N·m,
  provides a positive isotropic-explosion preset, evaluates a delayed Ricker
  wavelet robustly, and prepares source storage coordinates.
- `receiver.hpp` prepares arbitrary receiver locations and generates regular
  three-component surface geometry with deterministic x-fastest ordering.
- `wave3d_forward` prints resolved source/receiver coordinate conventions,
  tensor order and values, source-time parameters, and receiver components.
- Coordinate, model, and acquisition tests cover valid and invalid domains,
  layer interfaces, extrema, deterministic generation, waveform values,
  metadata, tensor validation, and receiver ordering.

No source injection, interpolation weights, finite-difference coefficients,
wavefield state, or time stepping is implemented. The later scientific gate
made the metadata dimensionally explicit without adding an injector.

## Increment 3 verification

Commands run on 2026-09-08:

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWAVE3D_ENABLE_CUDA=OFF
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
./build/wave3d_forward

cmake -S . -B build-cuda -DCMAKE_BUILD_TYPE=Release \
  -DWAVE3D_ENABLE_CUDA=ON
cmake --build build-cuda --config Release --parallel
ctest --test-dir build-cuda -C Release --output-on-failure

cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DWAVE3D_ENABLE_CUDA=OFF \
  -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build build-sanitize --config Debug --parallel
ASAN_OPTIONS=detect_leaks=0 \
  ctest --test-dir build-sanitize -C Debug --output-on-failure

compute-sanitizer --tool memcheck --error-exitcode 1 \
  ./build-cuda/wave3d_cuda_tests
```

Observed results:

- CPU-only Release: 5/5 tests passed without compiler warnings.
- CUDA-enabled Release: 6/6 tests passed, including the existing RTX 5060
  allocation/copy/fill regression.
- AddressSanitizer and UndefinedBehaviorSanitizer: 5/5 CPU tests passed with
  leak detection disabled for the previously documented execution constraint.
- Compute Sanitizer memcheck: zero errors.
- The `200^3` smoke executable maps the sample source at
  `(1000 m, 1000 m, 500 m)` to fractional padded storage coordinate
  `(126, 126, 56)` and reports nine regular surface receivers.

## Scientific reference gate

The gate was completed on 2026-09-08 before any propagation operator was
written:

- `ELASTIC_NUMERICAL_SPEC.md` is the accepted implementation contract for the
  3D isotropic elastic interior. It records all nine continuous equations,
  tension-positive stress, component units and staggered locations, exact
  standard 12th-order radius-six coefficients, `I->H` and `H->I` indexing,
  leapfrog time levels and update order, material averages, source sign and
  volume normalization, receiver interpolation, stability, dispersion, and
  predeclared CPU tests.
- `LEGACY_AUDIT.md` indexes and hashes the supplied 3D fragments, records the
  upstream deletion history of the missing main and kernel files, distinguishes
  corroborated facts from unsupported behavior, and prohibits copying because
  no license is supplied.
- Primary method references are Virieux (1986), Graves (1996), Moczo et al.
  (2002), Fornberg (1988), and Holberg (1987). The exact coefficients, CFL
  formula, moment-source sign conversion, and numerical thresholds are
  independently written-out Wave3D derivations.
- The selected solid-domain constraints are `Vs>0` and
  `Vp^2>(4/3)Vs^2`, ensuring positive shear and bulk moduli. Model and
  configuration tests now reject violations.
- `RickerWavelet` now exposes a peak moment rate in `s^-1`. Resolved metadata
  declares `f_i=-M_ij*s(t)*partial_j(delta)` and the corresponding
  tension-positive stress-rate source `-M_ij*q(t)*delta`.

No legacy code, finite-difference operator, wavefield, propagation loop,
boundary algorithm, or CUDA propagation kernel was added during this gate.
The existing provisional CFL helper deliberately remains conservative until
Increment 4a installs the accepted coefficient-aware formula and tests.

### Gate verification

The exact coefficient moments and spectral maximum were independently checked
with rational symbolic arithmetic:

```text
2 sum c_m*(m-1/2)       = 1
2 sum c_m*(m-1/2)^p     = 0  for p=3,5,7,9,11
power-13 error term      = -231/54525952
max half-symbol sum A    = 1187803/887040
cubic-grid CFL number    = 0.431159698015551 (eta=1)
sample dt limit          = 0.000970109320535 s (eta=0.9)
```

The accepted source/model interface changes were then checked with the same
Release CPU, Release CUDA-enabled, ASan/UBSan, and CUDA memcheck regression
commands used for Increment 3. Observed results:

- CPU-only Release: 5/5 tests passed without compiler warnings.
- CUDA-enabled Release: 6/6 tests passed, including the existing RTX 5060
  runtime regression.
- AddressSanitizer and UndefinedBehaviorSanitizer: 5/5 CPU tests passed with
  leak detection disabled for the documented execution constraint.
- Compute Sanitizer memcheck: zero errors.
- `wave3d_forward` prints the accepted tension-positive stress convention,
  distributional body-force sign, stress-rate sign, and Ricker `s^-1` units.

## Exact next action

The scientific reference gate is complete after its documentation, validation
changes, regression tests, and commit are reviewed. Do not implement the whole
CPU propagator in one change.

The exact next action is Increment 4a only: implement compile-time exact
radius-six FD constants, elastic `lambda/mu/K` and staggered buoyancy/shear
coefficient preparation, the coefficient-aware CFL formula, and design-band
dispersion validation. Add focused unit tests, compile CPU and CUDA-enabled
builds, run sanitizers, update this handoff, and commit before starting the CPU
derivative operator in Increment 4b.
