# Wave3D Project Handoff

Last updated: 2026-09-09

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

Increment 4a replaced the preliminary CFL hook with the accepted exact
radius-six spectral bound and independent design-band accuracy checks.

## Production I/O

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
Increment 3; `origin/main` remains at `63ecc49`. Local `main` contains the
scientific reference gate and verified work through Increment 4e after that
remote commit. Fetch uses the HTTPS URL and push uses the authenticated SSH
URL. Never force-push or rewrite shared history. The user authorizes local
commits; a later push still requires an explicit request.

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

At the Increment 2 gate, the conservative `200^3` example budgeted 18
padded-volume fields: three physical model fields, five placeholder coefficient
fields, nine wavefields, and one sponge field. It also budgeted 1000 receivers
× 4000 samples × three components, a 64 MiB workspace, and a 512 MiB runtime
reserve. Increment 4a later replaced the coefficient placeholder count with
the nine actually prepared fields; its current measurements are recorded
below.

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
Increment 4a subsequently replaced the provisional CFL helper with the
accepted coefficient-aware formula and tests.

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

## Increment 4a implementation

Increment 4a was completed on 2026-09-09 without implementing a derivative,
wavefield, or time step:

- `numerics/staggered_grid.hpp` stores all six weights as accepted exact integer
  ratios, derives their compile-time doubles, stores the exact spectral maximum
  `1187803/887040`, and evaluates the phase and group symbols used by tests and
  metadata.
- `numerics/elastic_validation.hpp` applies the coefficient-aware CFL bound,
  requires a positive explicit design frequency, enforces at least five
  minimum-S-wave points per wavelength on x, y, and z, enforces twenty time
  samples per design period, and emits the resolved margins.
- `model/elastic_coefficients.hpp` converts physical `Vp`, `Vs`, and density in
  double precision to padded `float32` `lambda`, `mu`, and `K`; three face
  buoyancies; and three harmonic edge shear moduli. Physical edge samples are
  extended constantly through nonphysical storage before averaging. Overflow
  and required-nonzero underflow are rejected.
- `TimeConfig` and numerical control values use double precision. The obsolete
  provisional safety field and helper are removed. The smoke configuration
  uses `eta=0.9` and `f_design=45 Hz`, which is three times its `15 Hz` Ricker
  central frequency.
- The conservative forward memory plan now enumerates the nine actual prepared
  coefficient volumes rather than the earlier five-field placeholder.

Focused tests retain the exact rational numerators and denominators, verify all
odd Taylor moments through power 13, the Nyquist spectral maximum, five-PPW
phase/group reference errors, exact pass/fail CFL and dispersion thresholds,
resolved metadata, all nine coefficient arrays, heterogeneous face/edge
averages, both halo extremes, valid negative `lambda`, and `float32`
overflow/underflow rejection.

### Increment 4a verification

Commands were the established Release CPU, Release CUDA-enabled, Debug
ASan/UBSan, and Compute Sanitizer invocations. Observed results:

- CPU-only Release: 7/7 tests passed without compiler warnings.
- CUDA-enabled Release: 8/8 tests passed, including the existing RTX 5060
  allocation/copy/fill regression.
- AddressSanitizer and UndefinedBehaviorSanitizer: 7/7 CPU tests passed with
  leak detection disabled for the documented execution constraint.
- Compute Sanitizer memcheck, initcheck, racecheck, and synccheck: zero errors,
  hazards, or warnings.
- The sample reports `dt_limit=0.000970109320535 s`, CFL fraction
  `0.515405830473`, `5.11111111111` S-wave points per design wavelength on all
  axes, and `44.4444444444` samples per design period.
- With nine coefficient fields, planned allocations are approximately
  `1346.2 MiB`; including the `512 MiB` runtime reserve requires approximately
  `1858.2 MiB`. At the sampled `7314.2 MiB` free VRAM, the 80% budget retained
  approximately `3993.2 MiB` headroom.

## Increment 4b implementation

Increment 4b was completed on 2026-09-09 without adding wavefield ownership or
any stress, velocity, source, receiver, or boundary update:

