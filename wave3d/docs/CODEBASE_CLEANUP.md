# Initial Codebase Cleanup

**Status:** Implemented and verified, 2026-09-15.

This pass reduces the default build surface before desktop development without
discarding scientific validation evidence or future RTM seams.

## Removed

- `apps/forward3d.cpp` and its `wave3d_forward` target. This was a 51-line
  hard-coded configuration printer from the initial foundation increment. It
  did not propagate a wavefield and was superseded by the tested YAML/HDF5
  `wave3d_run` production command.
- Redundant local build trees, the archived pre-layout build, failed configure
  trees, a local converter helper build, and Python bytecode caches. These were
  ignored generated files and contained no source or unique scientific data.

## Disabled by default

- `wave3d_cuda_info`, `wave3d_qualify_rtx5060`, and
  `wave3d_qualify_visualization` now require
  `WAVE3D_BUILD_QUALIFICATION_TOOLS=ON`. Their sources remain available because
  they reproduce accepted device, memory, performance, and visualization
  measurements, but ordinary solver and future desktop builds no longer
  compile them.
- RTM interfaces remain controlled by `WAVE3D_ENABLE_RTM=OFF` by default.
- The MatIO Overthrust converter remains controlled by
  `WAVE3D_ENABLE_OVERTHRUST=OFF` by default.
- YAML, HDF5, SEG-Y, and CUDA adapters retain their existing independent build
  switches.

## Retained deliberately

- Unit, CPU/GPU, numerical, boundary, I/O, and pipeline tests are scientific
  regression evidence and are not redundant application code.
- Increment plans, decisions, handoff records, and qualification reports retain
  the assumptions and acceptance evidence needed for later solver changes.
- The optional RTM checkpoint seam is small and required by the stated future
  direction; it adds no default target.
- The local Overthrust source model, prepared HDF5 model, and one canonical
  dense SEG-Y remain ignored data rather than repository payload. They are
  required for full milestone qualification and are protected by the local
  checksum manifest.

No numerical kernel, file format, model, receiver geometry, or accepted output
was changed by this cleanup.

## Verification

- A fresh Release configuration with CUDA, YAML, HDF5, and SEG-Y enabled and
  qualification tools left at their default `OFF` built successfully and
  exposed neither the removed placeholder nor the three qualification targets.
- That clean configuration passed 28/28 tests.
- Reconfiguration with `WAVE3D_BUILD_QUALIFICATION_TOOLS=ON` built all three
  retained qualification executables successfully.
