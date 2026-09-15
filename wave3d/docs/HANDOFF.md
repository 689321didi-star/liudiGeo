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
- One standards-based three-component SEG-Y file for external trace exchange.
- `wave3d_run CONFIG.yaml` for the validated canonical-HDF5-to-CUDA-to-SEG-Y
  production path.
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

The local `main` branch tracks `origin/main`. `origin/main` remains at
`63ecc49`; after the Increment 14b commit, local `main` is 18 commits ahead and
contains the scientific reference gate, the general HDF5-to-SEG-Y production
pipeline, and verified Overthrust work through the pure model transformation.
Fetch uses the HTTPS URL and push uses the authenticated SSH URL. Never
force-push or rewrite shared history. The user authorizes local commits; a
later push still requires an explicit request.

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

The elastic forward roadmap, single-file SEG-Y amendment, and general
YAML/HDF5-input production driver are complete through Increment 13. The user
authorized only the reduced derived-isotropic SEG/EAGE Overthrust benchmark in
Increment 14. Increment 14b pure model transformation is complete; add the
audited source-container adapter and generate/verify HDF5 and YAML artifacts
in Increment 14c next. Do not start Salt, SEAM, fluid-solid, anisotropic,
viscoelastic, RTM, imaging,
decomposition, optimization, or deep-learning work.

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
- Increment 9 originally wrote separate VX/VY/VZ Rev-1, big-endian IEEE-float
  files and JSON sidecars. Increment 12 supersedes only that organization with
  one self-describing three-component file.
  The standalone converter rejects mismatched or unsupported external model
  volumes before producing canonical HDF5.

Enabled-I/O Release passed 18/18 tests and focused ASan/UBSan passed 4/4.
With every optional I/O adapter disabled, CPU Release passed 15/15 and CUDA
Release passed 20/20, demonstrating that propagation remains independent.

## Increment 10 implementation and verification

Increment 10 qualified the full target forward envelope on 2026-09-09:

- `wave3d_qualify_rtx5060` fixes the production `200^3` grid, five-side CPML,
  traction-free top, homogeneous elastic material, source, nine receivers, and
  exact numerical validation. It replans against current free memory before
  allocating. Increment 10 added optional canonical HDF5 trace output;
  Increment 12 also permits a single SEG-Y output selected by file extension.
- The `252 x 252 x 232` allocation ran 4000 steps in `292785.148 ms`, or
  `73.196287 ms/step` and `201.279718` million allocated-cell steps/s.
- Owned device objects used `2023.692627 MiB`; CUDA free memory fell by
  `2090.25 MiB`. The conservative allocation plus 512 MiB reserve was
  `2704.293579 MiB`, safely below the contemporaneous `5885.849999 MiB`
  configured budget.
- Final exhaustive downloads found no NaN/Inf in nine wavefields, 18 CPML
  states, or three-component traces. Surface traction was exactly zero.
  Target-size one-step Compute Sanitizer memcheck reported zero errors.
- Trace download took `0.090972 ms`; writing the 440,408-byte HDF5 record took
  `0.645352 ms`. Generated traces and profiler reports were removed.

Nsight Systems attributed 99.8% of GPU kernel time to the two CPML main
kernels. Nsight Compute measured the stress kernel at 87.54% compute and
13.41% memory throughput, with FP64 the dominant pipeline. The accepted double
accumulation remains unchanged; any precision, register-pressure, or fusion
optimization requires a separate numerical gate. Full commands and all
measurements are in `RTX5060_QUALIFICATION.md`. Final CPU Release, CUDA
Release, and ASan/UBSan suites passed 15/15, 20/20, and 15/15; full
free-surface memcheck/initcheck and focused racecheck/synccheck reported zero
errors or hazards.

## Increment 11 implementation and verification

Increment 11 finalized removable RTM-ready seams without implementing RTM on
2026-09-09:

- `elastic_wavefield_view.hpp` exposes all nine fields as non-owning const
  ranges with checked indexing and copied grid identity.
- `IForwardObserver` receives completed-step velocity/stress time levels after
  sampling. The CPU reference factory owns inputs, state, frame storage, and
  receiver-major traces; an instrumented test observed zero step allocations
  and bitwise equality to direct CPU calls.