- `numerics/cpu_staggered_derivative.hpp` defines explicit x/y/z axes and
  `IntegerToHalf`/`HalfToInteger` mappings and evaluates the accepted
  radius-six stencil at one padded-storage target.
- The complete target range is `[5,n-6)` for `I->H` and `[6,n-5)` for `H->I`,
  where the upper bound is exclusive. Targets outside these ranges fail rather
  than silently using an incomplete stencil.
- Input may be a padded `float` production field or `double` validation field.
  The operator verifies the exact allocated volume size, accumulates in double,
  and allocates no temporary volume.

The focused test checks constants, affine fields, every polynomial degree from
zero through twelve, all 72 combinations of axis, mapping, coefficient, and
positive/negative impulse location, exact complete-stencil ranges, `float32`
input, invalid field sizes and boundary targets, and sinusoidal grid
refinement. Both mappings produced an observed order of approximately `11.87`
before roundoff, inside the predeclared 12th-order regime.

### Increment 4b verification

- CPU-only Release: 8/8 tests passed without compiler warnings.
- CUDA-enabled Release: 9/9 tests passed, including the unchanged RTX 5060
  CUDA foundation regression.
- AddressSanitizer and UndefinedBehaviorSanitizer: 8/8 CPU tests passed with
  leak detection disabled for the documented execution constraint.
- Compute Sanitizer memcheck, initcheck, racecheck, and synccheck: zero errors,
  hazards, or warnings.

## Increment 4c implementation

Increment 4c was completed on 2026-09-09 without source injection, receiver
sampling, a boundary algorithm, a propagation driver, or CUDA propagation:

- `wave/elastic_wavefield.hpp` owns zero-initialized padded `float32` arrays
  for `vx`, `vy`, `vz`, `sxx`, `syy`, `szz`, `sxy`, `sxz`, and `syz`.
  Ownership is move-only and all component sizes are checked against the grid.
- `physics/cpu_elastic_update.hpp` implements separate stress and velocity
  calls. The first advances `sigma^(n-1/2)` from `v^n`; the second advances
  `v^n` from `sigma^(n+1/2)`. This preserves the source and boundary hook
  positions fixed by the numerical specification.
- Normal stress uses the three `H->I` normal velocity gradients. Each shear
  stress uses its two `I->H` cross gradients and prepared edge shear modulus.
  Each velocity uses one `I->H` normal-stress gradient, two `H->I` shear-stress
  gradients, and its prepared face buoyancy.
- Updates occur only where every derivative needed by that component has a
  complete radius-six stencil. Unqualified outer storage remains unchanged.
  Grid/layout/time-step mismatches and non-finite or overflowing `float32`
  results fail explicitly.

The manufactured stress test uses independent affine slopes for all nine
velocity-gradient terms and checks every one of the six constitutive equations.
The manufactured velocity test uses independent affine slopes for all nine
stress-divergence terms and checks all three momentum equations. Tests also
verify zero initialization, move-only ownership, additive rather than replacing
updates, separation of stress and velocity calls, unchanged outer storage,
malformed layouts, grid mismatch, invalid `dt`, and overflow rejection.

### Increment 4c verification

- CPU-only Release: 9/9 tests passed without compiler warnings.
- CUDA-enabled Release: 10/10 tests passed, including the unchanged RTX 5060
  CUDA foundation regression.
- AddressSanitizer and UndefinedBehaviorSanitizer: 9/9 CPU tests passed with
  leak detection disabled for the documented execution constraint.
- Compute Sanitizer memcheck, initcheck, racecheck, and synccheck: zero errors,
  hazards, or warnings.

## Increment 4d implementation

Increment 4d was completed on 2026-09-09 without a boundary algorithm, a
multi-step propagation driver, CUDA propagation, or physical wave-propagation
claims:

- `acquisition/trilinear_stencil.hpp` defines the integer, three face, and
  three edge lattices. It prepares a fixed eight-node stencil by subtracting
  the field's logical half-cell offsets from the common fractional storage
  coordinate. Every node index and weight is retained explicitly.
