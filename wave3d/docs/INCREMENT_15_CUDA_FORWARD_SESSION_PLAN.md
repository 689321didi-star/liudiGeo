# Increment 15 Predeclared CUDA Forward Session Plan

## Purpose

Prepare the accepted CUDA elastic solver for a future desktop controller
without changing its equations, precision, source convention, receiver
sampling, CPML recurrence, free-surface projection, or SEG-Y contract.

The increment introduces a file-format-independent, stateful CUDA forward
session. It owns one prepared problem for its complete lifetime and advances
only at whole elastic time-step boundaries. Qt, rendering, snapshots, and RTM
remain outside this increment.

## Interface scope

The CUDA session shall:

- own coefficients, nine wavefields, source/receiver stencils, receiver
  traces, CPML profile/state, and the optional traction-free surface;
- expose total and completed step counts;
- advance one or more requested steps and synchronize only at the batch
  boundary;
- return the accepted completed-step velocity/stress time metadata;
- expose a non-owning const device-wavefield view for later CUDA visualization
  extraction;
- allow explicit host wavefield download after a completed batch for
  validation and diagnostics;
- allow receiver-trace download only after all requested samples complete;
- reject zero-sized advances, advances after completion, mismatched grids,
  inconsistent top CPML/free-surface composition, and use after a CUDA batch
  failure.

The session shall not:

- open YAML, HDF5, SEG-Y, or any other file;
- allocate, resize, or transfer a full field inside an elastic step;
- create a worker thread or implement GUI pause/resume policy;
- expose mutable device wavefields;
- add a wavefield snapshot implementation;
- change any numerical kernel in this increment.

## Fixed validation

The following acceptance checks are fixed before implementation:

1. A small free-surface/five-side-CPML problem run through deliberately uneven
   batches must match the existing direct CUDA step composition bitwise in all
   nine final wavefields and all three receiver components.
2. Initial progress is zero; batch progress, velocity time, stress time, and
   completion state follow the accepted leapfrog convention exactly.
3. The const device view reports the exact allocated grid/cell count and nine
   non-null pointers without exposing ownership.
4. Invalid zero-step, post-completion, premature trace-download, grid-mismatch,
   and free-surface/top-CPML combinations fail explicitly.
5. The existing HDF5-to-CUDA-to-SEG-Y pipeline test passes through the new
   session without changing its output contract.
6. The complete repository CUDA build and test suite passes.
7. The accepted Overthrust 101 x 101 experiment is repeated after the change;
   its SEG-Y must be byte-for-byte identical to SHA-256
   `3854ec1776b3fbfa5ed3000869c99341c7aa67a03980295a04fe14e8087914c9`.
8. The production CLI advances the full remaining interval as one batch, so
   this structural increment adds no per-step device synchronization.

## Follow-on boundary

After this increment passes, a separate increment may add CUDA visualization
extraction buffers and rendering-oriented scalar fields. Precision changes,
compact CPML slabs, kernel fusion, and register/block tuning remain separate
measured numerical/performance increments.