- `IReceiverData` and `ThreeComponentReceiverDataReader` provide checked,
  format-independent component/sample/metadata reads.
- `WAVE3D_ENABLE_RTM=OFF` is the default. Enabling it adds only the optional
  `ICheckpointStore`, fixed-interval observer, and mock tests. The mock saved
  steps 2 and 4, restored nine fields bitwise, rejected absent/wrong-grid
  requests, and reproduced metadata/state bitwise across runs.

RTM-on Release passed 17/17 and focused ASan/UBSan passed 2/2. With the entire
`optional/rtm` tree temporarily absent, RTM-off still configured, built, and
passed. The forward SHA-256 remained
`f56399fb419e73648db3707cf408823f52e8a6c47ad9cf508e0836f761e106f1`.
Final RTM-off CPU Release passed 16/16 and CUDA Release passed 21/21. No reverse
wavefield, image, P/S decomposition, checkpoint persistence, or RTM executable
exists. The final combined CUDA/YAML/HDF5/SEG-Y/RTM-interface Release build
passed 25/25 tests.

## Increment 12 implementation and verification

Increment 12 changed only the optional external trace output on 2026-09-09:

- `write_segy(path, traces)` now creates exactly one big-endian SEG-Y Revision
  1 file and no per-component files or JSON sidecars.
- Each receiver contributes three consecutive traces: VX/in-line code 14,
  VY/cross-line code 13, and VZ/vertical code 12. The file is marked as one
  fixed-length common-source ensemble with IEEE `float32` sample format 5.
- The writer now sets both `SCALCO` and `SCALEL` to `-1000`, writes source and
  receiver horizontal coordinates in millimetres, maps receiver z/down to
  negative elevation, and writes source z/down as positive source depth.
- The ASCII textual header records axes, component mapping, trace order, SI
  units, lack of normalization, source metadata, and the `(n+1)*dt` first-
  sample convention. Fractional-microsecond Revision 1 sampling is rejected
  rather than silently rounded.
- `wave3d_qualify_rtx5060 STEPS output.sgy` now writes this single file when
  CUDA and SEG-Y are enabled. HDF5 output remains selected by `.h5`/`.hdf5`.

The focused test independently parsed the raw file bytes and checked file
size, all relevant textual/binary/trace headers, component order and codes,
coordinate/depth/elevation scaling, every IEEE sample, truncation rejection,
and absence of legacy outputs. A one-step target GPU integration run produced
one 10,188-byte file with 27 traces for nine receivers and component codes
`14,13,12` repeated per receiver. The all-options Release suite passed 25/25,
the focused ASan/UBSan I/O suite passed 4/4 with leak detection disabled, and
the optional-I/O-off CPU Release suite passed 16/16. The qualification
executable also compiled in no-output, HDF5-only, SEG-Y-only, and combined
adapter configurations.

## Increment 13 implementation and verification

Increment 13a established model-bound configuration metadata on 2026-09-09:

- `ForwardRunConfiguration` no longer contains a redundant single
  `homogeneous_material`; its `SimulationConfig` owns all six declared extrema.
- YAML schema `wave3d.forward.v2` reads and writes `material_extrema` with
  minimum/maximum Vp, Vs, and density. Missing and v1 schemas are rejected.
- The focused YAML Release test preserves six deliberately distinct extrema
  through a write/read round trip and passes malformed and old-schema cases.

Increment 13b then added the production pipeline:

- `wave3d_run CONFIG.yaml` is built only when CUDA, YAML, HDF5, and SEG-Y are
  enabled. Relative input/output paths are resolved from the YAML directory.
- It reads a canonical Wave3D HDF5 model, checks exact grid and declared versus
  calculated extrema, validates the Rev1 sample axis and live 80%-plus-reserve
  memory plan before allocation, and prepares the accepted CUDA propagation
  objects.
- Free-surface configurations use five-side CPML; absorbing-top configurations
  use six-side CPML. Accepted CPML constants remain fixed and its velocity and
  frequency come from actual model/source metadata.
- After all configured steps, the program downloads three-component receiver
  data and creates only `<output_directory>/record.sgy`.