- Preparation requires complete allocated support and finite, non-negative
  weights whose double-precision sum is one. It rejects unavailable support;
  no node clipping or weight renormalization occurs at an edge.
- `acquisition/moment_source_injector.hpp` prepares separate `sxx`, `syy`,
  `szz`, `sxy`, `sxz`, and `syz` stencils. At step `n`, it evaluates the Ricker
  moment rate at `t_n=n*dt` and adds
  `-dt*Mij*q(t_n)*w/(dx*dy*dz)` to each stress component. Off-diagonal tensor
  entries are deposited once. All 48 candidate `float32` results are checked
  before any field is changed.
- `acquisition/receiver_sampler.hpp` prepares separate `vx`, `vy`, and `vz`
  stencils. After velocity step `n`, it interpolates the three fields into an
  exactly sized caller-owned output array and labels the samples `(n+1)*dt`.
  Sampling neither resizes nor allocates output storage.
- The focused test verifies all nine lattice assignments and weight sums,
  volume-integrated source increments for all six tensor entries, source sign
  and `q(t_n)` timing, unchanged velocity during stress injection, unavailable
  support rejection, affine-field interpolation for all three velocity
  components, `(n+1)*dt` labels, preallocated-output preservation, grid and
  layout checks, invalid time values, transactional `float32` overflow
  rejection, and non-finite sampled fields.

Surface receiver objects can be prepared when their padded support exists, but
Increment 4d validates interpolation only at interior points. Their physical
coupling to a traction-free surface remains Increment 8 work.

### Increment 4d verification

- CPU-only Release: 10/10 tests passed without compiler warnings.
- CUDA-enabled Release: 11/11 tests passed, including the unchanged RTX 5060
  CUDA foundation regression.
- AddressSanitizer and UndefinedBehaviorSanitizer: 10/10 CPU tests passed with
  leak detection disabled for the documented execution constraint.
- Compute Sanitizer memcheck, initcheck, racecheck, and synccheck: zero errors,
  hazards, or warnings.

## Increment 4e implementation

Increment 4e completed the boundary-free CPU elastic reference on 2026-09-09.
It did not add CUDA propagation, an absorbing or free-surface boundary, file
output, RTM, or performance-oriented kernel fusion:

- `docs/INCREMENT_4E_VALIDATION_PLAN.md` fixed the homogeneous material,
  sampling, source, receiver geometry, phase-derived arrival tolerances,
  symmetry/leakage limits, energy window, and allocation criterion before the
  first multi-step run. It records the rejected `60 m` S trial and the
  geometry-only Revision A; no acceptance threshold was relaxed.
- `physics/cpu_elastic_step.hpp` composes one step in the accepted order:
  stress update, source at `q(n*dt)`, deliberate no-op stress boundary,
  velocity update, deliberate no-op velocity boundary, and receiver sampling
  at `(n+1)*dt`. The caller owns the loop and preallocated trace storage.
- `diagnostics/elastic_energy.hpp` sums face kinetic energy, collocated
  deviatoric/volumetric normal-stress energy, and the three edge shear-energy
  terms in joules. It rejects non-finite wavefields or non-positive required
  coefficients.
- `test_cpu_elastic_physics.cpp` generates in-memory `vx`, `vy`, and `vz`
  records. It validates P and S arrival features with an analytical Ricker-
  derivative matched filter, causal explosion polarity, opposite-axis and
  cubic symmetry, transverse leakage, a pre-boundary post-source energy
  envelope, bitwise repeatability, input rejection before mutation, fixed
  buffer ownership, and zero time-loop allocations.

The accepted Revision A case uses a `41^3` physical grid, six-cell halo,
`10 m` spacing, `Vp=3200 m/s`, `Vs=2200 m/s`, `rho=2500 kg/m^3`, `0.5 ms`
sampling, a `40 Hz` source at `(200,200,200) m`, and six axis receivers at
`90 m`. Delayed arrival runs end at `0.09 s`, before the shortest theoretical
reflected P path at `0.096875 s`. The causal energy run ends at `0.062 s`,
before fastest theoretical boundary contact at `0.0625 s`.

