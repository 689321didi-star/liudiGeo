# Wave3D

Wave3D is an incremental single-GPU 3D isotropic elastic forward-modeling
project. The legacy Zhang Wei RTM source is kept unchanged outside this
directory and is used only as a numerical reference.

## Current status

The elastic forward roadmap through Increment 11 is implemented and verified:

- physical and allocated grid dimensions;
- the `[z][y][x]` storage convention with contiguous `x`;
- absorbing-boundary and finite-difference halo sizes;
- simulation time and material extrema;
- conservative pre-run validation and overflow checks;
- optional CUDA discovery and error translation;
- move-only RAII device memory;
- a field-by-field elastic forward memory budget;
- a trivial CUDA allocation/copy/fill round trip;
- distinct physical, physical-grid, and padded-storage coordinates;
- validated homogeneous and horizontal-layer elastic models;
- a dimensionally defined Ricker moment-rate function and symmetric
  moment-tensor source;
- deterministic regular or irregular surface receivers for `vx`, `vy`, and
  `vz`;
- exact rational constants for the standard radius-six, 12th-order staggered
  derivative and its spectral maximum;
- padded `float32` elastic `lambda`, `mu`, `K`, face-buoyancy, and edge-`mu`
  coefficient preparation with constant material extension;
- the accepted coefficient-aware CFL limit and explicit design-band spatial
  and temporal dispersion gates.
- a checked CPU radius-six staggered derivative for `I->H` and `H->I` mappings
  along all three storage axes.
- move-only, zero-initialized, nine-component padded `float32` elastic
  wavefield ownership;
- transparent CPU and CUDA stress/velocity propagation;
- replaceable sponge, six-side CPML, and the production traction-free top plus
  five-side CPML boundary;
- typed optional YAML, CSV, HDF5, and SEG-Y production I/O;
- a qualified `200^3`, 4000-step RTX 5060 envelope;
- const wavefield views, observer/factory/receiver interfaces, and a removable
  default-off checkpoint interface for a possible future RTM phase.

The accepted equation contract is in `docs/ELASTIC_NUMERICAL_SPEC.md`; the
target measurements are in `docs/RTX5060_QUALIFICATION.md`. Viscoelasticity,
RTM, imaging, reverse propagation, and P/S decomposition are intentionally not
implemented.

## Build

CPU-only requirements are CMake 3.24 or newer and a C++17 compiler.

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWAVE3D_ENABLE_CUDA=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CUDA support is opt-in and currently targets the RTX 5060 (`sm_120`):

```text
cmake -S . -B build-cuda -DCMAKE_BUILD_TYPE=Release -DWAVE3D_ENABLE_CUDA=ON
cmake --build build-cuda --config Release
ctest --test-dir build-cuda -C Release --output-on-failure
./build-cuda/wave3d_cuda_info
compute-sanitizer --tool memcheck --error-exitcode 1 \
  ./build-cuda/wave3d_cuda_tests
```

Keeping CUDA optional preserves independently testable CPU configuration and
memory-planning rules.

Production I/O and RTM-ready checkpoint interfaces are independently opt-in:

```text
-DWAVE3D_ENABLE_YAML=ON
-DWAVE3D_ENABLE_HDF5=ON
-DWAVE3D_ENABLE_SEGY=ON
-DWAVE3D_ENABLE_RTM=ON
```

`WAVE3D_ENABLE_RTM` adds interfaces and mock tests only; it does not build an
RTM executable.
