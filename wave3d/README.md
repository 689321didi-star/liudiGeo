# Wave3D

Wave3D is an incremental single-GPU 3D isotropic elastic forward-modeling
project. The legacy Zhang Wei RTM source is kept unchanged outside this
directory and is used only as a numerical reference.

## Increment 1

The first increment intentionally contains no wave-propagation kernels. It
defines and tests:

- physical and allocated grid dimensions;
- the `[z][y][x]` storage convention with contiguous `x`;
- absorbing-boundary and finite-difference halo sizes;
- simulation time and material extrema;
- conservative pre-run validation and overflow checks.

## Build

Requirements: CMake 3.24 or newer and a C++17 compiler.

```text
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CUDA becomes a build requirement in a later increment. Keeping increment 1
CPU-only makes the indexing and configuration rules independently testable.
