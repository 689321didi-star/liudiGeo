# Wave3D

Wave3D is an incremental single-GPU 3D isotropic elastic forward-modeling
project. The legacy Zhang Wei RTM source is kept unchanged outside this
directory and is used only as a numerical reference.

## Current foundation

The first three increments intentionally contain no wave-propagation kernels.
They define and test:

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
- deterministic regular surface receivers for `vx`, `vy`, and `vz`.

The accepted interior-equation contract and its predeclared validation tests
are in `docs/ELASTIC_NUMERICAL_SPEC.md`. No propagation operator is implemented
yet.

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