Observed physical metrics:

- P feature: `0.0658258512 s` versus `0.065625 s`, within the fixed
  `0.0010185209 s` tolerance; normalized correlation `0.9939220`.
- S feature: `0.0792410475 s` versus `0.0784090909 s`, within the fixed
  `0.0010263976 s` tolerance; normalized correlation `0.9714117`.
- Maximum recorded symmetry error: `1.21975e-7` relative L2, below `2e-5`.
- Isotropic transverse/radial trace-energy ratio: `2.05475e-16`, below `1e-8`.
- Positive/negative x first significant explosion samples:
  `+2.6643791e-5/-2.6643791e-5 m/s`, both outward.
- Post-source energy `max/min`: `1.00094514`, below `1.05`.
- Repeated wavefields and records: bitwise identical; dynamic allocations in
  every instrumented time loop: zero.

These are interior homogeneous validation records, not surface seismic records
and not persistent files. Surface physics and persistent production I/O were
qualified separately in Increments 8 and 9.

### Increment 4e verification

- CPU-only Release: 11/11 tests passed without compiler warnings; the physical
  test took approximately `11.34 s`.
- CUDA-enabled Release: 12/12 tests passed, including the unchanged RTX 5060
  CUDA foundation regression and the CPU physical test.
- AddressSanitizer and UndefinedBehaviorSanitizer: 11/11 CPU tests passed with
  leak detection disabled for the documented execution constraint; the fully
  instrumented physical test took approximately `178.20 s`.
- Compute Sanitizer memcheck, initcheck, racecheck, and synccheck: zero errors,
  hazards, or warnings.

## Exact next action

Increment 9 is complete. The exact next action is Increment 10 only: qualify
the `200^3` elastic five-side-CPML/free-surface case on the RTX 5060, recording
actual peak device ownership, time per step, throughput, output overhead,
finite-state checks, tool availability, and reproducible commands. Do not add
RTM implementation or optimize without a measured bottleneck.

## Increment 5 implementation and verification

Increment 5 completed the boundary-free CUDA reference on 2026-09-09:

- `cuda/elastic_propagator.cu` implements separate grid-stride stress and
  velocity kernels, fixed-stencil moment-source injection, and component-wise
  receiver sampling in the accepted leapfrog order.
- `cuda/elastic_propagator.hpp` owns all nine wavefields, nine coefficients,
  fixed source/receiver tables, and complete traces in move-only RAII buffers.
  Launch and step calls perform no allocation or full-field transfer.
- `test_cuda_elastic.cpp` compares a deterministic nonzero nine-component
  manufactured state and an eight-step centered explosion with four off-grid
  receivers. Every full field and trace had normalized maximum error `0`
  against CPU, below the predeclared `2e-5` limit; incomplete-stencil corner
  storage remained exactly zero.
- The direct Release run measured `400 us` for eight steps and `1,419,864`
  bytes for all owned device fields/tables/traces in that case.

Verification: CPU Release 11/11, CUDA Release 13/13, ASan/UBSan 11/11, and
Compute Sanitizer memcheck/initcheck/racecheck/synccheck all passed with zero
errors or hazards. These results qualify CUDA interior propagation only; no
absorbing or surface boundary has yet been applied.

## Increment 6 implementation and verification

Increment 6 added a replaceable debug sponge on 2026-09-09:

- `boundary/sponge.hpp` prepares the fixed quadratic factor volume and applies
  it separately to six stresses and three velocities. Physical cells are
  exactly one; enabled face factors multiply at edges/corners.
- `cuda/sponge.cu` and `cuda/sponge.hpp` provide a move-only uploaded profile,
  two allocation-free kernels, and a composed step with the accepted boundary
  hook positions.
- The fixed homogeneous test produced zero pre-boundary trace error and a
  damped/undamped late reflection ratio of `0.01085103272`, versus the `0.12`
  debug threshold. All fields and traces were finite after 600 steps. A direct
  Release run measured `1095.53 ms` undamped and `1143.00 ms` damped.