The heterogeneous `9^3`, 40-step end-to-end case produced finite nonzero
samples and the expected 14/13/12 trace sequence. Deliberate grid and extrema
mismatches failed before propagation and left no SEG-Y. All-options Release
passed 26/26, full-pipeline Compute Sanitizer memcheck reported zero errors,
focused I/O ASan/UBSan passed 4/4, and optional-I/O-off CPU Release passed
16/16. No-adapter, HDF5-only, and SEG-Y-only CUDA builds also remained green.

## Increment 14a implementation and verification

Increment 14a fixed the complete acceptance plan and audited the Overthrust
source without changing production or test code:

- The SEG page identifies Aminzadeh, Brac, and Kunz (1997) as the scientific
  authority and CC BY 4.0 as the license. Its advertised S3 CD1 object returned
  HTTP 403 to both direct metadata and ranged data requests on 2026-09-09.
- The accepted transport mirror is the 149,938,918-byte MATLAB v5 file linked
  by `leileely/FDwave3D`, whose repository points back to the SEG source. Its
  SHA-256 is
  `251fd1fbd2e1d9ac6aa227960c348f27e08f38325ff7d6d650aa69d77ef105f6`.
- The decoded `data` variable is `[z,y,x]=[187,801,801]`, its `d` vector is
  `[25,25,25]` m, every one of 119,979,387 cells is finite and exactly
  binary32-representable, and the extrema are `2178.83447265625-6000 m/s`.
  These values agree with independent published descriptions of the model.
- A depth-integrated horizontal-gradient scan and orthogonal-slice inspection
  selected zero-based source window `[0:187,153:353,154:354]`. It retains 25 m
  sampling, performs no interpolation or smoothing, and produces a
  `[187,200,200]` Wave3D volume.
- Expected binary32 Vp, derived Vs, and derived Gardner-density hashes and
  extrema are frozen in `OVERTHRUST_SOURCE_AUDIT.md` as independent converter
  oracles.

This documentation-only gate required no compilation. `git diff --check`
passed. Downloaded MATLAB data and generated audit images remain under the
ignored `data/` directory and are not committed.

## Increment 14b implementation and verification

Increment 14b added only the file-format-independent Overthrust model
transformation:

- `model/derived_overthrust.hpp` validates source shape, crop arithmetic and
  bounds, output storage geometry, and every selected Vp before mutation of any
  caller-owned state.
- It directly copies canonical `[z,y,x]` Vp, calculates `Vs=Vp/sqrt(3)` and
  `rho=1000*0.31*Vp^(1/4)` in binary64, converts once to binary32, and validates
  every elastic material and the complete returned `PhysicalModel`.
- The unique-index test proves x-fastest output, z/y/x crop offsets, unchanged
  Vp, exact formulas, deterministic output, and retained 25 m/output-boundary
  geometry. Truncation, NaN, zero Vp, float32 underflow, zero/out-of-range crop,
  crop-range overflow, and allocated-grid overflow all fail explicitly.

Increment 14c began with a prerequisite correction for heterogeneous model
metadata. The configuration validator no longer combines global minimum Vp
and global maximum Vs into a material that may not exist. Exact extrema
identity is still checked by the production task, and the loaded HDF5 model
still enforces positive bulk modulus at every physical cell.

The next 14c sub-gate added a default-off MatIO adapter. It strictly checks
the audited MATLAB v5 `d`, `n`, and `data` contract, decodes only the requested
hyperslab, and reorders MATLAB column-major `[z,y,x]` values to Wave3D
x-fastest storage. Synthetic unique-index tests prove the mapping and reject
spacing/shape mismatches, non-binary32 data, and missing data. Focused Release
and ASan/UBSan tests both passed. At that point, real converter/HDF5/YAML
generation was the next action.

Increment 14c then completed canonical artifact generation. The real audited
MAT file produced an ignored 89,768,192-byte `overthrust_small.h5` and a
3,373-byte matching YAML. Immediate HDF5/YAML rereads were exact. Independent
`h5dump` little-endian extraction matched the three frozen dataset SHA-256
oracles exactly. All 7,480,000 values per property were finite; independently
recomputed Vs/rho matched every float; every cell had positive bulk modulus;
and inspected horizontal/x-z/y-z slices retained consistent complex structure
and depth direction. The 1 ms configuration passed at 65.49% of CFL and 6.28
minimum S-wave points per design wavelength. The complete real-source
preparation path also passed ASan/UBSan. Increment 14d CUDA smoke, refinement,
full propagation, and SEG-Y checks are next.

