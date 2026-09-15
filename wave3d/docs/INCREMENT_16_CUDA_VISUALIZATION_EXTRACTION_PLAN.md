# Increment 16 Predeclared CUDA Visualization Extraction Plan

## Purpose

Add the solver-side data product required by a future desktop renderer: one
physical-domain scalar volume derived on the GPU from a synchronized const
elastic-wavefield view. This increment still contains no Qt, OpenGL, display
thread, image generation, or snapshot output.

## Fixed fields and layout

The first interface supports:

- cell-centred `Vx`, `Vy`, and `Vz`;
- cell-centred particle-velocity magnitude;
- divergence of particle velocity as a P-wave display quantity;
- curl magnitude of particle velocity as an S-wave display quantity.

Every output is one `float32` volume shaped `[nz][ny][nx]` with x contiguous.
It contains only the physical model domain; halo and absorbing cells are not
exported. Full physical resolution is fixed for this increment. Filtered
downsampling is deferred rather than introducing an aliased preview path.

The staggered velocities must first be interpreted on their actual X-half,
Y-half, and Z-half lattices. Component values are linearly interpolated to
integer physical nodes. Divergence uses the accepted radius-six half-to-
integer derivative. Curl components are calculated on their natural YZ-, XZ-,
and XY-half edge lattices and interpolated to the same integer nodes before
forming curl magnitude.

## Ownership and synchronization

A reusable `DeviceVisualizationVolume` owns only one scalar GPU buffer. The
extraction call accepts a validated `DeviceElasticWavefieldConstView`, writes
the existing buffer, allocates nothing, and launches no file I/O. The caller
owns synchronization and frame-buffer policy. The future desktop controller
may therefore allocate two or three volumes once and reuse them. It must add
those persistent render buffers to `ForwardMemoryPlanRequest::workspace_bytes`
before constructing the session. For the qualified `200 x 200 x 187` grid,
three full-resolution scalar volumes require 89,760,000 bytes (about 85.6 MiB)
in that preflight budget.

## Fixed validation

1. Analytic affine velocity fields on their actual staggered coordinates must
   produce the expected centred components, speed, divergence, and curl
   magnitude throughout the physical domain.
2. Output shape, x-fastest indexing, origin removal, and byte count must match
   the physical grid exactly.
3. Extraction must leave all nine source wavefields bitwise unchanged.
4. Mismatched grids and unknown display fields must fail explicitly.
5. Reusing one output object for every supported field must allocate no new
   volume inside extraction.
6. The focused test, complete CUDA suite, CPU-only build, and RTM-on all-option
   build must continue to pass.

## Deferred work

Filtered downsampling, CUDA/OpenGL interop, asynchronous streams, triple-buffer
publication, colour mapping, volume ray casting, orthogonal views, and image or
video export belong to later desktop increments. Scientific snapshots and RTM
checkpoints remain separate storage products.