Verification: CPU Release 12/12, CUDA Release 15/15, ASan/UBSan 12/12.
Compute Sanitizer memcheck/initcheck covered the full long physics case;
racecheck/synccheck covered the focused CUDA sponge kernels. All reported zero
errors, hazards, or warnings. This is not CPML and does not qualify a free
surface.

## Increment 7 implementation and verification

Increment 7 added six-sided unsplit CPML on 2026-09-09:

- `CPML_NUMERICAL_SPEC.md` records the audited Komatitsch–Martin recurrence,
  coefficient profiles, staggering mapping, sources, and independent reuse
  boundary.
- `boundary/cpml.hpp` prepares integer/half axis coefficients, owns 18 CPU
  derivative memories, and provides transparent CPU stress/velocity updates.
- `cuda/cpml.cu` and `cuda/cpml.hpp` own the compact device coefficients and 18
  move-only state arrays and apply the same recurrence in separate CUDA
  kernels. The no-boundary Increment 5 calls remain unchanged.
- The memory planner now selects none, sponge, or CPML. The target CPML boundary
  allocation is `1,060,788,480 bytes`.

The CPU/GPU comparison had zero error across nine fields, 18 states, and three
trace arrays. The normal reflection ratio was `0.0002237981069` (limit
`0.005`); the 68-degree proxy residual ratio was `2.265134082e-6` (limit
`0.02`); the 800-step run stayed finite. CPU Release passed 13/13, CUDA Release
17/17, ASan/UBSan 13/13, and all four focused Compute Sanitizer tools reported
zero errors or hazards.

## Increment 8 implementation and verification

Increment 8 added the traction-free top/five-side-CPML composition on
2026-09-09:

- `boundary/free_surface.hpp` fixes the surface plane, validates top geometry,
  and applies radius-six stress and velocity ghost projection.
- `cuda/free_surface.cu` provides allocation-free projection kernels and the
  full stress/source/surface/velocity/surface/sample step order. It rejects a
  CPML profile whose top side is enabled.
- Surface receivers use the existing three-component staggered interpolator;
  no acquisition-specific sampling path was added.

Final normal and interpolated shear traction was exactly zero and CPU/CUDA
projection was bitwise equal. Surface/reference peaks occurred one sample
apart, retained upward polarity, correlated `0.9892208823`, and had amplitude
ratio `2.0587715`. All fields, CPML states, and traces stayed finite for 600
steps. CPU Release passed 14/14, CUDA Release 19/19, ASan/UBSan 14/14, and all
four focused Compute Sanitizer tools reported zero errors or hazards.

## Increment 9 implementation and verification

Increment 9 added production I/O without adding any file dependency to the
propagator on 2026-09-09:

- `WAVE3D_ENABLE_YAML`, `WAVE3D_ENABLE_HDF5`, and `WAVE3D_ENABLE_SEGY` are
  independent and default off. Ubuntu `libyaml-cpp-dev` and `libhdf5-dev` were
  installed for the enabled qualification build.
- YAML loads and emits a resolved typed forward configuration; strict CSV
  preserves irregular binary64 receiver coordinates and order.
- HDF5 stores model volumes as `[z,y,x]`, one-source three-component records as
  `[source,receiver,time]`, and sparse velocity snapshots with explicit storage
  indices. Schemas, units, axes, coordinates, source metadata, and exact
  binary64 time values are checked during reads.
- SEG-Y writes separate VX/VY/VZ Rev-1, big-endian IEEE-float files and JSON
  sidecars containing semantics that integer trace headers cannot preserve.
  The standalone converter rejects mismatched or unsupported external model
  volumes before producing canonical HDF5.

Enabled-I/O Release passed 18/18 tests and focused ASan/UBSan passed 4/4.
With every optional I/O adapter disabled, CPU Release passed 15/15 and CUDA
Release passed 20/20, demonstrating that propagation remains independent.