The first 14d gate now passes. The fixed 64-cubed/eight-step Overthrust smoke
profile completed through `wave3d_run` and Compute Sanitizer memcheck reported
zero errors. Its only output was a 10,944-byte `record.sgy` containing 27
receiver-major 14/13/12 traces, 8 samples per trace at exactly 1 ms, and
216/216 finite values (55 nonzero). Time refinement and the full production
run remain.

The second 14d gate also passes. Full-model 1 ms/800-sample and
0.5 ms/1600-sample runs took 55.99 s and 111.63 s of propagation. The committed
standard-library verifier aligned coarse `n` to fine `2n+1` under the
`(n+1)dt` convention and measured all-trace normalized L2
`0.000433639742266`; VX/VY/VZ were `0.000454674475987`,
`0.000432879948454`, and `0.000387158120674`. All are far below the fixed 5%
gate. The full 3 s production run and final regression matrix followed.

Increment 14d and the authorized Overthrust work are now complete. The full
3000-step production run used 2,710,794,920 planned bytes against a live
6,078,123,212-byte budget and completed in 207.773 s propagation. Its output
directory contains only a 4,446,720-byte `record.sgy`. The committed independent
parser verifies 363 receiver-major 14/13/12 Revision 1 IEEE traces, 3000
samples at exactly 1 ms, all 1,089,000 samples finite, 1,015,337 nonzero,
positive VX/VY/VZ energy, and 121/121 significant arrivals inside conservative
travel bounds.

Final all-options Release passed 28/28, focused I/O ASan/UBSan passed 5/5,
optional-I/O-off CPU Release passed 17/17, and CUDA no-I/O, HDF5-only,
SEG-Y-only, and all-adapter builds passed. Detailed evidence and reproduction
commands are in `OVERTHRUST_FORWARD_QUALIFICATION.md`. Stop here unless the
user explicitly authorizes a new model or research phase.

Observed commands/results:

```text
cmake -S . -B build-cpu -DCMAKE_BUILD_TYPE=Release \
  -DWAVE3D_ENABLE_CUDA=OFF -DWAVE3D_ENABLE_YAML=OFF \
  -DWAVE3D_ENABLE_HDF5=OFF -DWAVE3D_ENABLE_SEGY=OFF
cmake --build build-cpu --parallel 2
ctest --test-dir build-cpu --output-on-failure
# 17/17 passed

cmake -S . -B build-derived-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DWAVE3D_ENABLE_CUDA=OFF -DWAVE3D_ENABLE_YAML=OFF \
  -DWAVE3D_ENABLE_HDF5=OFF -DWAVE3D_ENABLE_SEGY=OFF \
  -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build build-derived-sanitize \
  --target wave3d_derived_overthrust_tests --parallel 2
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-derived-sanitize \
  -R '^wave3d_derived_overthrust_tests$' --output-on-failure
# 1/1 passed
```

## SEG-Y receiver-gather visualization

The dependency-free `tools/plot_segy.py` utility reads Wave3D's fixed-length,
big-endian SEG-Y Revision 1 IEEE-float output and renders its receiver-major
VX/VY/VZ traces as either a labeled standalone SVG or a plain PNG. It validates
the file sizing, sample axis, format code, component codes, equal component
counts, and finite samples before rendering. Time-bin reduction retains the
largest absolute sample and therefore does not erase narrow arrivals. Display
scaling clips each component independently at a configurable absolute-value
percentile; it does not modify the SEG-Y data. A square receiver count is
rendered with separators between its x-fastest rows so that flattening the
surface array does not look like a propagation discontinuity. The optional
`--shared-scale` mode uses one absolute-amplitude color limit for all three
panels when component strength must be compared directly.

The commands below preserve the historical paths used before the data-layout
migration. Their 121-receiver artifacts were subsequently deleted as
requested; current runs follow `data/README.md`.

Observed commands/results:

```text
python3 -m py_compile tools/plot_segy.py
python3 tools/plot_segy.py \
  data/overthrust/overthrust_output/record.sgy \
  data/overthrust/plots/record_gathers.svg
python3 tools/plot_segy.py \
  data/overthrust/overthrust_output/record.sgy \
  data/overthrust/plots/record_gathers.png
python3 tools/plot_segy.py --shared-scale \
  data/overthrust/overthrust_output/record.sgy \
  data/overthrust/plots/record_gathers_shared.svg
# All renderings: 121 receivers, 3000 samples, dt=0.001 s.
```

## Dense 101 by 101 Overthrust receiver run

The explicitly requested dense run retained the qualified physical model,
source, 3 s sample axis, and 4 km by 4 km receiver aperture. The receiver
spacing changed from 400 m to 40 m, producing 10,201 surface receivers and
30,603 receiver-major VX/VY/VZ traces. The old 121-receiver `record.sgy`, its
forward log, and its derived plots were deleted before this run; the audited
MAT and prepared HDF5 inputs were retained.

The new configuration required 3,073,674,920 planned bytes against a live
5,826,727,116-byte budget on the RTX 5060 Laptop GPU. All 3000 steps completed
in 200,400.441 ms propagation and 204.54 s wall time. The resulting SEG-Y is
exactly 374,584,320 bytes with SHA-256
`3854ec1776b3fbfa5ed3000869c99341c7aa67a03980295a04fe14e8087914c9`.

The streaming dense-grid verifier checked all 30,603 trace headers and all
91,809,000 samples. Every sample was finite, 86,284,671 were nonzero, all
10,201 receiver arrivals were inside their conservative travel windows, and
the first-significant range was 0.294–0.857 s. Early radial polarity agreed
with the Mxy quadrant sign at 9,895/10,000 off-axis receivers. The original MAT
three views, marked crop, actual HDF5 Vp crop, Vp/Vs/density section, two center
line gathers, and dense peak/arrival maps were rendered from the actual data.

Observed commands/results:

```text
ctest --test-dir build/overthrust --output-on-failure --parallel 2
# 26/26 passed

build/overthrust/wave3d_run \
  data/overthrust/runs/forward_101x101/config.yaml
# receivers=10201, samples=3000, exit=0

python3 tools/verify_and_plot_segy_grid.py \
  data/overthrust/runs/forward_101x101/output/record.sgy \
  data/overthrust/runs/forward_101x101/figures/receiver_maps.svg \
  --report data/overthrust/runs/forward_101x101/reports/segy_verification.txt \
  --side 101 --spacing 40
# result=PASS

python3 tools/plot_overthrust_model.py \
  data/overthrust/source/overthrust_3d_vp.mat \
  data/overthrust/models/elastic_200x200x187.h5 \
  data/overthrust/figures/model \
  --h5dump /home/byai/.cache/wave3d-toolchain/env/bin/h5dump
# crop_slice_match=PASS
```

## Local data-layout standardization

Local Overthrust artifacts now follow the versioned `data/README.md` contract.
The downloaded MAT moved to `source/`, the prepared elastic HDF5 model moved to
`models/`, model-wide views moved to `figures/model/`, and both receiver
configurations moved into self-contained `runs/forward_11x11/` and
`runs/forward_101x101/` directories. The dense run keeps separate `output/`,
`figures/`, `logs/`, and `reports/` subdirectories. Compiler output and the
local zlib preparation helper moved to the repository-level ignored `build/`
tree.

The YAML model paths were changed to relative paths for the new layout. The
MAT, HDF5, and SEG-Y payloads were moved without rewriting: their SHA-256
values remain `251fd1f...105f6`, `229a723e...43c`, and
`3854ec17...14c9`. `manifests/SHA256SUMS` verifies the primary inputs,
configurations, record, and SVGs. The dense SEG-Y streaming verifier again
checked all 30,603 traces and 91,809,000 samples and returned `PASS`; the model
plotter again returned `crop_slice_match=PASS`. A fresh repository-level CUDA
build passed 26/26 tests.

## Pre-optimization Overthrust revalidation

Before solver control and performance work, the unchanged dense Overthrust
configuration was run again on 2026-09-15. The repository build passed 26/26
tests before propagation. The 200 x 200 x 187 physical model, 101 x 101
three-component surface array, 3 Hz Mxy source, 1 ms step, 3000 samples,
traction-free top, and five-side CPML all remained unchanged.

The run exited successfully after 241,562.642 ms of propagation. Its
374,584,320-byte SEG-Y had SHA-256
`3854ec1776b3fbfa5ed3000869c99341c7aa67a03980295a04fe14e8087914c9`
and was byte-for-byte identical to the accepted canonical output. An
independent scan again found 91,809,000/91,809,000 finite samples, 10,201 of
10,201 arrivals inside the conservative bounds, and 9,895/10,000 expected Mxy
off-axis polarity matches. No new validation figure was retained. The
duplicate SEG-Y was removed after comparison so the organized dataset keeps
one canonical copy.

This propagation was 20.54% slower than the preceding 200,400.441 ms
measurement while producing identical data. Future optimization benchmarks
must therefore control GPU power/clock state and use repeated measurements.
The detailed report is
`data/overthrust/runs/forward_101x101/reports/pre_optimization_revalidation_20260915.txt`.

## Desktop solver preparation

The user authorized solver preparation before any desktop UI work. Increment
15 adds `CudaForwardSession`: stable CUDA ownership, positive whole-step batch
advance, completed-step time metadata, explicit validation downloads, and a
const device-wavefield view. The production task now composes through this
session but advances all remaining steps in one batch, preserving the previous
single-synchronization behavior. A focused free-surface/five-side-CPML test
proved that deliberately uneven batches reproduce direct CUDA composition
bitwise in all nine final fields and all VX/VY/VZ traces.

Increment 16 adds a reusable 29,920,000-byte scalar device volume for the
target physical grid and GPU extraction of centred Vx/Vy/Vz, speed, divergence,
and curl magnitude. The affine staggered-coordinate test checks every physical
voxel against analytic values and proves extraction leaves the source fields
bitwise unchanged. Target-grid five-run means were 1.058/1.104/1.070 ms for
Vx/Vy/Vz, 2.042 ms for speed, 4.840 ms for divergence, and 30.754 ms for curl
magnitude. No display image was generated.

CUDA RTM-off passed 28/28 tests, all-options RTM-on passed 29/29, and CPU-only
passed 17/17. The active environment did not contain `compute-sanitizer`; no
existing numerical CUDA kernel changed. The production Overthrust session run
completed in 240,622.832 ms propagation and produced a 374,584,320-byte SEG-Y
with the accepted SHA-256
`3854ec1776b3fbfa5ed3000869c99341c7aa67a03980295a04fe14e8087914c9`.
`cmp` confirmed byte-for-byte identity, after which the duplicate was removed.
The full report is
`data/overthrust/runs/forward_101x101/reports/desktop_solver_preparation_20260915.txt`.

The desktop controller must add its persistent visualization buffers to the
existing memory preflight through `ForwardMemoryPlanRequest::workspace_bytes`.
For this grid, a planned three-volume render buffer is 89,760,000 bytes (about
85.6 MiB).

## Increment 17 desktop shell

The accepted desktop review is now fixed in
`docs/DESKTOP_DEVELOPMENT_REVIEW.md`, and the observed development-machine
capacity is recorded in `docs/DESKTOP_ENVIRONMENT_QUALIFICATION.md`. The
current RTX 5060 Laptop GPU and 8 GiB VRAM are sufficient for the first forward
desktop/live-display release. WSL2/WSLg remains development evidence only;
native Linux is still required for release qualification. Qt 6.8.4 and Noto
Sans CJK were installed in the local development environment.

Increment 17 adds the default-off `WAVE3D_BUILD_DESKTOP` boundary and the
`wave3d_studio` Qt 6 Widgets/OpenGL shell. Its modern dark interface contains
one large 3-D viewport, stacked XY/XZ/YZ viewports, eight experiment modules,
a shared six-field selector defaulting to velocity magnitude, run-state
controls, a log dock, and a disabled wavefield-snapshot action. Theme rules are
centralized. Inspection, WSLg OpenGL smoke, and review-image capture modes make
the empty shell testable before scientific data is connected.

The Qt-enabled CPU build passed 18/18 tests. The WSLg XCB smoke returned zero
after all four viewports created valid OpenGL contexts. A 1440 x 900 review
image was captured with the four OpenGL framebuffers composited into the main
window and inspected for typography, spacing, control duplication, and view
proportions. The full CUDA/YAML/HDF5/SEG-Y build then passed 28/28 tests. A
default build still omits every desktop target and does not search for Qt.

Model loading, scientific rendering, experiment persistence, worker threads,
solver control, snapshot writing, and RTM remain outside Increment 17.

Verification commands were:

```text
cmake --build build/desktop17 --parallel 2
ctest --test-dir build/desktop17 --output-on-failure --parallel 2
# 18/18 passed

QT_QPA_PLATFORM=offscreen \
  build/desktop17/wave3d_studio --inspect-shell
# viewport_count=4, display_field_count=6, default_display_field=speed

QT_QPA_PLATFORM=xcb build/desktop17/wave3d_studio --smoke-test
QT_QPA_PLATFORM=xcb build/desktop17/wave3d_studio \
  --capture-shell /tmp/wave3d-desktop-shell-17-verified.png
# both exited 0; capture is 1440 x 900

cmake --build build/desktop17-default --parallel 2
# WAVE3D_BUILD_DESKTOP=OFF; wave3d_studio is absent

cmake --build build/overthrust --parallel 2
ctest --test-dir build/overthrust --output-on-failure --parallel 2
# 28/28 passed
```

## Increment 18 desktop project workspace

The desktop now has a Qt Core-only project layer beneath the Widgets shell.
`project.wave3d.json` uses `wave3d.desktop.project.v1` and records the project
identity, creation time, safe relative model reference, queue preference,
shared display field, and unique shot table. New projects start with
`shot-001` and create `source/`, `models/`, `figures/model/`, `runs/`, and
`manifests/` together. Loading rejects unknown schemas, wrong JSON types,
invalid timestamps/identifiers, duplicate shots, unsafe paths, and incomplete
workspace trees. Saving uses `QSaveFile` atomic publication.

Run preparation creates `runs/<run_id>/` once, writes the supplied resolved
YAML bytes exactly as `config.yaml`, creates `output/`, `figures/`, `logs/`,
and `reports/`, and atomically publishes a `wave3d.desktop.run.v1` manifest
with project/shot/run identities and the configuration SHA-256. It refuses an
existing run ID, unsafe ID, unknown shot, or empty configuration. No method
mutates a prepared run.

New/Open actions now call this store. The left dock shows active project name,
path, and shot count; shared display choice changes are persisted. Window
geometry, dock state, and last project live in Qt settings, and the last valid
project reopens on startup. Preflight and all run controls remain disabled
until later experiment-editor validation is implemented. The adjusted layout
keeps all eight modules visible at the verified window size.

The first fresh Conda-compiler configuration left
`CMAKE_CXX_FLAGS_RELEASE` empty, so the long CPU physics test was stopped after
inspection showed it running unoptimized at full CPU utilization. The build
was immediately reconfigured with `-O3 -DNDEBUG`; no test failure occurred.
The corrected clean build and verification commands were:

```text
cmake -S . -B build/desktop18 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/home/byai/.cache/wave3d-toolchain/env/bin/x86_64-conda-linux-gnu-g++ \
  -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG" \
  -DWAVE3D_ENABLE_CUDA=OFF \
  -DWAVE3D_BUILD_DESKTOP=ON \
  -DCMAKE_PREFIX_PATH=/home/byai/.cache/wave3d-toolchain/env
cmake --build build/desktop18 --parallel 2
ctest --test-dir build/desktop18 --output-on-failure --parallel 2
# 19/19 passed

QT_QPA_PLATFORM=xcb build/desktop18/wave3d_studio --smoke-test
QT_QPA_PLATFORM=xcb build/desktop18/wave3d_studio \
  --capture-shell build/desktop18/review/wave3d-desktop-shell.png
# both exited 0; all four OpenGL contexts were valid

cmake --build build/desktop17-default --parallel 2
# WAVE3D_BUILD_DESKTOP=OFF; wave3d_studio is absent

cmake --build build/overthrust --parallel 2
ctest --test-dir build/overthrust --output-on-failure --parallel 2
# 28/28 passed
```

Forward-YAML editing/parsing, model loading, scientific volume rendering,
source/acquisition editors, CUDA worker ownership, and SEG-Y result viewing
remain deferred. Increment 19 should be predeclared before starting the static
model scene.
